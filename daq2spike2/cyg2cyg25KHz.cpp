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




/* Read cygnus digital tape .dd file(s) and upconvert to the nominal 24 KHz to
   25 KHz and write out the adjust file as a dd file with _25KHz as part of the 
   new file name.

   The ideal number of samples between the 5 HZ timing pulse peaks is 4800
   (24000/5).  In practice, the number of sample error varies from 0 to -2. By
   far the most typical error is -1, that is, 1 sample short. The upconversion
   process will take 4800, 4799, 4798, and whatever other sampling under-counts
   and interpolate the samples to the 5000 between peaks. This is the ideal
   number of samples for 25 Khz (25000/5).

   Some of the cygnus tapes have "bad sectors". See comments in cyg_fixup.cpp
   for details. This program works correctly with files that have been fixed.
   Results without the fixup may not be correct.

   The algorithm used here was prototypes in the octave program upscale.m.
   Write a new .dd file out with _25KHz_ as part of the file name.
*/

#define _FILE_OFFSET_BITS 64

#include <stdio.h>
// #include <stdbool.h>
#include <error.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/limits.h>
#include <getopt.h>
#include <errno.h>
#include <dirent.h> 
#include <math.h> 

#include <map>
#include <string>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <array>
#include <vector>
#include <algorithm> 

using namespace std;

const int CYG_BUFF_SIZ = 65024;          // # bytes in a tape sector
const int CYG_SAMP_SIZ = CYG_BUFF_SIZ/2; // 2 bytes per sample
const int CYG_HEADER = 128;
const int CYG_CHANS = 16;
const off_t CYG_CHAN_BLOCK = CYG_CHANS * sizeof(short); // bytes in a sample
const int MAX_TAPES = 4;
const double CYG_RATE = 24000.0;
const double DAQ_RATE = 25000.0;
const double DAQ_RATE_SEC = 1.0 / DAQ_RATE;
const double CYG_RATE_SEC = 1.0 / CYG_RATE;
const double DAQ_INTV = DAQ_RATE / 5.0;    // This many samples in DAQ interval
const double DAQ_INTV_TIME = DAQ_INTV*DAQ_RATE_SEC; // This many milliseconds in same
const int TP_RATE = 5;                     // timing pulse Hz/sec rate 
const double TP_INTV = 1000.0/TP_RATE;     // pulse interval in ms
const int IDEAL_CYG = CYG_RATE / TP_RATE;  // Exactly this many samples / interval
const int INTV_BYTES = IDEAL_CYG*CYG_CHAN_BLOCK;
const string TAPES("ABCD");

#pragma pack(push,1) 
using cygheader = struct 
{
   unsigned short file_num;
   short          unused1;
   short          file_size;
   short          unused2;
   char           mpx;
   char           cdat_type; // always 1
   char           bcd_time[6];
   char           bcd_date[6];
   char           gain[8];
   char           pad[98];
};
#pragma pack(pop)

class OneFile
{
   public:
      string InName = "";
      string OutName = "";
      ifstream InStrm;
      ofstream OutStrm;
      int SyncChan = 0;
};
enum FILE_SLOT {A,B,C,D};

class Interval
{
   public:
      Interval() {reset();};
      void reset() {Peak = 0; PeakBlock = 0;};
      off_t Peak;
      off_t PeakBlock;
};

using IntvProc = array <Interval, MAX_TAPES>;
using IntvProcIter = array <Interval, MAX_TAPES>::iterator;
using DataBuff = array <char,CYG_CHAN_BLOCK>;
using DataBuffIter = array <char,CYG_CHAN_BLOCK>::iterator;
using Files = array <OneFile,MAX_TAPES>;
using FilesIter = array <OneFile,MAX_TAPES>::iterator;
using chanMap = map<int,int>;

// turn the bytes into shorts and shorts into bytes
class Sample
{
   public:
      short sample[CYG_CHANS];
      short & operator[] (int idx) {return sample[idx];}
      void toShort(DataBuff bytes) {
         size_t i, j;
         unsigned char y_l, y_h;
         j = 0;
         for (i = 0; i < sizeof(DataBuff); i+= 2) {
            y_l = bytes[i];
            y_h = bytes[i+1];
            sample[j] = y_l + 256 * y_h;
            ++j;
         }
      }

