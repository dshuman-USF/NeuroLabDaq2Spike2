
#ifndef _DAQ2SPIKE2_H
#define _DAQ2SPIKE2_H

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

const int wordsPerSamp = 66;
const int bytesPerSamp = wordsPerSamp * sizeof(short); // 2 words header, 64 words data
const int dataPerSamp = 64 * 2;  // 64 words data
// 64 disk blocks (32K) will hold 20 byte header, 16364 samples, no pad
const int blocksPerChan = 64;
const int sampsPerBlock = (blocksPerChan*DISKBLOCK - SONDBHEADSZ) / sizeof(short);
const int bytesPerBlock = (blocksPerChan*DISKBLOCK - SONDBHEADSZ);
const int daqChansPerFile = 64;
const int daqChans = 128;
// use same size blocks, even if last one only has 1 sample in it
const unsigned long stdBlkSize = (SONDBHEADSZ + (sampsPerBlock) * sizeof(TAdc)) / DISKBLOCK;
const int blocksAllChans = daqChans * stdBlkSize;


// a variation on TDataBlock in Sonint.h, hardwired for our purposes;
using DaqDataBlock = struct
{
    TDOF   predBlock;     /* Predecessor block in the file */
    TDOF   succBlock;     /* Following block in the file */
    TSTime startTime;     /* First time in the block */
    TSTime endTime;       /* Last time in the block */
    WORD   chanNumber;    /* Channel number+1 for the block */
    WORD   items;         /* Actual number of data items found */
    short TAdc[sampsPerBlock];    /* ADC data */
};

// LUT stuff from son32 son.c file
using TLUTID = struct     /*!< structure used to identfy a LUT on disk */
{
    uint32_t ulID;        /*!< set to 0xfffffffe to identify */
    int chan;             /*!< set to channel number or -1 if no more entries */
    uint32_t ulXSum;      /*!< simple checksum of table header and table values */
};                        /*!< Lookup table identifier block */

#define LUT_ID 0xfffffffe /*!< identifies a table */
#define LUT_MINBLOCKS 64  /*!< minimum blocks to write LUT */
#define LUT_MINSAVE 64    /*!< don't save if fewer lookups than this */


#endif
