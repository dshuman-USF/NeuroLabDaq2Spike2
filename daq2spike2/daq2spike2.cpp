/*
Copyright 2005-2020 Kendall F. Morris

This file is part of a collection of recording processing software.

    The is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    The suite is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with the suite.  If not, see <https://www.gnu.org/licenses/>.
*/





/* This was written after the local_daq2spike2 program, mainly to test that
   program's correctness. It all worked correctly except that the checksums for
   the LUT were wrong. When we got the CED GPL'ed code, it was obvious that
   they were only adding the first third of the buffer, probably a bug not a
   feature. The local_daq2spike2 program and this one produces identical output
   except that changed how the values in two vars used to set the sample rate
   were used. The spike2 program does not seem to care, it figures out it is
   25KHz.  All that said, I decided to use this program because the CED lib has
   functions that may one day prove useful. And this program runs a bit faster.

   Mod History
   Mon Feb 25 09:57:36 EST 2019 dale add this comment.
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>
#include <getopt.h>
#include <math.h>
#include <vector>
#include <array>
#include <string>
#include <sstream>
#include <memory>
#include <chrono>
#include <ctime>

#include "s64.h"
#include "s3264.h"
#include "s32priv.h"

// buried in the s64 code, this is the default buff size when creating wave chans
#define S32_BUFSZ 0x8000 

using namespace std;
using namespace ceds64;

const int wordsPerSamp = 66;
const int bytesPerSamp = wordsPerSamp * sizeof(short); // 2 words header, 64 words data
const int dataPerSamp = 64 * 2;  // 64 words data
// 32 disk blocks (16K) will hold 20 byte header, 8182 samples, no pad
const int blocksPerChan = 64;
const int sampsPerBlock = (blocksPerChan*DISKBLOCK - SONDBHEADSZ) / sizeof(short);
const int bytesPerBlock = (blocksPerChan*DISKBLOCK - SONDBHEADSZ);
const int daqChansPerFile = 64;
const int daqChans = 128;
int realDaqChans = daqChans;
// use same size blocks, even if last one only has 1 sample in it
const unsigned long stdBlkSize = (SONDBHEADSZ + (sampsPerBlock) * sizeof(TAdc)) / DISKBLOCK;

// LUT stuff from son32 son.c file
// Globals
static string baseName;
static string dateStamp;
static off64_t wholeBlocks;
static off64_t totalBlocks;
static off64_t shortBlock;
static off64_t maxTick;

string File0, File1;
string outFile;


static void usage(char *name)
{
   cout << endl << "Usage: " 
   << name << " -n daq_file_basename -t \"date/time stamp\" from recording's log file"
   << endl << "For example: "
   << endl << endl << name << " -n 2014-06-24_001 -t \"2014-06-24 21:31:53:515\""
   << endl <<endl << "The data/time stamp is in the DAQ's log file and generally looks like this:"
   << endl << "Recording started at 2014-06-24 21:31:53:515"
   << endl << "If you are using Cygnus recordings, you can use the"
   << endl << "print_cygdate program to get the date and time from one of "
   << endl << "the Cygnus files. The times of the files in a recording may"
   << endl << "differ slihtly."
   << endl << "Note: You must put it in quotes because it contains a space."
   << endl << "This must be run from the directory containing the daq2 files."
   << endl;
}

static int parse_args(int argc, char *argv[])
{
   int ret = 1;
   int cmd;
   static struct option opts[] = 
   {
      {"n", required_argument, NULL, 'n'},
      {"t", required_argument, NULL, 't'},
      { 0,0,0,0} 
   };
   while ((cmd = getopt_long_only(argc, argv, "", opts, NULL )) != -1)
   {
      switch (cmd)
      {
         case 'n':
               baseName = optarg;
               if (baseName.size() == 0)
               {
                  printf("Base file name is missing.\n");
                  ret = 0;
               }
               break;

         case 't':
               dateStamp = optarg;
               if (dateStamp.size() == 0)
               {
                  printf("Date/time stamp is missing, aborting. . .\n");
                  ret = 0;
               }
               break;

         case '?':
         default:
            printf("Unknown argument.\n");
            ret = 0;
           break;
      }
   }
   if (!ret)
   {
      usage(argv[0]);
      cout << "Aborting. . ." << endl;
      exit(1);
   }
   return ret;
}

// Create some useful info 
static void initConsts(FILE* in_fd0, FILE* in_fd1)
{
   struct stat stats0;
   struct stat stats1;
   unsigned long bytesPerSegment = bytesPerSamp*sampsPerBlock;

   fstat(in_fd0->_fileno,&stats0);
   if (in_fd1)
   {
      fstat(in_fd1->_fileno,&stats1);
      if (stats0.st_size != stats1.st_size)
      {
         cout << "FATAL: The .daq files must be the same size." 
              << endl <<  "Are these from the same recording?" 
              << endl << "Exiting. . ." << endl;
         exit(1);
      }
   }
   bytesPerSegment = bytesPerSamp*sampsPerBlock;
   wholeBlocks = stats0.st_size / bytesPerSegment;
   shortBlock = (stats0.st_size - (wholeBlocks * bytesPerSegment)) / bytesPerSamp;
   totalBlocks = wholeBlocks;
   if (shortBlock)  // if data exactly fits in wholeblocks, no short block at end
      ++totalBlocks;
   maxTick = stats0.st_size / wordsPerSamp; // each block of data is a tick
   cout << "Whole blocks: " << wholeBlocks << endl << "Samps in last short block: " << shortBlock << endl;
   cout << "MaxTick: " << maxTick << endl;
}

static short daqData[daqChans][sampsPerBlock];    /* ADC data */