      DataBuff toBytes() {
         DataBuff buff;
         short val;
         int i, j, y_l, y_h;
         j = 0;
         for (i = 0; i < CYG_CHANS ; ++i) {
            val = sample[i];
            y_l = val & 0xff;
            y_h = (val & 0xff00) >> 8;
            buff[j] = y_l;
            buff[j+1] = y_h;
            j += 2;
         }
         return buff;
      }
};

// globals
Files FList;
string OutName;
string OutTag("_25KHz");
off_t totalBytes;
unsigned long long feedback, throttle;
bool OverWrite = false;
bool HaveRaw = false;
bool HaveArgs = false;
bool Debug = false;

// The order of chans in a cygnus data block recording is not 1,2,3, etc.
// This is the index into the data given chan#.
// E.g. chan 8 is at index 14, or 13 for zero-based indexing.
const chanMap rev_cmap = {
   {1, 1},
   {2, 9},
   {3, 5},
   {4, 13},
   {5, 2},
   {6, 10},
   {7, 6},
   {8, 14},
   {9, 3},
   {10, 11},
   {11, 7},
   {12, 15},
   {13, 4},
   {14, 12},
   {15, 8},
   {16, 16}
};

using dataRates = map<int,vector<double>>;
using daq25 = vector<double>;
using daq25Iter = daq25::iterator;
using daq24 = vector<double>;
using daq24Iter = daq25::iterator;

static void usage(char * name)
{
   printf (
"\nUsage: %s "\
"[-a A_Tape_filename,timing_pulse_chan] "\
"[-b B_Tape_filename,timing_pulse_chan] "\
"[-c C_Tape_filename,timing_pulse_chan] "\
"[-d D_Tape_filename,timing_pulse_chan] "\
"\n\n"\
"Read one or more Cygnus recordings from an experiment and make new Cygnus files\n"\
"that have a constant number of samples per timing pulse\n"\
"interval and upscaled to 25KHz.\n"\
"\nThis can be used in a command line prompt mode, or using command line arguments.\n"\
"If there are no arguments, the program will prompt for input.\n"\
"If using command line arguments, use commas with no spaces\n"\
"This must be run from the directory containing the Cygnus files.\n"\
"\n"\
,name);
}

static bool parse_args(int argc, char *argv[])
{
   static struct option opts[] = { 
                                   {"a", required_argument, NULL, 'a'},
                                   {"b", required_argument, NULL, 'b'},
                                   {"c", required_argument, NULL, 'c'},
                                   {"d", required_argument, NULL, 'd'},
                                   {"h", no_argument, NULL, 'h'},
                                   {"D", no_argument, NULL, 'D'},
                                   { 0,0,0,0} };
   int cmd;
   bool ret = true;
   opterr = 0;
   string arg;
   vector <string> tokens;
   string::size_type off;

   auto tokenize = [&](FILE_SLOT idx) {tokens.clear(); stringstream strm(arg);string str; 
                                       while(getline(strm,str,',')) tokens.push_back(str);
                                       if (tokens.size() < 2) 
                                         {cout << "FATAL ERROR: Timing pulse channel is "
                                             << "missing. Exiting. . ." << endl; exit(1);}
                                       FList[idx].InName = tokens[0];
                                       FList[idx].SyncChan = strtol(tokens[1].c_str(),nullptr,10);};

   while ((cmd = getopt_long_only(argc, argv, "", opts, NULL )) != -1)
   {
      switch (cmd)
      {
         case 'a':
               arg = optarg;
               tokenize(A);
               off = FList[A].InName.find_last_of('.');
               FList[A].OutName = FList[A].InName.substr(0, off) + OutTag + ".dd";
               HaveArgs = true;
               break;
         case 'b':
               arg = optarg;
               tokenize(B);
               off = FList[B].InName.find_last_of('.');
               FList[B].OutName = FList[B].InName.substr(0, off) + OutTag + ".dd";
               HaveArgs = true;
               break;
         case 'c':
               arg = optarg;
               tokenize(C);
               off = FList[C].InName.find_last_of('.');
               FList[C].OutName = FList[C].InName.substr(0, off) + OutTag + ".dd";
               HaveArgs = true;
               break;
         case 'd':
               arg = optarg;
               tokenize(D);
               off = FList[D].InName.find_last_of('.');
               FList[D].OutName = FList[D].InName.substr(0, off) + OutTag + ".dd";
               HaveArgs = true;
               break;
         case 'D':
               Debug = true;
               cout << "Debug turned on." << endl;
               break;
         case 'h':
         case '?':
         default:
            usage(argv[0]); 
            ret = false;
            break;
      }
   }
   return ret;
}

