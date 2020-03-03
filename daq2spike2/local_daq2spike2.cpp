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



/* Our local version using sonlib32 docs and more recent GPL'ed CED source code
   to turn daq files into smr files. This produces exactly the same output as a
   program using the CED code in a library.  I decided to use that program
   since the lib contains other functions, such as adding different kinds of
   channels other than ADC ones, that we may want to use some day.  Plus, their
   version is a bit faster.

   Mod History
   Mon Feb 25 09:29:20 EST 2019  dale add this comment.
*/

#define _FILE_OFFSET_BITS 64

#define LINUX 1
#define _LARGEFILE64_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>
#include <getopt.h>
#include <math.h>
#include <vector>
#include <array>
#include <string>
#include <memory>
#include <string.h>

#include "sonintl.h"
#include "sonpriv.h"
#include "local_daq2spike2.h"

using namespace std;

using chanInfo = vector<TChannel>;
using LUTvals = vector <TLookup>;

// Globals
static string baseName;
static string dateStamp;
static int firstChanOffset = 0;
static DaqDataBlock daqConvert[daqChans];
static off64_t wholeBlocks;
static off64_t totalBlocks;
static off64_t shortBlock;
static unsigned long maxTick;
static LUTvals chanLUT[daqChans];

// Create some useful info 
static void initConsts(FILE* in_fd0, FILE* in_fd1)
{
   struct stat stats0;
   struct stat stats1;
   unsigned long bytesPerSegment = bytesPerSamp*sampsPerBlock;

   fstat(in_fd0->_fileno,&stats0);
   fstat(in_fd1->_fileno,&stats1);

   if (stats0.st_size != stats1.st_size)
   {
      cout << "FATAL: The .daq files must be the same size." 
       << endl <<  "Are these from the same recording?" 
       << endl << "Exiting. . ." 
       << endl;
   }

   bytesPerSegment = bytesPerSamp*sampsPerBlock;
   wholeBlocks = stats0.st_size / bytesPerSegment;
   shortBlock = (stats0.st_size - (wholeBlocks * bytesPerSegment)) / bytesPerSamp;
   totalBlocks = wholeBlocks;
   if (shortBlock)  // if data exactly fits in wholeblocks, no short block at end
      ++totalBlocks;
   maxTick = stats0.st_size / (wordsPerSamp*sizeof(short)) - 1; // each block of data is a tick
}

static void writeHeader(TFileHead& head, FILE *out_fd)
{
   bzero(&head,sizeof(head));
   head.systemID = 9;             /*  2 filing system revision level */
   strncpy(head.copyright,COPYRIGHT,LENCOPYRIGHT); /* 10 space for "(C) CED 87" */
   strncpy(head.creator.acID,"00000000",8);
   head.usPerTime = 40;           /*  40 microsecs per time unit, 25KHz */
   head.dTimeBase = 0.000001;     /* time scale factor, normally 1.0e-6 */
//   head.usPerTime = 1;           /*  40 microsecs per time unit, 25KHz */
//   head.dTimeBase = 0.000040;     /* time scale factor, normally 1.0e-6 */
   head.timePerADC = 1;           /*  1 time units per ADC interrupt */
   head.fileState = 1;            /*  condition of the file */
   head.channels = daqChans;      /* maximum number of channels */
   head.chanSize = CHANSIZE(daqChans);  /* memory size to hold chans */
   head.firstData = (head.chanSize/DISKBLOCK)+1; /* offset to first data block */
   firstChanOffset = head.firstData;
   head.extraData = 0;            /* No of bytes of extra data in file */
   head.bufferSz = 0x8000;        /* Not used on disk; bufferP in bytes */
                                  /* Will write out this much per channel block */
   head.osFormat = 0;             /* either 0x0101 for Mac, or 0x00 for PC */
   head.maxFTime = maxTick;      /* max time in the data file */
                                 /* date/time that corresponds to tick 0 */ 
   sscanf(dateStamp.c_str(),"%hu-%hhu-%hhu %hhu:%hhu:%hhu",
   &head.timeDate.wYear,
   &head.timeDate.ucMon,
   &head.timeDate.ucDay,
   &head.timeDate.ucHour,
   &head.timeDate.ucMin,
   &head.timeDate.ucSec);
   head.timeDate.ucHun = 0;
   head.cAlignFlag = 1;          /* 0 if not aligned to 4, set bit 1 if aligned */
   head.LUTable = 0;             /* max time in the data file, (set later) */

   off64_t pos = ftell(out_fd);   /* remember pos */
   fseek(out_fd,0,SEEK_SET);      /* first thing in file */
   fwrite(&head,1,sizeof(head),out_fd);
   fseek(out_fd,pos,SEEK_SET);    /* put it back */
}

