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



/*
   Read cygnus digital tape file(s) and make .daq file(s).
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

#include <map>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <array>
#include <vector>
#include <algorithm> 

using namespace std;

const int CHANS_PER_FILE = 64;
const int DAQ_BUFF_SIZ = 66;
const int DAQ_DATA_SIZ = 64;
const int CYG_BUFF_SIZ = 65024;          // # bytes in a tape sector
const int CYG_SAMP_SIZ = CYG_BUFF_SIZ/2; // 2 bytes per sample
const int CYG_HEADER = 128;
const int CYG_CHANS = 16;
const off_t CYG_CHAN_BLOCK = CYG_CHANS * sizeof(short);
const int MAX_TAPES = 4;

string DAQ_EXT(".daq");

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
      OneFile() {memset(&header,0,sizeof(header));}
      string name = "";
      ifstream fstrm;
      cygheader header;
      int  sync_chan = 0;
      off_t peak = numeric_limits<off_t>::max();
};
enum FILE_SLOT {A,B,C,D};

using chanMap = map<int,int>;
using InBuff = array <char,CYG_CHAN_BLOCK>;
using InBuffIter = array <char,CYG_CHAN_BLOCK>::iterator;
using OutBuff =  array <unsigned short,DAQ_BUFF_SIZ>;
using FList = array <OneFile,MAX_TAPES>;

// globals
FList Files;
string OutName;
string OutTag("_from_cyg_");
bool OverWrite = false;
bool HaveRaw = false;
bool HaveArgs = false;
bool Debug = false;


// this is the index of the sequential words in a sample block 
chanMap cmap = {
   {1, 1},
   {2, 5},
   {3, 9},
   {4, 13},
   {5, 3},
   {6, 7},
   {7, 11},
   {8, 15},
   {9, 2},
   {10, 6},
   {11, 10},
   {12, 14},
   {13, 4},
   {14, 8},
   {15, 12},
   {16, 16}
};

// This is the index into the data given chan#.
// E.g. chan 8 is at index 14, or 13 for zero-based indexing.
chanMap rev_cmap = {
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

static void usage(char * name)
{

   printf (
"\nUsage: %s "\
"[-a A_Tape_filename,timing_pulse_chan] "\
"[-b B_Tape_filename,timing_pulse_chan] "\
"[-c C_Tape_filename,timing_pulse_chan] "\
"[-d D_Tape_filename,timing_pulse_chan] "\
"-o outfile_name "\
"\n"\
"Read one or more corrected and upscaled cygnus tapes from an experiment and "\
"make a .daq file.\n"\
"The timing pulse channels are used to align the tapes so the first timing pulse\n"\
"occurs at the same sample time.\nNote: Use commas with no spaces\n\n"\
"This can be used in a command line prompt mode, or using command line arguments.\n" \
"\nIf there are no arguments, the program will prompt for input.\n" \
"This must be run from the directory containing the cygnus files.\n"\
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
                                   {"o", required_argument, NULL, 'o'},
                                   {"h", no_argument, NULL, 'h'},
                                   {"f", no_argument, NULL, 'f'},
                                   {"D", no_argument, NULL, 'D'},
                                   { 0,0,0,0} };
   int cmd;
   bool ret = true;
   opterr = 0;
   string arg;
   vector <string> tokens;

   auto tokenize = [&](FILE_SLOT idx) {tokens.clear(); stringstream strm(arg);string str; 
                                       while(getline(strm,str,',')) tokens.push_back(str);
                                       Files[idx].name = tokens[0];
                                       Files[idx].sync_chan = strtol(tokens[1].c_str(),nullptr,10);};

   while ((cmd = getopt_long_only(argc, argv, "", opts, NULL )) != -1)
   {
      switch (cmd)
      {
         case 'a':
               arg = optarg;
               tokenize(A);
               HaveArgs = true;
               break;
         case 'b':
               arg = optarg;
               tokenize(B);
               HaveArgs = true;
               break;
         case 'c':
               arg = optarg;
               tokenize(C);
               HaveArgs = true;
               break;
         case 'd':
               arg = optarg;
               tokenize(D);
               HaveArgs = true;
               break;
         case 'o':
               OutName = optarg;
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
   if (HaveArgs && OutName.length() == 0)
   {
      cout << "FATAL: If using command line arguments, the output name is required." << endl;
      usage(argv[0]);
      ret = false;
   }
   return ret;
}


// read headers of open files. Leave file pos at 0 on exit
static void read_headers()
{
   for (int idx = 0; idx < MAX_TAPES; ++idx)
   {
      if (Files[idx].fstrm.is_open())
      {
         Files[idx].fstrm.read(reinterpret_cast<char*>(&Files[idx].header),sizeof(OneFile::header));
         Files[idx].fstrm.seekg(0);
      }
   }
}


/* For each open file, skip header then look at timing channel for each file.
   Examining a couple of recordings, the values on the timing pulse channel
   starts out as a stream of values such as -12, -9, -11, -13. 
   This is the "zero" value.

   Examining the recordings, we see that the first timing pulse if of poor
   quality, it barely looks like a triangle at all. Plus, it bounces + and -
   for the first few samples and is not very wide.  The second pulse is a good
   triangle with no bounce, so we use it as the origin.  We expect to see a
   monotonically increasing set of at least 10 + values once the pulse starts.

   Our quantum is sample blocks. Each block is 16 samples, as 16 shorts. Return
   the sample block number the peak is in.
   Leave each file positioned at zero on exit.
*/
static void sync_timings()
{
   int file;
   bool found, find_peak;
   int chan;
   off_t seq_pos = 0;
   unsigned char sample_l, sample_h;
   short sample, max_sample;
   off_t curr_start = 0;
   InBuff inbuff;
   InBuffIter iter;
 
     // There is a header from the tape and a pad
     // of zero that ddrescue adds to form 1 tape sector. Skip this.
   for (file=0; file < MAX_TAPES; ++file)
      if (Files[file].fstrm.is_open())
         Files[file].fstrm.seekg(CYG_BUFF_SIZ);

   for (file=0; file < MAX_TAPES; ++file)
   {
      if (!Files[file].fstrm.is_open())
         continue;
      found = find_peak = false;
      chan = rev_cmap[Files[file].sync_chan]-1;
      cout << "Searching for timing pulse in " << Files[file].name << endl;
      if (Debug) cout << "clock chan: " <<  Files[file].sync_chan << "  index in stream: " << chan << endl;
      max_sample = 0;
      off_t blk = 0;
      while(!found)
      {
         blk = (Files[file].fstrm.tellg()) / CYG_CHAN_BLOCK;
         Files[file].fstrm.read(reinterpret_cast<char *>(inbuff.data()),CYG_CHAN_BLOCK);
         if (Files[file].fstrm.eof())  // missing marker?
         {
            cout << "File " << Files[file].name << " appears to not have a timing pulse or the channel number is wrong." << endl;
            cout << "Unable to proceed." <<endl;
            cout << "Exiting program. . ." << endl;
            exit(1);
         }
         iter = inbuff.begin();
         iter += chan*2;
         sample_l = *iter++;
         sample_h = *iter;
         sample = sample_l + 256 * sample_h;
         
         if (false)   // debug
         {
            if (sample < 0)
               cout << blk << ":  " << sample << endl << flush;
            else if (sample > 0)
               cout << "  " << blk << ": ++++ " << sample << endl << flush;
         }
         if (find_peak)  // are in pulse, find max
         {
            if (max_sample < sample)
               max_sample = sample;
            else if (max_sample > sample)
            {
               //blk -= 1;                 // went one sample too far
               Files[file].peak = blk;  // peak in this sample blk #
               Files[file].fstrm.seekg(0);
               found = true;
               cout << "Found peak for " << Files[file].name << " at sample block " << blk << endl;
            }
         }
         else if (sample <= 0)
         {
            curr_start = seq_pos = 0;
         }
         else if (sample > 0 && curr_start == 0)
         {
            curr_start = Files[file].fstrm.tellg() - CYG_CHAN_BLOCK;
            seq_pos = 0;
            max_sample = sample;
         }
         else if (sample > 0 && sample > max_sample) // rising signal
         {
            max_sample = sample;
            ++seq_pos;
            if (seq_pos == 10) // looks like a good pulse, back up, then find peak
            {
               find_peak = true;
               Files[file].fstrm.seekg(curr_start); // back to start of pulse
               max_sample = 0;
            }
         }
         else if (sample > 0 && sample < max_sample) // falling sig
         {
            curr_start = 0;     // if here, we did not find minimum increasing samples
            max_sample = 0;
            seq_pos = 0;
         }
      }
   }
}