static void convertData(FILE* in_fd0, FILE* in_fd1, TSon32File& sFile)
{
   int recBlock, chan;
   int currtime = 0;
   int rec;
   off_t res;
   off_t whole;
   unsigned short *in_ptr;
   unsigned short inBuff[wordsPerSamp];
   off_t currpos;
   struct stat stats;

   fstat(in_fd0->_fileno,&stats);
   for (whole = 0; whole < totalBlocks; ++whole)
   {
      for (recBlock = 0; recBlock < sampsPerBlock; ++recBlock) // first file 1-64
      {
         rec = fread(&inBuff, sizeof(inBuff), 1,  in_fd0);
         if (!rec)
            break;
         in_ptr = inBuff;
         in_ptr += 2;    // skip 0000 0000 header
          // the daq stores data as "offset binary", so ffff is max pos, 0x8000 is zero,
          // and 0 is max neg. Spike2 wants signed shorts.
         for (chan = 0; chan < daqChansPerFile; ++chan, in_ptr++)
            daqData[chan][recBlock] = *in_ptr - 0x8000;
      }
      if (in_fd1) // second file 65-128, if we have one
      {
         for (recBlock = 0; recBlock < sampsPerBlock; ++recBlock)
         {
            rec = fread(&inBuff, sizeof(inBuff), 1,  in_fd1);
            if (!rec)
               break;
            in_ptr = inBuff;
            in_ptr += 2;    // skip 0000 0000 header
            for (chan = daqChansPerFile; chan < daqChans; ++chan, in_ptr++)
               daqData[chan][recBlock] = *in_ptr - 0x8000;
         }
      }

      for (chan = 0; chan < realDaqChans; ++chan)
      {
         res = sFile.WriteWave(chan, &daqData[chan][0], recBlock, currtime);
         if (res < 0)
            cout << "write error " << res << endl;
      }
//      sFile.Commit(); // The lib saves the first 2 samples out of the order we
                        // write them if we do not force a flush. This makes
                        // comparing the output of this and local_daq2spike2
                        // difficult. Since we are using this as primary
                        // conversion tool, this is not needed.
      currtime += recBlock;
      float percent;
      currpos = ftell(in_fd0);
      percent = 100.0 * (float)currpos / stats.st_size;
      printf("\rProcessed: %3.0f%%  ", percent);
      fflush(stdout);
   }
   printf("\rProcessed: %3.1f%%  ", 100.0);
   if (feof(in_fd0) || feof(in_fd1))
      cout << "EOF" << endl;
   else
      cout << "We seem to have ran out of data before we ran out of file" << endl;
   sFile.Close();
   // need to mod permissions, they are rw------- by default, not what we want
   chmod(outFile.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
}


int main(int argc, char*argv[])
{
   FILE *in_fd0 = NULL;
   FILE *in_fd1 = NULL;
   int chan;
   int res;
   char text[128];
   TTimeDate td;

   cout << "Program to convert .daq files to Spike2 .smr files." << endl <<"Version " << VERSION << endl;
   parse_args(argc,argv);
   if (argc < 4)
   {
      usage(argv[0]);
      cout << "Aborting. . ." << endl;
      exit(1);
   }
   File0 = baseName + "_1-64.daq";
   File1 = baseName + "_65-128.daq";
   in_fd0 = fopen(File0.c_str(),"rb");
   if (!in_fd0)
   {
      cout << "Could not open " << File0 << endl << "Aborting. . ." << endl;
      exit(1);
   }
   in_fd1 = fopen(File1.c_str(),"rb");
   if (!in_fd1)
   {
      cout << "Could not open " << File1 << endl << "Using one recording file." << endl;
      realDaqChans = daqChansPerFile;
      File1 = "";
   }
   cout << File0 << " " << File1 << endl;

   outFile = baseName + "_from_daq.smr";

   initConsts(in_fd0, in_fd1);
   SONInitFiles();   // using static lib, have to do this
   TSon32File sFile(1);
   res = sFile.Create(outFile.c_str(),realDaqChans);
   if (res == S64_OK)
   {
      sFile.SetTimeBase(0.000040);
      sscanf(dateStamp.c_str(),"%hu-%hhu-%hhu %hhu:%hhu:%hhu",
      &td.wYear,
      &td.ucMon,
      &td.ucDay,
      &td.ucHour,
      &td.ucMin,
      &td.ucSec);
      td.ucHun = 0;
      sFile.TimeDate(nullptr,&td);
      stringstream strm;
      auto now = chrono::system_clock::now();
      time_t nowtime = chrono::system_clock::to_time_t(now);
      strm << "DAQ file Conversion to smr format.";
      sFile.SetFileComment(0,strm.str().c_str());
      strm.str("");
      strm.clear();
      strm << "File 1: "<< File0;
      sFile.SetFileComment(1,strm.str().c_str());
      if (in_fd1)
      {
         strm.str("");
         strm.clear();
         strm << "File 2: "<< File1;
         sFile.SetFileComment(2,strm.str().c_str());
      }
      strm.str("");
      strm.clear();
      strm << "On " << ctime(&nowtime);
      sFile.SetFileComment(3,strm.str().c_str());
      strm.str("");
      strm.clear();


      for (chan = 0 ; chan < realDaqChans; ++chan)
      {
         res = sFile.SetWaveChan(chan,1,ceds64::TDataKind::Adc,0.000040,chan);
         if (res != S64_OK)
            cout << "wave chan write res: " << res << endl;
         sFile.SetChanUnits(chan,"Volts");
         sprintf(text,"Chan %3d",chan); // 9 chars or less
         sFile.SetChanTitle(chan,text);
         sFile.SetChanScale(chan,0.5);  // default is +/-5, we use +/-2.5
      }
      sFile.SetBuffering(-1,0x8000,0); // all chans
   }
   convertData(in_fd0, in_fd1, sFile);
}



