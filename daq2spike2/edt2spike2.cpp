/*
Copyright 2005-2020 Kendall F. Morris

Ths file is part of a collection of recording processing software.

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




/* Create a .smr file from a .bdt or .edt file. Spike trains are saved as
   event chans, analog as ADC chans.

   Mod History
   Thu Apr 25 15:26:00 EDT 2019 Forked from daq2spike2.cpp
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
#include <map>
#include <sstream>
#include <fstream>
#include <memory>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <string.h>

#ifdef WIN32
#include <windows.h>
#include <time.h>
#endif

#include "s64.h"
#include "s3264.h"
#include "s32priv.h"

using namespace std;
using namespace ceds64;

class intv {public: int t0, t1, dt; intv(): t0(0), t1(0),dt(0){} };
// map is <file chan , smr chan>
using spikeList = map<int, int>;
using spikeListIter = spikeList::iterator;
using analogList = map<int, unsigned int>;
using analogListIter = analogList::iterator;
using analogSamp = map<int, intv>;
using analogSampIter = analogSamp::iterator;
using analogIntv = map<int, unsigned int>;
using analogIntvIter = analogIntv::iterator;

// globals
string inName;
string outName;
string baseName;
double tickSize;
TSTime64 sampIntv;
spikeList sChans;
analogList aChans;
analogSamp intvChans;
analogIntv Intervals;
bool isEdt = true;

static void usage(char *name)
{
   cout << endl << "Usage: "
   << name << " -n <filename.edt | filename.bdt>"
   << endl << "For example: "
   << endl << endl << name << " -n 2014-06-24_001.edt" << endl
   << "or" << endl
   << name << " -n c:\\path\\to\\2014-06-24_001.edt" << endl
   << endl;
}

static int parse_args(int argc, char *argv[])
{
   int ret = 1;
   int cmd;
   static struct option opts[] =
   {
      {"n", required_argument, NULL, 'n'},
      {"h", no_argument, NULL, 'h'},
      { 0,0,0,0}
   };
   while ((cmd = getopt_long_only(argc, argv, "", opts, NULL )) != -1)
   {
      switch (cmd)
      {
         case 'n':
               inName = optarg;
               if (inName.size() == 0)
               {
                  cout << "File name is missing." << endl;
                  ret = 0;
               }
               break;

         case 'h':
         case '?':
         default:
            usage(argv[0]);
            ret = false;
           break;
      }
   }
   if (!ret)
   {
      usage(argv[0]);
      cout << "Exiting. . ." << endl;
      exit(1);
   }
   return ret;
}


/* Scan the file and see how many channels of what kind we have.
   Build lists and assign chan #s in edt/scope order.
   For BDT files, the analog sample rate can vary, so determine what it
   is for this file.
   Exit on fatal errors.
*/
void readFile()
{
   string line;
   unsigned int id;
   TSTime64 time;
   TSTime64 max_val = 0;
   analogSampIter sampIter;
   analogIntvIter intvIter;
   string choice;

   ifstream in_file(inName.c_str());
   if (!in_file.is_open())
   {
      cout << "Could not open " << inName << endl << "Exiting. . ." << endl;
      exit(1);
   }

   getline(in_file, line); // skip header
   getline(in_file, line);
   if (line.find("   11") == 0)
   {
      cout << "bdt file detected" << endl;
      tickSize = 0.0005;  // 2000 ticks / sec
      isEdt = false;
   }
   else if (line.find("   33") == 0)
   {
      cout << "edt file detected" << endl;
      tickSize = 0.0001;  // 10,000 ticks / sec
      sampIntv = 50;
      isEdt = true;
   }
   else
   {
      cerr << "This is not a bdt or edt file, exiting. . ." << endl;
      exit(1);
   }
          // read file and make lists of spike and analog chans
   cout << "Reading " << inName << " (this may take a while)" << endl;
   while (getline(in_file,line))
   {
      id = atoi(line.substr(0,5).c_str());
      time = atoi(line.substr(5).c_str());
      if (id < 4096 && id != 0)
      {
         if (sChans.find(id) == sChans.end())
            sChans.insert(make_pair(id,0));
      }
      else if (id >= 4096)  // analog channel
      {
         id /= 4096;
         if (aChans.find(id) == aChans.end())
            aChans.insert(make_pair(id, 0));
         if ((sampIter = intvChans.find(id)) == intvChans.end())
         {
            sampIter = intvChans.insert(make_pair(id, intv())).first;
            sampIter->second.t1 = time;
         }
         else
         {
            sampIter->second.t0 = sampIter->second.t1;
            sampIter->second.t1 = time;
            off_t delta = sampIter->second.t1 - sampIter->second.t0;
            if ((intvIter = Intervals.find(delta)) == Intervals.end())
            {
               intvIter = Intervals.insert(make_pair(delta,0)).first;
               cout << "new: " << id << " gap:  " << delta << endl;
               cout << "tn:   " << sampIter->second.t0 << endl 
                    << "tn+1: " << sampIter->second.t1 << endl;
            }
            intvIter->second++;
         }
      }
   }
   if (Intervals.size() > 1)
   {
      cout << "The are variable sampling rates for this file." << endl
           << "Here are the rates: " << endl; 
      for (auto iter : Intervals)
         cout << "Rate: " << iter.first << " Occurences: " << iter.second << endl;
      cout << "If there are many instances of the rates, the spike2 file " << endl
           << "will be large and mostly unusable." << endl
           << "If there are just a few, as a side-effect of edits," << endl
           << "the spike2 file will be okay." << endl
           << "Do you want to continue (type y for yes, anything else for no): ";
      getline(cin,choice);
      if (choice[0] != 'y' && choice[0] != 'Y')
      {
         cout << "Exiting. . ." << endl;
         exit(1);
      }
   }
   for (auto iter : Intervals)
      if (iter.second > max_val)
      {
         max_val = iter.second;
         sampIntv = iter.first;
      }
   cout << "Sample interval set to " << sampIntv << " ticks." << endl;

   in_file.close();
   int s2chan = 0;
     // assign chans in edt/bdt/scope chan order
   for (auto iter = sChans.begin(); iter != sChans.end(); ++iter, ++s2chan)
      (*iter).second = s2chan;
   for (auto iter = aChans.begin(); iter != aChans.end(); ++ iter, ++s2chan)
      (*iter).second = s2chan;
   cout << "Found " << sChans.size() << " spike chans" << endl;
   cout << "Found " << aChans.size() << " analog chans" << endl;
}