/* Template of linspace function used in octave prototype.
   Due to roundoff error, the last element may not exactly be
   the 'to' value. We make sure it is.
*/
template <typename T> vector<T> linspace(T from, T to, size_t steps) 
{
   T step = (to - from) / static_cast<T>(steps-1);
   vector<T> ticks(steps);
   T val;
   typename vector<T>::iterator x;
   for (x = ticks.begin(), val = from; x != ticks.end()-1; ++x, val += step)
      *x = val;
   *x = to;
   return ticks;
}

/* Find the next timing pulse in a cygnus file. 
   The file will be position on peak sample position.
   That is, the next read will read the peak.
*/
static bool next_pulse(FilesIter& cygfile, Interval& intv)
{
   off_t blk = 0;
   int chan;
   unsigned char sample_l, sample_h;
   short sample;
   bool found = false;
   bool find_peak = false;
   unsigned short max_sample = 0;
   int mono_pos = 0;
   DataBuff inbuff;
   DataBuffIter iter;
   off_t samps_in_block = 0;
   if (Debug) cout << endl << "FIND NEXT PEAK" << endl;
   intv.reset();
   chan = rev_cmap.at(cygfile->SyncChan) - 1;

   while (!found)
   {
      if (!cygfile->InStrm.eof())
      {
         blk = cygfile->InStrm.tellg(); // current position
         cygfile->InStrm.read(inbuff.data(),CYG_CHAN_BLOCK);
         if (cygfile->InStrm.eof()) // if we found EOF and no peak, the previous peak
            return false;           // we found was the last one. Tell caller.
         ++samps_in_block;
      }
      else
      {
         cout << "EOF" << endl;
         return false;
      }

      iter = inbuff.begin() + 2 * chan;  // timing pulse offset
      sample_l = *iter++;
      sample_h = *iter;
      sample = sample_l + 256 * sample_h;
      if (find_peak)  // are in pulse, find max
      {
         if (max_sample < sample)
         {
            max_sample = sample; // possible peak
            intv.Peak = blk;
            intv.PeakBlock = blk/CYG_CHAN_BLOCK;
         }
         else if (max_sample > sample) // assumes strictly monotonic
         {
            if(Debug){cout << " +++ PEAK at byte: " << intv.Peak << " block:" << intv.PeakBlock << " value: " << max_sample << endl;}
            cygfile->InStrm.seekg(intv.Peak); // done, position on peak
            found = true;
         }
      }
      else if (sample <= 0)
      {
         intv.Peak = mono_pos = 0;  // start over on negative voltage
      }
      else if (sample > 0 && intv.Peak == 0) // found a positive
      {
         intv.Peak = blk;
         mono_pos = 0;
         max_sample = sample;
      }
      else if (sample > 0 && sample > max_sample) // rising signal
      {
         max_sample = sample;
         ++mono_pos;
         if (mono_pos == 10) // looks like we're in a good pulse, look for peak
            find_peak = true;
      }
      else if (sample > 0 && sample < max_sample) // falling sig, keep looking
      {
         max_sample = sample;
         intv.Peak = blk;
         mono_pos = 0;
      }
   }
   return true;
}


/* Open input and output files. Any error is fatal. 
   Return total bytes in all input files.
*/
void open_files()
{
   FilesIter iter;
   struct stat info;

   for (iter = FList.begin(); iter != FList.end(); ++iter)
   {
      if (iter->InName.length())
      {
         iter->InStrm.open(iter->InName.c_str(),ios::binary);
         if (!iter->InStrm.is_open())
         {
            cout << "FATAL ERROR: Could not open " << iter->InName << endl << "exiting. . ." << endl;
            exit(1);
         }
         iter->OutStrm.open(iter->OutName.c_str(),ios::binary | ios::trunc);
         if (!iter->OutStrm.is_open())
         {
            cout << "FATAL ERROR: Could not open output file " << iter->OutName << endl << "Exiting. . ." << endl;
            iter->InStrm.close();
            exit(1);
         }
          // size of files
         FILE* fd = fopen(iter->InName.c_str(),"r");
         fstat(fileno(fd),&info);
         totalBytes += info.st_size;  // bytes in all files
         fclose(fd);
      }
   }
}