/* if we have this (in blocks):
   f1 0123456| <- peak in block 7
   f2 0123456789| <- peak in block 10
   Read as much of lead-in as we can
   start f1 here V
             f1  0123456|
   start f2 here V
           f2 0123456789|  10-7=3
   File with smallest offset, say, 7, start at byte 0
   file with larger, say 10 start at 3, so when we read peak block 7
   for f1, we'll be reading peak block 10 in f2 as the same sample, 
   so recordings are aligned.
*/
static void align_chans()
{
   off_t diffs[MAX_TAPES];
   off_t peak_off = numeric_limits<off_t>::max();
   int file;

   for (file=0; file < MAX_TAPES; ++file)  // find minimum peak offset
      if (Files[file].fstrm.is_open())
         if (Files[file].peak < peak_off)
            peak_off = Files[file].peak;

   for (file=0; file < MAX_TAPES; ++file)  // find minimum peak offset
      if (Files[file].fstrm.is_open())
         diffs[file] = (Files[file].peak - peak_off) * CYG_CHAN_BLOCK;

     // skip first block
   for (file=0; file < MAX_TAPES; ++file)
      if (Files[file].fstrm.is_open())
         Files[file].fstrm.seekg(CYG_BUFF_SIZ);

   for (file=0; file < MAX_TAPES; ++file)
      if (Files[file].fstrm.is_open())
         Files[file].fstrm.seekg(diffs[file], ios_base::cur);
}

      
/* What we are here for.  Read all of the chan files for the current
   section, 1-64 or 65-128 and combine them all back into a .daq file that
   looks like a recording.  
   The format is a set of blocks.  The first two words in the block are 0000 0000.
   The daq data format is offset binary,
   0xffff is max positive, 
   0x8000 is zero.
   0000 is max negative
*/
static bool create_daq()
{
   OutBuff outbuff;
   InBuff inbuff;
   InBuffIter in_iter;
   unsigned short *outptr;
   unsigned char sample_l, sample_h;
   short sample;
   off_t percent = 0;
   struct stat info;
   unsigned long long feedback = 0, throttle = 0;
   int chan, index, file, cyg_off;;
   bool read_more;
   short maxb = 0;
   short maxc = 0;

   auto newbuff = [&] {outbuff.fill(0x8000); outbuff[0] = outbuff[1] = 0;};

   for (file = 0; file < MAX_TAPES; ++file)
   {
      if (Files[file].name.length())
      {
         Files[file].fstrm.open(Files[file].name.c_str(),ios::binary);
         if (!Files[file].fstrm.is_open())
         {
            cout << "FATAL: Could not open " << Files[file].name << endl << "exiting. . ." << endl;
            exit(1);
         }
          // size of files
         FILE* fd = fopen(Files[file].name.c_str(),"r");
         fstat(fileno(fd),&info);
         percent += info.st_size;  // bytes in all files
         fclose(fd);
      }
   }

   ofstream out_file(OutName.c_str(),ios::binary);
   if (!out_file.is_open())
   {
      cout << "FATAL: Could not open output file " << OutName << endl << "Exiting. . ." << endl;
      exit(1);
   }

   read_headers();
   sync_timings();
   align_chans();
   for (file=0; file < MAX_TAPES; ++file)
      if (Files[file].fstrm.is_open())
         cout << "Starting file position for " << file << ": " << Files[file].fstrm.tellg() - (off_t) CYG_BUFF_SIZ << endl;


   off_t blk = 0;
   while (true)
   {
      // check to see if all files at eof
      read_more = false;
      for (int eofchk = 0; eofchk < MAX_TAPES; ++eofchk)
      {
          if (Files[eofchk].fstrm.is_open() && !Files[eofchk].fstrm.eof())
          {
             read_more = true;
             break;
          }
      }
      if (!read_more)
         break;

      cyg_off = 0;
      newbuff();
      outptr = outbuff.data() + 2; // skip markers
      ++blk;
      for (file = 0; file < MAX_TAPES; ++file, cyg_off += CYG_CHANS)
      {
         if (!Files[file].fstrm.is_open() || Files[file].fstrm.eof())
            continue;
         Files[file].fstrm.read(reinterpret_cast<char *>(inbuff.data()),CYG_CHAN_BLOCK);
         in_iter = inbuff.begin();
         feedback += CYG_CHAN_BLOCK;
         ++throttle;
         while (in_iter != inbuff.end())
         {
            for (chan = 0; chan < CYG_CHANS; ++chan)
            {
               sample_l = *in_iter++;
               sample_h = *in_iter++;
               sample = sample_l + 256 * sample_h;

               if (Debug)
               {
                  if (file == 1 && cmap[chan+1] == 16)
                  {
                     if (sample < 0)
                        cout << "B: file: " << file << " blk: " << blk << ":  " << sample << endl << flush;
                     else
                        cout << "  +++ B: file: " << file << " blk: " << blk << ":  " << sample << endl << flush;
                     if (sample > maxb)
                     {
                        maxb = sample;
                        cout << "B MAX: " << maxb  << endl;
                     }
                  }
                  else if (file == 2 && cmap[chan+1] == 16)
                  {
                     if (sample < 0)
                        cout << "C: file: " << file << " blk: " << blk << ":  " << sample << endl << flush;
                     else
                        cout << "  +++ C: file: " << file << " blk: " << blk << ":  " << sample << endl << flush;
                     if (sample > maxc)
                     {
                        maxc = sample;
                        cout << "C MAX: " << maxc << endl;
                     }
                  }
               }

               sample += 0x8000;
               if (sample == 0)  // illegal value, most negative value
                  sample =  1;   // make it the next most negative value
               index = cmap[chan+1] + cyg_off;
               if (Debug) cout << "file index: " << chan << " lookup: " << index << endl;
               *(outptr + index-1) = sample;
            }
         }
      }
      out_file.write(reinterpret_cast<char*>(outbuff.data()),sizeof(outbuff));
      if (throttle > 1024)
      {
         printf("\r  %3.0f%%",((double)feedback/percent)*100.0);
         fflush(stdout);
         throttle = 0;
      }
   }
   cout << endl;
   for (file = 0; file < MAX_TAPES; ++file)
      if (Files[file].fstrm.is_open())
         Files[file].fstrm.close();
   return true;
}



