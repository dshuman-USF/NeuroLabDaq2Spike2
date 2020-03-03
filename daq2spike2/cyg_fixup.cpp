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
   It appears that the "bad sectors" that ddrescue found on the cygnus tapes
   are not read errors of valid data sectors that have been corrupted. Instead,
   it appears that during the original recording, the cygnus machine detected a
   bad write and simply wrote the block to the next valid location on the tape.
   This program takes the bad-block offset(s) and number of sectors and createa
   file without the sector(s).

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
#include <iomanip> 

using namespace std;
const int CYG_BUFF_SIZ = 65024;          // # bytes in a tape sector


string InName;
string MapName;
string OutName;
string OutTag("_sector_fixed");
ifstream InStrm;
ifstream MapStrm;
ofstream OutStrm;
int NumSkipped = 0;

class Info_t
{
   public:
      Info_t(off_t off, off_t size) :offset(move(off)),bytes(move(size)){}
      off_t offset;
      off_t bytes;
};

vector <Info_t> BadBlocks;


bool Debug = false;

static void usage(char * name)
{

   printf (
"\nUsage: %s "\
"-f input_file_name\n"\
"A map file with the same base name is expected, such as b04.b1.map\n\n"\
"It appears that the 'bad sectors' that ddrescue found on the cygnus tapes\n"\
"are not read errors of valid data sectors that have been corrupted. Instead,\n"\
"it appears that during the original recording, the cygnus machine detected a\n"\
"bad write and simply wrote the block to the next valid location on the tape.\n"\
"This program uses the bad sector offsets in the .map file, and creates a new file\n"\
"that removes the so-called bad sectors.\n"\
"\n"\
,name);

}

static void parse_args(int argc, char *argv[])
{
   static struct option opts[] = { 
                                   {"f", required_argument, NULL, 'f'},
                                   {"h", no_argument, NULL, 'h'},
                                   {"d", no_argument, NULL, 'd'},
                                   { 0,0,0,0} };
   int cmd;
   while ((cmd = getopt_long_only(argc, argv, "", opts, NULL )) != -1)
   {
      switch (cmd)
      {
         case 'f':
               InName = optarg;
               break;
         case 'd':
               Debug = true;
               cout << "Debug turned on." << endl;
               break;
         case 'h':
         case '?':
         default:
            usage(argv[0]); 
            break;
      }
   }
}



/* Read and parse ddrescue mapfile
   Expect something like this:

# Mapfile. Created by GNU ddrescue version 1.23
# Command line: ddrescue -b 65024 -c 1 --skip-size=0 --no-scrape --no-trim -v -v -v 
#  -v -u /dev/nst0 b01.c1.2nd.ddrtry b01.c1.2nd.ddrtry.map
# Start time:   2019-05-07 14:04:54
# Current time: 2019-05-08 07:26:52
# Copying non-tried blocks... Pass 1 (forwards)
# current_pos  current_status  current_pass
0x115BE9F0800     ?               1
#      pos        size  status
0x00000000  0x00000080  +
0x00000080  0x0000FD80  -
0x0000FE00  0x0FD51600  +
0x0FD61400  0x0000FE00  -
0x0FD71200  0x11CB2200  +
0x21A23400  0x0000FE00  -
0x21A33200  0x098CC000  +
0x2B2FF200  0x0000FE00  -
0x2B30F000  0x86249A00  +
0xB1558A00  0x0000FE00  -
0xB1568800  0x1F57D200  +
0xD0AE5A00  0x0000FE00  -
0xD0AF5800  0x409EBE00  +
0x1114E1600  0x114AD50F200  -
0x115BE9F0800  0x7FFFFEEA4160F7FF  ?
*/
static void parse_map()
{
   string line;
   off_t position, size;
   string state;

   MapStrm.flags(ios_base::showbase | ios_base::hex);
   while (!MapStrm.eof())
   {
      getline(MapStrm,line);
      stringstream info(line);
      info << std::hex;
      if (info >> position >> size >> state)
         if (state == "-")
            BadBlocks.emplace_back(position,size);
   }
   if (Debug){for (auto iter = BadBlocks.begin(); iter != BadBlocks.end(); ++iter)
            cout << hex << iter->offset << " " << iter->bytes << endl;}
}


