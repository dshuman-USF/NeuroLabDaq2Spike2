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


/* This was written to investigate the structure of the Spike2 .smr file.
   Modified slightly after we got the open source CED library. Was a 32-bit
   app, now a 64-bit app. CED altered some code so the difference between long
   and int was handled correctly.
*/
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <vector>

#include "sonintl.h"
#include "sonpriv.h"
#include "local_daq2spike2.h"

using namespace std;

using chanInfo = vector<TChannel>;

int main(int argc, char** argv)
{
   TFileHead  header;
   FILE *fd;

   printf("Try to open %d %s\n",argc,argv[1]);
   fd = fopen(argv[1],"rb");
   if (fd == 0)
   {
      printf("Could not open %s\n",argv[1]);
      perror("Error:");
      exit(1);
   }
   printf("Header is %lu bytes\n",sizeof(header));
   printf("Okay, read it\n\n");
   fread (&header,1,sizeof(header),fd);

    /* filing system revision level */
   printf("File version: %hd\n", header.systemID);
    /* space for "(C) CED 87" */
   printf("%.*s\n", LENCOPYRIGHT, header.copyright); 
    /* optional application identifier */
   printf("app id: %.*s\n", (int)sizeof(TSONCreator),header.creator.acID);
    /* microsecs per time unit */
   printf("usecs per time unit: %hd\n", header.usPerTime);
    /* time scale factor */
   printf("time scale factor: %lf\n", header.dTimeBase);
   printf("Sample rate: %8.2lf Hz\n",(1.0 / (header.usPerTime*header.dTimeBase)));
    /* time units per ADC interrupt */
   printf("time units per adc interrupt: %hd\n", header.timePerADC);
    /* condition of the file */
   printf("file condition: %hd\n", header.fileState);
    /* offset to first data block */
   printf("offset to first data block: %d\n", header.firstData);
    /* maximum number of channels */
   printf("max # of channels: %hd\n", header.channels);
    /* memory size to hold chans */
   printf("chan size %hd\n", header.chanSize);
    /* No of bytes of extra data in file */
   printf("extra data: %hd\n", header.extraData);
    /* Not used on disk; bufferP in bytes */
   printf("buffer p %hu\n", header.bufferSz);
    /* either 0x0101 for Mac, or 0x00 for PC */
   printf("OS: %hd\n", header.osFormat);
    /* max time in the data file */
   printf("max time in file: %d\n", header.maxFTime);

    /* time that corresponds to tick 0 */ 
   printf("time: hund: %u  sec: %u  min:%u hour: %u  day: %u mon: %u %hd: year\n",
             header.timeDate.ucHun,
             header.timeDate.ucSec,
             header.timeDate.ucMin,
             header.timeDate.ucHour,
             header.timeDate.ucDay,
             header.timeDate.ucMon,
             header.timeDate.wYear);
    /* 0 if not aligned to 4, set bit 1 if aligned */
   printf("align flag (0 not aligned to 4, 1 if it is) %d\n", header.cAlignFlag);
      //    char pad0[3];                 /* align the next item to a 4 byte boundary */
   /* 0, or the TDOF to a saved look up table on disk */
   printf("Type Of Disk Lookup Table (0 means none) at offset: %d\n", header.LUTable);
     //    char pad[44];                 /* padding for the future */
    printf("comment: [%s]\n", header.fileComment->string);     /* what user thinks of it so far */

   chanInfo chanList;
   int chans, inuse = 0;
   TChannel onechan;
   for (chans = 0; chans < header.channels; ++chans)
   {
      fread(&onechan,1,sizeof(onechan),fd);
      {
         chanList.push_back(onechan);
         ++inuse;
      }
   }
   printf("Found %d channels in use\n", inuse);
   printf("curr file pos: %lu %f blocks, next free %f\n", ftell(fd), floor(ftell(fd)/DISKBLOCK*1.0), floor((ftell(fd)+DISKBLOCK)/(DISKBLOCK*1.0)));

   chans = 0;
   for (auto iter = chanList.begin(); iter != chanList.end(); ++iter, ++chans)
   {
      printf("\nChan %2d\n",chans);
      printf("delsize %d\n",iter->delSize);
      printf("nextDelBlock %d\n",iter->nextDelBlock);
      printf("%.*s\n",*iter->comment.string, iter->comment.string+1);
      printf("%.*s\n",*iter->title.string, iter->title.string+1);
      printf("Starts at block: %d Ends at: %d\n",iter->firstBlock,iter->lastBlock);
      printf("blocks: %u\n",(iter->blocks));
      printf("blocksMSW: %u\n",(iter->blocksMSW));
      printf("Total blocks: %u\n",(iter->blocks + (iter->blocksMSW << 16)));
      printf("Extra bytes attached to marker: %u\n",iter->nExtra);
      printf("preTrig: %u\n",iter->preTrig);
      printf("Ideal sample rate: %f\n",iter->idealRate);
      printf("Physical size of block: %u (%u blocks)\n", iter->phySz, iter->phySz/DISKBLOCK);
      printf("Max # of data items in block: %u\n", iter->maxData);
      printf("Physical channel: %u\n",iter->phyChan);
      printf("Max chan time: %d\n",iter->maxChanTime);
      printf("lchandvd: %d\n",iter->lChanDvd);
      printf("delsizemsb: %d\n",iter->delSizeMSB);
      printf("Data type: ");
      switch (iter->kind)
      {
         case 0: printf("Off"); break;
         case 1: 
            printf("16 bit ADC\n");
            printf("scale: %f  offset: %f\nunits: %.*s\nADC Mark Interleave %u\n",
                  iter->v.adc.scale, iter->v.adc.offset, *iter->v.adc.units.string,
                  iter->v.adc.units.string+1, iter->v.adc.divide);
            break;
         case 2: printf("Event Fall");break;
         case 3: printf("Event Rise");break;
         case 4: printf("Event Both ");break;
         case 5: printf("Marker");break;
         case 6: printf("ADC Marker");break;
         case 7: printf("Real Mark");break;
         case 8: printf("Text Mark");break;
         case 9: printf("Real Wave");break;
         default:printf("Missed a case");break;
      }
   }


   for (int chan = 0 ; chan < 128; ++ chan)
//   for (int chan = 0 ; chan < 2; ++ chan)
   {
      size_t dataSize;
      TDataBlock data;
      bzero(&data,sizeof(data));
      printf("sizeof data block %lu\n",sizeof(data));
      off_t block = 0;
      off_t startblock = chanList[chan].firstBlock * DISKBLOCK; 
      off64_t nextblock = 0;
      long blkcount = 1;
      fseek(fd, startblock, SEEK_SET);
      unsigned int rread;
      if (chanList[chan].kind == 0)
      {
         dataSize = sizeof(TAdc);
         printf("Chan %d is 16 bit ADC, %ld bytes\n",chan,dataSize);
      }
      else if (chanList[chan].kind == 3)
      {
         dataSize = sizeof(TSTime);
         printf("Chan %d is Event Rise, %ld bytes\n",chan,dataSize);
      }
      else
         dataSize = 0;  // don't know what this is


      while (true)
      {
        // The data block may not be full, read header to see
        // how much more we need to read
         printf("Block: %ld\n", blkcount++);
         block = ftell(fd);
         bzero(&data,sizeof(data));
         rread = fread(&data,1,SONDBHEADSZ,fd);
         if (rread != SONDBHEADSZ)
            printf("oops!\n");
         printf("Chan #: %u\n",data.chanNumber);
         printf("curr file pos: %lu block: %f\n", block,  block/DISKBLOCK*1.0);
         printf("Pred block: %d\n",data.predBlock);
         printf("Next block: %d\n",data.succBlock);
         printf("Start time: %d\n",data.startTime);
         printf("End time: %d\n",data.endTime);
         printf("Samples: %u\n",data.items);
          // now read data 
         rread = fread(&data.data,1,data.items*dataSize ,fd);
         if (rread < data.items*dataSize)
             printf("Unexpectedly hit EOF\n");
         if (data.succBlock == -1)
            break;
         nextblock = (off64_t) data.succBlock * DISKBLOCK;
         int res = fseek(fd,nextblock,SEEK_SET);
         if (res == -1)
            printf("Seek error\n");
      }
   }

   // LUT?
   short buff[10000];
   TLookup table[8000];
   TSonLUTHead head;
   TLUTID h_id;
   unsigned long tabsize;
   unsigned int loop;
   printf("%lu %lu \n", sizeof(TLookup),sizeof(TSonLUTHead));
   printf("LUT Off %d\n",header.LUTable);
   printf("DISKBLOCK %d\n",DISKBLOCK);
   printf("times %ld\n",DISKBLOCK * (off64_t) header.LUTable);
   long off = (long) header.LUTable * DISKBLOCK;
//   off64_t off = (off64_t) header.LUTable * DISKBLOCK;
   printf("lut off %ld\n",off);
   if (header.LUTable)
   {
      int res = fseek(fd, off, SEEK_SET);
      if (res >= 0) // sometimes there is a LUT offset, but it is at EOF, no LUT
      {   
         printf("seek offset is %ld\n",off);
         printf("seek res is %d\n",res);
         printf("curr file pos: %lu block: %f\n", ftell(fd), ftell(fd)/DISKBLOCK*1.0);
         bool done = false;
         while (!done)
         {
            bzero(&h_id,sizeof(h_id));
            fread(&h_id,1,sizeof(h_id),fd);
            if (h_id.chan == -1)
               break;
            printf("0x%x\n",h_id.ulID);
            bzero(&head,sizeof(head));
            fread(&head,1,sizeof(head),fd);
            tabsize = head.nUsed;
            bzero(&table,sizeof(table));
            fread(&table,1,tabsize*(sizeof(TLookup)),fd);
            uint32_t chksum = 0;
            uint32_t *ptr;
            ptr = (uint32_t *) &head;
            for (loop = 0; loop < sizeof(TSonLUTHead)/sizeof(uint32_t); ++loop)
            {
               printf("%d %d\n",loop,ptr[loop]);
               chksum += ptr[loop];
            }
            ptr = (uint32_t *) &table;
            for (loop = 0; loop < tabsize; ++loop)
            {
               printf("%d %d\n",loop,ptr[loop]);
               chksum += ptr[loop];
            }
            printf("cksum in file:\n%u\n%u\n",h_id.ulXSum, chksum);
         }
         bzero(&buff,sizeof(buff));
         fseek(fd, off, SEEK_SET);
         fread(&buff,1,sizeof(buff),fd);
      }
      else
         printf("No LUT, just EOF\n");
   }
   fclose(fd);
   return 0;
}
