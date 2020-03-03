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
   Read cygnus digital tape file and print header info
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

const int CYG_HEADER = 128;
const int MAX_TAPES = 4;


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
using FList = array <OneFile,MAX_TAPES>;

// globals
FList Files;
bool HaveArgs = false;
static void usage(char * name)
{
   printf(
"\nUsage: %s "\
"[-a A_Tape_filename] "\
"[-b B_Tape_filename] "\
"[-c C_Tape_filename] "\
"[-d D_Tape_filename] "\
"\n"\
"Read one or more cygnus tapes and print time and date from header.\n"\
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
                                   { 0,0,0,0} };
   int cmd;
   bool ret = true;
   string arg;
   vector <string> tokens;

   auto tokenize = [&](FILE_SLOT idx) {tokens.clear(); stringstream strm(arg);string str; 
                                       while(getline(strm,str,',')) tokens.push_back(str);
                                       Files[idx].name = tokens[0]; };

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

static void get_in(const string& prompt1, int f_idx)
{
   string tmp_num;

   cout << prompt1;
   getline(cin,Files[f_idx].name);
}

const string a_prompt1("Enter cygnus tape A input filename, ENTER for none: ");
const string b_prompt1("Enter cygnus tape B input filename, ENTER for none: ");
const string c_prompt1("Enter cygnus tape C input filename, ENTER for none: ");
const string d_prompt1("Enter cygnus tape D input filename, ENTER for none: ");
const string units("ABCD");

int main (int argc, char **argv)
{
   string tmp_num;
   int file;
   int year; 

   cout << argv[0] << "Version: " << VERSION << endl;
   if (!parse_args(argc, argv))
      exit(1);

   if (!HaveArgs)
   {
      get_in(a_prompt1, 0);
      get_in(b_prompt1, 1);
      get_in(c_prompt1, 2);
      get_in(d_prompt1, 3);
   }

   for (file = 0; file < MAX_TAPES; ++file)
   {
      if (Files[file].name.length())
      {
         Files[file].fstrm.open(Files[file].name.c_str(),ios::binary);
         if (!Files[file].fstrm.is_open())
         {
            cout << "Could not open " << Files[file].name << endl ;
            continue;
         }
         Files[file].fstrm.read(reinterpret_cast<char*>(&Files[file].header),sizeof(OneFile::header));
         year = (int) Files[file].header.bcd_date[1] * 10 + 
                (int) Files[file].header.bcd_date[0];
         if (year > 19)  // only 2 digits of year, which century?
            cout << "19";
         else
            cout << "20";
         cout << (int) Files[file].header.bcd_date[1];
         cout << (int) Files[file].header.bcd_date[0] << "-";

         cout << (int) Files[file].header.bcd_date[5]; 
         cout << (int) Files[file].header.bcd_date[4] << "-";
         cout << (int) Files[file].header.bcd_date[3];
         cout << (int) Files[file].header.bcd_date[2] << " ";

         cout << (int) Files[file].header.bcd_time[5]; 
         cout << (int) Files[file].header.bcd_time[4] << ":";
         cout << (int) Files[file].header.bcd_time[3];
         cout << (int) Files[file].header.bcd_time[2] << ":";
         cout << (int) Files[file].header.bcd_time[1];
         cout << (int) Files[file].header.bcd_time[0] << ":00" << endl;
       }
   }

  return 0;
}