static void fixup()
{
   off_t curr;
   off_t len;
   streamsize num_read;
   off_t percent = 0;
   struct stat info;
   off_t feedback = 0;
   int throttle = 0;
   char buff[CYG_BUFF_SIZ];        // # bytes in a tape sector
   auto iter = BadBlocks.begin();  // The first sector on the tape is a 128 byte header.

   auto next_block=[&]{++iter; if (iter != BadBlocks.end()) {curr=iter->offset; len=iter->bytes;}else{curr = numeric_limits<off_t>::max();len = 0;}};

   FILE* fd = fopen(InName.c_str(),"r");
   fstat(fileno(fd),&info);
   percent = info.st_size;  // bytes in all files
   fclose(fd);

   // The first entry in the map file is the 128 byte header plus the missing
   // rest of the CYG_BUFF_SIZ sector that actually is not on the tape, but which
   // ddrescue returned (we can't specify variable sector sizes to ddrescue.)
   // Skip the first BadBlocks entry.
   next_block();
   while (!InStrm.eof())
   {
      if (InStrm.tellg() == curr)
      {
         if (!InStrm.eof())
         {
            while (len)
            {
               InStrm.read(buff,CYG_BUFF_SIZ);
               num_read = InStrm.gcount();
               if (!InStrm.eof())
               {
                  cout << "\nSkipping 0x" << CYG_BUFF_SIZ << " bytes starting at offset 0x"  << curr << endl;
                  len -= CYG_BUFF_SIZ;
                  ++NumSkipped;
                  if (Debug)
                  {
                     for (int i=0; i < CYG_BUFF_SIZ; ++i)
                        if (buff[i] != 0)
                        {
                           cout << "Unexpected non-zero value in skipped buffer" << endl;
                        }

                  }
               }
               else if (num_read > 0)
               {
                  OutStrm.write(buff,num_read); // Don't think this can happen, but 
                  break;                        // handle case.
               }
               else
                  break;
            }
            next_block();
         }
         else
            continue;
      }
      else
      {
          InStrm.read(buff,CYG_BUFF_SIZ);
          OutStrm.write(buff,CYG_BUFF_SIZ);
          feedback += CYG_BUFF_SIZ;
          ++throttle;
      }
      if (throttle > 1024)
      {
         printf("\r  %3.0f%%",((double)feedback/percent)*100.0);
         fflush(stdout);
         throttle = 0;
      }
   }
   printf("\r  %3.0f%%\n",((double)feedback/percent)*100.0);
   fflush(stdout);
}

int main (int argc, char **argv)
{

   cout << argv[0] << " Version: " << VERSION << endl;
   parse_args(argc, argv);
   if (InName.length() == 0)
   {
      cout << "FATAL: No input file name, exiting. . ." << endl;
      exit(1);
   }
   string::size_type const off(InName.find_last_of('.'));
   MapName = InName.substr(0, off) + ".map";
   OutName = InName.substr(0, off) + OutTag + ".dd";

   InStrm.open(InName.c_str(),ios::binary);
   if (!InStrm.is_open())
   {
      cout << "FATAL: Could not open input file " << InName << ", exiting. . ." << endl;
      exit (1);
   }
   MapStrm.open(MapName.c_str());
   if (!MapStrm.is_open())
   {
      cout << "FATAL: Could not open map file " << MapName << ", exiting. . ." << endl;
      exit (1);
   }
   OutStrm.open(OutName.c_str());
   if (!OutStrm.is_open())
   {
      cout << "FATAL: Could not open output file " << OutName << ", exiting. . ." << endl;
      exit (1);
   }
   parse_map();
   if (BadBlocks.size() == 0)
   {
      cout << "The file " << InName << " has no bad blocks. No fixup file is required" << endl;
         exit(0);
   }
   fixup();
   cout << "Created " << OutName << endl << "Skipped " << NumSkipped << " sectors" << endl;
  return 0;
}