// Read headers of file and copy to output file
static void copy_header(FilesIter& iter)
{
   char buff[CYG_BUFF_SIZ];
   iter->InStrm.read(buff,sizeof(buff));
   iter->OutStrm.write(buff,sizeof(buff));
}

// Copy every sample up to the first peak timing pulse
// Leave the file position at that peak sample location
// and return the interval values.
static Interval copy_to_first(FilesIter& iter)
{
   Interval first;
   off_t start, count;
   char val[1];

   start = iter->InStrm.tellg();
   next_pulse(iter, first);
   iter->InStrm.seekg(start);
   while (start++ < first.Peak)
   {
      iter->InStrm.read(val,sizeof(val));
      iter->OutStrm.write(val,sizeof(val));
      ++count;
   }
   return first;
}


// Setups done, upsample nominal 24Khz file to 25Khz file
static void adjust_file(FilesIter& iter)
{
   Interval start, next;
   char val[1];
   off_t tick_count;
   double d_src, d_tar;
   double interpol;
   double last24, last25;
   daq25Iter outIter25;
   daq24Iter inIter24;
   DataBuff outBuff;
   DataBuff sampBytes;
   Sample left,right, result;
   int err;
   daq24 tick24;
   daq25 tick25;
   int num_samp = 0;
   off_t block = 1;

   copy_header(iter);
   start = copy_to_first(iter);
   feedback += start.Peak;

    // first sample is special, no interpolation
   next_pulse(iter, next); 
   iter->InStrm.seekg(start.Peak); // back up so we read this
   tick_count = next.PeakBlock - start.PeakBlock;
   err = tick_count - IDEAL_CYG;
   if (Debug) cout << "err: " << err << endl;
   tick24 = linspace<double> (0.0, (IDEAL_CYG + err)/(CYG_RATE+5*err), IDEAL_CYG+err); 
   tick25 = linspace<double> (0.0, DAQ_INTV/DAQ_RATE, DAQ_INTV); 
   iter->InStrm.read(sampBytes.data(),sampBytes.size());
   feedback += sampBytes.size();
   ++throttle;
   right.toShort(sampBytes);
   for (int chan = 0; chan < CYG_CHAN_BLOCK; ++chan)
      outBuff[chan] = sampBytes[chan];
   iter->OutStrm.write(outBuff.data(),outBuff.size());
   if (Debug) cout << "dest idx 0 is not between anything, it starts the sequence" << endl;
   left = right;
   int how_many24 = 1;
   int how_many25 = 1;
   outIter25 = tick25.begin() + 1; // used index 0 above
   inIter24 = tick24.begin();      // first interpolation is between [0] and [1]
   for ( ; inIter24 != tick24.end()-1; ++inIter24)
   {
      iter->InStrm.read(sampBytes.data(),sampBytes.size());
      feedback += sampBytes.size();
      ++throttle;
      ++how_many24;
      right.toShort(sampBytes);
      if (*outIter25 >= *inIter24 && *outIter25 <= *(inIter24+1))
      {
         ++num_samp;
         if (*(outIter25+1) >= *inIter24 && *(outIter25+1) <= *(inIter24+1))
            ++num_samp;  // 2 25KHz samples in on 24Khz interval
      }
      else
      {
         cout << "What? Should not be here #1." << endl;
      }
      while (num_samp)
      {
          // upscale into outbuff
         d_src = *(inIter24+1) - *inIter24;
         d_tar = *outIter25 - *inIter24;
         interpol = d_tar / d_src;
         if (Debug) 
         {
            cout << "dest idx " << outIter25 - tick25.begin() 
                 << " is between " 
                 << inIter24-tick24.begin()
                 << " and "
                 << inIter24+1-tick24.begin() 
                 << " Time scale is " << interpol << endl;
         }
         for (int chan = 0; chan < CYG_CHANS; ++chan)
         {
            result[chan] = (left[chan] + interpol * (right[chan]-left[chan]));
            if (Debug) 
            {
               if (chan == 15)
               {
                  if (left[chan] > 20)
                     cout << "Orig is: " << left[chan] << " New is: " <<  left[chan] + interpol * (right[chan]-left[chan]) << endl;
               }
            }
         }
         outBuff = result.toBytes();
         iter->OutStrm.write(outBuff.data(),outBuff.size());
         ++how_many25;
         ++outIter25;
         --num_samp;
         if (Debug){ if (num_samp) // we are making a new out pt betwee two in points
                     cout << "New one" << endl;}
      }
      left =  right;
   }
   if (Debug) cout << " wrote from peak " << start.PeakBlock << " to " << next.PeakBlock << endl;
   start = next;

   if (Debug)
   {
      cout << "Read " << how_many24 << " sample blocks" << endl;
      cout << "Wrote " << how_many25 << " sample blocks" << endl;
   }

   // now do rest of file
   if (Debug) cout << endl << "*** DO REST *** " << endl;
   while (next_pulse(iter, next)) 
   {
      num_samp = 0;
      how_many24 = how_many25 = 0;
      tick_count = next.PeakBlock - start.PeakBlock;
      if (tick_count > IDEAL_CYG+10) // kind of arbitrary
         cout << "Two timing pulses in this file are to far apart" << endl
         << "Has this file been processed by cyg_fixup?" << endl
         << "Results are probably incorrect." << endl;

         // move along the time axis
      err = tick_count - IDEAL_CYG;
      if (Debug) cout << "err: " << err << endl;
      last25 = block * DAQ_INTV_TIME; // next 25khz set of ticks
      last24 = last25;
      ++block;
     tick25 = linspace<double> (last25+DAQ_RATE_SEC, last25+DAQ_INTV/DAQ_RATE, DAQ_INTV); 
     tick24 = linspace<double> (last24, last24+(IDEAL_CYG + err)/(CYG_RATE+5*err), IDEAL_CYG+err); 
       if (Debug) cout << "l24: " << last24 << " l25: " << last25 << endl;
      // back up to start pulse
      iter->InStrm.seekg(start.Peak);
      outIter25 = tick25.begin();
      inIter24 = tick24.begin();
      for (  ; inIter24 != tick24.end()-1; ++inIter24)
      {
         if (outIter25 == tick25.end())
         {
            cout << "25 end" << endl;
            cout << "24 pos: " << tick24.end() - inIter24 << endl;
         }
         iter->InStrm.read(sampBytes.data(),sampBytes.size());
         right.toShort(sampBytes);
         ++how_many24;
         feedback += sampBytes.size();
         ++throttle;
         if (*outIter25 >= *inIter24 && *outIter25 <= *(inIter24+1))
         {
            ++num_samp;
            if (*(outIter25+1) >= *inIter24 && *(outIter25+1) <= *(inIter24+1))
               ++num_samp;
         }
         else
         {
            cout << "What? Should not be here #2." << endl;
         }
         while (num_samp)
         {
               // upscale into outbuff
            d_src = *(inIter24+1) - *inIter24;
            d_tar = *outIter25 - *inIter24;
            interpol = d_tar / d_src;
            if (Debug) {cout << "dest idx " << outIter25 - tick25.begin() 
                 << " is between " 
                 << inIter24-tick24.begin()
                 << " and "
                 << inIter24+1-tick24.begin() 
                 << " Time scale is " << interpol << endl; }
            for (int chan = 0; chan < CYG_CHANS; ++chan)
            {
               result[chan] = (left[chan] + interpol * (right[chan]-left[chan]));
               if (Debug)
               {
                  if (chan == 15)
                  {
                     if (left[chan] > 20) // just timing pulse part
                        cout << "Orig is: " << left[chan] << " New is: " <<  left[chan] + interpol * (right[chan]-left[chan]) << endl;
                  }
               }
            }
            outBuff = result.toBytes();
            iter->OutStrm.write(outBuff.data(),outBuff.size());
            ++outIter25;
            --num_samp;
            ++how_many25;
            if (Debug) {if (num_samp) // we are making a new out pt betwee two in points
               cout << "New one" << endl;}
         }
         left =  right;
      }
      if (Debug) cout << " wrote from peak " << start.PeakBlock << " to " << next.PeakBlock << endl;
      start = next;
      if (outIter25 != tick25.end())
         cout << "Unexpectedly did not use all target slots" << endl;
      if (inIter24 != tick24.end()-1)
         cout << "Unexpectedly did not use all source slots" << endl;
      if (Debug)
      {
         cout << "Read " << how_many24 << " sample blocks" << endl;
         cout << "Wrote " << how_many25 << " sample blocks" << endl;
      }
      if (throttle > 1024)
      {
         printf("\r  %3.0f%%",((double)feedback/totalBytes)*100.0);
         fflush(stdout);
         throttle = 0;
      }
   }

   // Ran out of pulses. Copy rest of file without upscaling.
   iter->InStrm.clear(); // no longer at eof
   iter->InStrm.seekg(start.Peak);
   while (!iter->InStrm.eof())
   {
      iter->InStrm.read(val,sizeof(val));
      if (!iter->InStrm.eof())
      {
         iter->OutStrm.write(val,sizeof(val));
         ++feedback;
      }
   }
   printf("\r  %3.0f%%",((double)feedback/totalBytes)*100.0);
   fflush(stdout);
   iter->InStrm.close();
   iter->OutStrm.close();
}