static void get_in(const string& prompt1, const string& prompt2, int f_idx)
{
   string tmp_num;

   cout << prompt1;
   getline(cin,Files[f_idx].name);
   if (Files[f_idx].name.length())
   {
      cout << prompt2;
      getline(cin,tmp_num);
      if (tmp_num.length())
      {
         try
         {
            Files[f_idx].sync_chan = stoi(tmp_num);
         }
         catch (const std::invalid_argument& ia) 
         {
            std::cerr << "FATAL: Not a number in " << ia.what() << ", exiting. . ." << endl;
            exit(1);
         }
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
const string units("ABCD");

int main (int argc, char **argv)
{
   string tmp_num;
   int  complain;

   cout << argv[0] << "Version: " << VERSION << endl;
   if (!parse_args(argc, argv))
      exit(1);

   if (!HaveArgs)
   {
      get_in(a_prompt1, a_prompt2, 0);
      get_in(b_prompt1, b_prompt2, 1);
      get_in(c_prompt1, c_prompt2, 2);
      get_in(d_prompt1, d_prompt2, 3);
      cout << "Enter output file name without .daq extension: " ;
      cin >> OutName;
   }
   for (int file = 0; file < MAX_TAPES; ++file)
      if (Files[file].name.length())
         cout << units[file] << ": " << Files[file].name 
              << " sync chan: " << Files[file].sync_chan << endl;
      else
         cout << units[file] << ": No file" << endl;
   OutName = OutName + OutTag + "1-64" + DAQ_EXT;
   cout << "Output file: " << OutName << endl;
   complain = !create_daq();
   if (complain)
      usage(argv[0]);

  return 1;
}
