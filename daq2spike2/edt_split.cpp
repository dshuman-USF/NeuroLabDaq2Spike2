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
#include <string.h>

using namespace std;
using analogList = map<int, ofstream*>;
using analogListIter = analogList::iterator;

// globals
string inName;
string outName;
string baseName;
bool isEdt;
analogList aChans;

static void usage()
{
   cout << " Program to split the channels in a .bdt or .edt file and put the "
        << "spike firings into a single file and each analog channel into a "
        << "separate file.  The input name will be used as the base name "
        << "for the output files." << endl <<"Version " << VERSION << endl
        << "Usage: edt_split -f <filename.edt | filename.bdt>"
        << endl << "For example: "
        << endl << endl << " edt_split -f 2014-06-24_001.edt" << endl
        << endl << "This must be run from the directory containing the edt/bdt files."
        << endl;
}

static int parse_args(int argc, char *argv[])
{
   int ret = 1;
   int cmd;
   static struct option opts[] =
   {
      {"f", required_argument, NULL, 'f'},
      {"h", no_argument, NULL, 'h'},
      { 0,0,0,0}
   };
   while ((cmd = getopt_long_only(argc, argv, "", opts, NULL )) != -1)
   {
      switch (cmd)
      {
         case 'f':
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
            usage();
            ret = false;
           break;
      }
   }
   if (!ret)
   {
      usage();
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
void splitFile()
{
   unsigned int id;
   string header1, header2, line, exten;
   analogListIter a_iter;

   ifstream in_file(inName.c_str());
   if (!in_file.is_open())
   {
      cout << "Could not open " << inName << endl << "Exiting. . ." << endl;
      exit(1);
   }

   getline(in_file,header1);
   getline(in_file,header2);

   if (header1.find("   11") == 0)
   {
      cout << "bdt file detected" << endl;
      exten = ".bdt";
      isEdt = false;
   }
   else if (header1.find("   33") == 0)
   {
      cout << "edt file detected" << endl;
      exten = ".edt";
      isEdt = true;
   }
   else
   {
      cerr << "This is not a bdt or edt file, exiting. . ." << endl;
      exit(1);
   }
   cout << "Reading " << inName << " (this may take a while)" << endl;
   string s_name = baseName + "_spk" + exten;
   ofstream spk_file(s_name);
   spk_file << header1 << endl;
   spk_file << header2 << endl;
   while (getline(in_file,line))
   {
      id = atoi(line.substr(0,5).c_str());
      if (id < 4096 && id != 0)  // spike chan
      {
         spk_file  << line << endl;
      }
      else if (id >= 4096)  // analog chan
      {
         id /= 4096;
         if ((a_iter = aChans.find(id)) == aChans.end())
         {
            string a_name = baseName + "_an" + to_string(id) + exten;
            a_iter = aChans.insert(make_pair(id, new ofstream(a_name))).first;
            *(a_iter->second) << header1 << endl;
            *(a_iter->second) << header2 << endl;
         }
         *(a_iter->second) << line << endl;
      }
   }
   in_file.close();
   for (auto iter : aChans)
      iter.second->close();
   spk_file.close();
}

int main(int argc, char*argv[])
{
   string inFile, outBaseName;

   parse_args(argc,argv);
   if (argc < 3)
   {
      usage();
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
   splitFile();

   exit(0);
}