// Spike2 does not know ahead of time how much data it has, so it keeps running
// tallies as values show up. We know exactly how much data we have to deal
// with, so many of the tallies are constants for a given file. Use that info
// here to init the channel structs.
static void initWaveChan(TChannel& chan, int num)
{
   char text[128];
   bzero(&chan,sizeof(chan));
   chan.kind = Adc;
   chan.nextDelBlock = -1;
   chan.firstBlock = firstChanOffset + num * blocksPerChan; 
   chan.lastBlock = chan.firstBlock + daqChans * blocksPerChan * (totalBlocks-1);
   chan.phySz = blocksPerChan*DISKBLOCK;
   chan.phyChan = num;
   if (totalBlocks > 0xFFFF)  // takes 2 words to hold larger counts
   {
      chan.blocks = totalBlocks % 0x10000;
      chan.blocksMSW = totalBlocks >> 16;
   }
   else
   {
      chan.blocks = totalBlocks;
      chan.blocksMSW = 0;
   } 
   chan.maxData = sampsPerBlock;
   chan.comment.len = 8;
   chan.comment.string[0] = 8;
   sprintf(text,"Chan %3d",num);
   strncpy(&chan.comment.string[1],text,8);
   chan.maxChanTime = maxTick;
   chan.title.len = 0;
   chan.title.string[0] = 0;
   chan.idealRate = 25000.0;
   chan.lChanDvd = 1;
   chan.v.adc.scale = .5; // doc says scale is for +/-5v, we use +/-2.5
   chan.v.adc.units.len = 5;
   chan.v.adc.units.string[0] = 5;
   strncpy(&chan.v.adc.units.string[1],"Volts",5);
   chan.v.adc.divide = 0;
}


// from CED son.c file
static uint32_t calcChk(const void* buff, int num)
{
   uint32_t cksum = 0;
   uint32_t const* pul = (uint32_t const*)buff;
    for (int loop = 0; loop < num; ++loop)
    {
       cout << loop << "  " << *pul << endl;  
       cksum += *pul++;
    }
    return cksum;
}


static void writeLUT(FILE *out_fd)
{
   fseek(out_fd,0,SEEK_END);
   TLUTID lutID;
   TSonLUTHead header;
   int chan;
   size_t size = totalBlocks;
   uint32_t bits = 1;
    
   while (bits < size) // next power of 2
      bits <<= 1;

   header.nSize = bits;  // same for all
   header.nUsed = totalBlocks;
   header.nInc = 1;
   header.nGap = -1;
   header.nCntAddEnd = 0;
   header.nCntGapLow = 0;
   header.nCntGapHigh = 0;
   lutID.ulID = LUT_ID;  // same for all but last

   for (chan = 0; chan < daqChans; ++chan)
   {
      lutID.chan = chan;
      lutID.ulXSum = calcChk(&header,sizeof(TSonLUTHead)/sizeof(uint32_t));
      const TLookup* ptr = chanLUT[chan].data(); 
      uint32_t tmp = calcChk(ptr,header.nUsed);
      lutID.ulXSum += tmp;
      cout << "chksum: " << lutID.ulXSum << endl;
// todo: this almost works, off by just a few values
   }

}