static void adjust_timing()
{
   FilesIter iter;
   open_files();
   for (iter = FList.begin(); iter != FList.end(); ++iter)
   {
      if (iter->InName.length())
      {
         cout << endl << "Processing " << iter->InName << endl;
         adjust_file(iter);
      }
   }
}


static void get_in(const string& prompt1, const string& prompt2, int f_idx)
{
   string tmp_num;
   string basename;

   cout << prompt1;
   getline(cin,FList[f_idx].InName);
   if (FList[f_idx].InName.length())
   {
      
      string::size_type const off(FList[f_idx].InName.find_last_of('.'));
      FList[f_idx].OutName = FList[f_idx].InName.substr(0, off) + "_25KHz" + ".dd";
      cout << prompt2;
      getline(cin,tmp_num);
      if (tmp_num.length())
      {
         try
         {
            FList[f_idx].SyncChan = stoi(tmp_num);
         }
         catch (const std::invalid_argument& ia) 
         {
            cerr << "FATAL ERROR: Not a number in " << ia.what() << ", exiting. . ." << endl;
            exit(1);
         }
      }
      else
      {
         cerr << "FATAL ERROR: Timing pulse channel missing,  exiting. . ." << endl;
         exit(1);
      }
   }
}

const string a_prompt1("Enter cygnus tape A input filename, ENTER for none: ");
const string a_prompt2("Enter cygnus tape A Timing Pulse channel: ");
const string b_prompt1("Enter cygnus tape B input filename, ENTER for none: ");
const string b_prompt2("Enter cygnus tape B Timing Pulse channel: ");
const string c_prompt1("Enter cygnus tape C input filename, ENTER for none: ");
const string c_prompt2("Enter cygnus tape C Timing Pulse channel: ");
const string d_prompt1("Enter cygnus tape D input filename, ENTER for none: ");
const string d_prompt2("Enter cygnus tape D Timing Pulse channel: ");

int main (int argc, char **argv)
{
   string tmp_num;

   cout << argv[0] << " Version: " << VERSION << endl;
   if (!parse_args(argc, argv))
      exit(1);

   if (!HaveArgs)
   {
      get_in(a_prompt1, a_prompt2, 0);
      get_in(b_prompt1, b_prompt2, 1);
      get_in(c_prompt1, c_prompt2, 2);
      get_in(d_prompt1, d_prompt2, 3);
   }
   for (int file = 0; file < MAX_TAPES; ++file)
      if (FList[file].InName.length())
         cout << TAPES[file] << ": Upsampling " << FList[file].InName
              << " to " << FList[file].OutName 
              << " sync chan: " << FList[file].SyncChan << endl;
      else
         cout << TAPES[file] << ": No file" << endl;
   adjust_timing();
   cout << endl << "DONE." << endl;
   return 0;
}