// Read the edt/bdt file again and save info as if doing
// a real-time recording.
void writeFile()
{
   int res;
   TTimeDate td;
   int num_s = sChans.size();
   int num_a = aChans.size();
   TSTime64 time;
   string line;
   //string cpyline;
   TChanNum chan;
   int ourChan;
   unsigned int id;
   TAdc a_val;
   char text[200];
   int tot_chans = max((int)MINCHANS, num_s + num_a); // lib requires at least 32 chans

   TSon32File sFile(1); // up to 1 TB file
   res = sFile.Create(outName.c_str(),tot_chans);
   if (res != S64_OK)
   {
      cout << "Could not create .smr file, error is " << res << endl;
      return;
   }

   sFile.SetTimeBase(tickSize);
   if (sscanf(baseName.c_str(),"%hu-%hhu-%hhu", &td.wYear, &td.ucMon, &td.ucDay) != 3)
   {
      // Maybe a pre yyyy-mm-dd file? Use date of file creation.
      cout << "No timestamp in file name, using file time instead" << endl;
#ifdef __linux__
      struct stat stats;
      stat(inName.c_str(),&stats);
      struct timespec ftime = stats.st_mtim;
      struct tm *tmloc = localtime(&ftime.tv_sec);
      strftime(text, sizeof(text), "%Y-%m-%d", tmloc);
      sscanf(text, "%hu-%hhu-%hhu", &td.wYear, &td.ucMon, &td.ucDay);
      cout << "timestamp: " << text << endl;
#else
      HANDLE hnd = CreateFile(inName.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
      FILETIME ftime;
      SYSTEMTIME sys_time;
      GetFileTime(hnd,0,0,&ftime);
      FileTimeToSystemTime(&ftime,&sys_time);
      td.wYear = sys_time.wYear;
      td.ucMon = sys_time.wMonth;
      td.ucDay = sys_time.wDay;
      printf("Using %d %d %d as recording date\n", sys_time.wYear, sys_time.wMonth, sys_time.wDay);
      fflush(stdout);
      CloseHandle(hnd);
#endif
   }
   else
      cout << "timestamp: " << baseName << endl;

   td.ucHour = td.ucMin = td.ucSec = td.ucHun = 0;
    // add some comments
   sFile.TimeDate(nullptr,&td);
   stringstream strm;
   auto now = chrono::system_clock::now();
   time_t nowtime = chrono::system_clock::to_time_t(now);
   strm << "edt/bdt file conversion to smr format, version " << VERSION << ".";
   sFile.SetFileComment(0,strm.str().c_str());
   strm.str("");
   strm.clear();
   strm << "Input File: "<< inName;
   sFile.SetFileComment(1,strm.str().c_str());
   strm.str("");
   strm.clear();
   strm << "On " << ctime(&nowtime);
   sFile.SetFileComment(2,strm.str().c_str());
   strm.str("");
   strm.clear();

    // create chans
   for (auto iter = sChans.begin(); iter != sChans.end(); ++iter)
   {
      ourChan = (*iter).first;
      chan = (*iter).second;
      res = sFile.SetEventChan(chan,100,ceds64::TDataKind::EventRise,ourChan);
      if (res != S64_OK)
         cout << "event chan create res: " << res << endl;
      sFile.SetChanUnits(chan,"");
      sprintf(text,"%3d",ourChan); // 9 chars or less
      sFile.SetChanTitle(chan,text);
      sFile.SetBuffering(chan,0x4000);
   }
   for (auto iter = aChans.begin(); iter != aChans.end(); ++ iter, ++chan)
   {
      ourChan = (*iter).first;
      chan = (*iter).second;
      res = sFile.SetWaveChan(chan,sampIntv,ceds64::TDataKind::Adc,tickSize,ourChan);
      if (res != S64_OK)
         cout << "wave chan create res: " << res << endl;
      sFile.SetChanUnits(chan,"");
      sprintf(text,"An %2d",ourChan); // 9 chars or less
      sFile.SetChanTitle(chan,text);
      sFile.SetChanScale(chan,0.5);  // default is +/-5, we use +/-2.5
      sFile.SetBuffering(chan,0x1000);
   }

   ifstream in_file(inName.c_str());
   getline(in_file, line); // skip header
   getline(in_file, line);

// TSTime64 diffs[8] = {0};

   while (getline(in_file,line))
   {
//      cpyline = line;
      id = atoi(line.substr(0,5).c_str());
      time = atoi(line.substr(5).c_str());
      if (id < 4096 && id != 0)
      {
         chan = sChans[id];
//cout << " *** id: " << id << " chan: " << chan << " t: " << time << endl;
         res = sFile.WriteEvents(chan,&time,1);
         if (res != S64_OK)
            cout << "Event chan write error: " << res << endl;
      }
      else if (id >= 4096)   // an analog channel, id & data are packed
      {
         a_val = id % 4096 - (id % 4096 > 2047) * 4096;
         id /= 4096;
         chan = aChans[id];
//cout << "chan: " << id << " " << time << endl;
// cout << cpyline << "    bdt chan: " << id << "  time: " << time << "  sample interval: " << time - diffs[id] << endl;
// diffs[id] = time;
//cout << "t: " << time;
//time = max(time, time -(time%sampIntv));
//cout << " newt: " << time << endl;
         res = sFile.WriteWave(chan,&a_val,1,time);
         if (res < 0)
            cout << "Wave chan write error: " << res << endl;
      }
   }
   sFile.Close();
   // need to mod permissions, they are rw------- by default, not what we want
   chmod(outName.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
}

int main(int argc, char*argv[])
{
   string inFile, outFile;

   cout << "Program to convert .bdt or .edt files to Spike2 .smr files." << endl <<"Version " << VERSION << endl;
   parse_args(argc,argv);
   if (argc < 3)
   {
      usage(argv[0]);
      cout << "Not enough arguments, exiting. . ." << endl;
      exit(1);
   }
   size_t last = inName.find_last_of(".");
   if (last != string::npos)
      baseName = inName.substr(0,last);
   else
   {
      cout << inName << " does not have a .edt or .bdt extension, exiting. . ." << endl;
      exit(1);
   }
   readFile();
   if (isEdt)
      outName = baseName + "_from_edt.smr";
   else
      outName = baseName + "_from_bdt.smr";
   cout << "Creating " << outName << " from " << inName << endl;
   writeFile();
   exit(0);
}