// we are going to write fixed sized buffs for each chan 1-n
// this makes offset-to-next-block calcs easy.
static void writeChans(chanInfo& list, FILE *fd)
{
   fseek(fd, DISKBLOCK, SEEK_SET); // skip header
   for (auto iter = list.begin(); iter != list.end(); ++iter)
   {
      fwrite(&*iter,1,sizeof(*iter),fd);
   }
}

/* 
   setup 128 buffers for daq data
   read one output block's worth of samples into each
   for all active adc chans
      convert 2's cpl to signed short
      init data struct and write block to file
      update chan info - curr last block, # of blocks, max time, etc.

   at daq eof
      seek back to chan area of file
      update chan info with final values
*/
static void convertData(TFileHead& header, chanInfo& list, FILE* in_fd0, FILE* in_fd1, FILE* out_fd)
{
   int recBlock, chan;
   unsigned int total_ticks = 0;
   unsigned long  curr_ticks;
   unsigned long rec;
   unsigned long curr_data_start;
   const unsigned long blocksAllChans = daqChans * stdBlkSize;
   off_t whole;
   unsigned short *in_ptr;
   unsigned short inBuff[wordsPerSamp];
   off_t currpos;
   struct stat stats;
   long predStart = -1;
   long succStart = 0;

   cout << "Whole blocks: " << wholeBlocks << endl << "Samps in last short block: " << shortBlock << endl;

   for (chan = 0; chan < daqChans; ++chan)
      daqConvert[chan].chanNumber = chan+1; // 1-based

   curr_ticks = 0;
   curr_data_start = 1 + CHANSIZE(daqChans) / DISKBLOCK; // 1st data block
   fseek(out_fd, curr_data_start * DISKBLOCK, SEEK_SET); // make sure we're there
   fstat(in_fd0->_fileno,&stats);

   for (whole = 0; whole < totalBlocks; ++whole)
   {
      for (chan = 0; chan < daqChans; ++chan)
      {
         if (predStart == -1)  // no pred for 1st block of each chan
            daqConvert[chan].predBlock = -1;
         else
            daqConvert[chan].predBlock = predStart + chan * stdBlkSize;
         daqConvert[chan].startTime = curr_ticks;
         bzero(daqConvert[chan].TAdc, sizeof(DaqDataBlock::TAdc));
      }
      curr_ticks = total_ticks;
      for (recBlock = 0; recBlock < sampsPerBlock; ++recBlock) // first file 1-64
      {
         rec = fread(&inBuff, sizeof(inBuff), 1,  in_fd0);
         if (!rec)
            break;
         in_ptr = inBuff;
         in_ptr += 2;    // skip 0000 0000 header
           // DAQ data is offset binary
           // 0xffff is max +, 0x8000 is 0, 7fff is first - val, 0000 is max - value
           //  val - 0x8000 is 2's complement
         for (chan = 0; chan < daqChansPerFile; ++chan)
         {
            daqConvert[chan].TAdc[recBlock] = *in_ptr++ - 0x8000;
         }
      }
      for (recBlock = 0; recBlock < sampsPerBlock; ++recBlock) // second file 65-128
      {
         rec = fread(&inBuff, sizeof(inBuff), 1,  in_fd1);
         if (!rec)
            break;
         in_ptr = inBuff;
         in_ptr += 2;    // skip 0000 0000 header
         for (chan = daqChansPerFile; chan < daqChans; ++chan)
            daqConvert[chan].TAdc[recBlock] = *in_ptr++ - 0x8000;
      }

      predStart = curr_data_start;
      succStart = curr_data_start + blocksAllChans;
      total_ticks += recBlock;

//      cout << "offset to 1st data: " << curr_data_start << "  " << blocksAllChans << " succBlock " << succStart << endl;
      for (chan = 0; chan < daqChans; ++chan)
      {
// cout << "offset to block: " << chan << " " << curr_data_start << "  " << blocksAllChans << " succBlock " << succStart << endl;
         if (whole < totalBlocks - 1) // most blocks
            daqConvert[chan].succBlock = succStart;
         else if (whole == totalBlocks - 1) // last, short or no short, just whole
            daqConvert[chan].succBlock = -1;

         daqConvert[chan].items = recBlock;
         daqConvert[chan].startTime = curr_ticks;
         daqConvert[chan].endTime = total_ticks - 1;
         TLookup currLUT;
         currLUT.lPos = curr_data_start;
         currLUT.lStart = curr_ticks;
         currLUT .lEnd = curr_ticks + recBlock - 1;
         chanLUT[chan].push_back(currLUT);
         curr_data_start += stdBlkSize;
         succStart += stdBlkSize;
      }
      curr_ticks += recBlock;
      for (chan = 0; chan < daqChans; ++chan)
         fwrite(&daqConvert[chan], 1, stdBlkSize*DISKBLOCK, out_fd);
      currpos = ftell(in_fd0);
      printf("\rProcessed: %3.2f%%  ", 100.0 * (float)currpos / stats.st_size);
      fflush(stdout);
   }
   cout << endl;

   if (!feof(in_fd0) && !feof(in_fd1))
      cout << "Warning: should be at EOF and are not." << endl;

   
   // todo there is some things in the header we can update, also
   // include updates to channel array after it
   if (totalBlocks >= LUT_MINBLOCKS)  /*!< minimum blocks to write LUT */
   {
      long lutStart = ftell(out_fd);
      header.LUTable = lutStart;
      writeHeader(header, out_fd);
      writeLUT(out_fd);
       // todo actually write the lut, don't forget to pad to DISKBLOCK at end
   }
   writeChans(list, out_fd); // update chan array 
}

static void usage(char *name)
{
   cout << endl << "Usage: " 
   << name << " -n daq_file_basename -t \"date/time stamp\" from recording's log file"
   << endl << "For example: "
   << endl << endl << name << " -n 2014-06-24_001 -t \"2014-06-24 21:31:53:515\""
   << endl <<endl << "The data/time stamp is in the log file and generally looks like this:"
   << endl << "Recording started at 2014-06-24 21:31:53:515"
   << endl << "Note: You must put it in quotes because it contains a space."
   << endl << "This must be run from the directory containing the daq2 files."
   << endl;
}

static int parse_args(int argc, char *argv[])
{
   int ret = 1;
   int cmd;
   static struct option opts[] = {
                                   {"n", required_argument, NULL, 'n'},
                                   {"t", required_argument, NULL, 't'},
                                   { 0,0,0,0} };

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



int main(int argc, char** argv)
{
   TFileHead  header;
   TChannel waveChan;
   chanInfo chanList;
   FILE *in_fd0 = NULL;
   FILE *in_fd1 = NULL;
   FILE *out_fd = NULL;
   int chans;
   string file0, file1, outfile;

   parse_args(argc,argv);
   if (argc < 4)
   {
      usage(argv[0]);
      cout << "Aborting. . ." << endl;
      exit(1);
   }
   file0 = baseName + "_1-64.daq";
   file1 = baseName + "_65-128.daq";
   cout << file0 << " " << file1 << endl;
   in_fd0 = fopen(file0.c_str(),"rb");
   if (!in_fd0)
   {
      cout << "Could not open " << file0 << endl << "Aborting. . ." << endl;
      exit(1);
   }
   in_fd1 = fopen(file1.c_str(),"rb");
   if (!in_fd1)
   {
      cout << "Could not open " << file1 << endl << "Aborting. . ." << endl;
      exit(1);
   }
   outfile = baseName + "_daq.smr";
   out_fd = fopen(outfile.c_str(),"w+b");
   if (!out_fd)
   {
      cout << "Could not open " << outfile << "for writing." 
           << endl << "Aborting. . ." << endl;
      exit(1);
   }
   cout << "Saving daq recordings to " << outfile << endl; 
   initConsts(in_fd0, in_fd1);
   writeHeader(header,out_fd);
   for (chans = 0 ; chans < daqChans; ++chans)
   {
      initWaveChan(waveChan,chans);
      chanList.push_back(waveChan);
   }
   writeChans(chanList,out_fd);
   convertData(header,chanList,in_fd0, in_fd1, out_fd);
   fclose(in_fd0);
   fclose(in_fd1);
   fclose(out_fd);
   return 0;
}
