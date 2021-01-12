/******************************************************************************/
/**
@file		flash_minsort_sublist.c
@author		Ramon Lawrence
@brief		Flash Minsort designed to handle regions that are sorted sublists.
@copyright	Copyright 2020
			The University of British Columbia,
			IonDB Project Contributors (see AUTHORS.md)
@par Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

@par 1.Redistributions of source code must retain the above copyright notice,
	this list of conditions and the following disclaimer.

@par 2.Redistributions in binary form must reproduce the above copyright notice,
	this list of conditions and the following  disclaimer in the documentation
	and/or other materials provided with the distribution.

@par 3.Neither the name of the copyright holder nor the names of its contributors
	may be used to endorse or promote products derived from this software without
	specific prior written permission.

@par THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
	LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
	CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/
/******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include <time.h>
#include <math.h>

#include "flash_minsort_sublist.h"
#include "in_memory_sort.h"

/*
#define DEBUG 1
#define DEBUG_OUTPUT 1
#define DEBUG_READ 1
*/

void readPage_sublist(MinSortStateSublist *ms, int pageNum, external_sort_t *es, metrics_t *metric)
{
    file_iterator_state_t* is = (file_iterator_state_t*) ms->iteratorState;
    ION_FILE* fp = is->file;
    
    unsigned long int offset = ms->fileOffset;
    offset += pageNum*es->page_size;

    /* Seek to page location in file */
    fseek(fp, offset, SEEK_SET);

    /* Read page into start of buffer */   
    if (0 ==  fread(ms->buffer, es->page_size, 1, fp))
    {	printf("Failed to read block: %d Offset: %lu\n", pageNum, offset);        
    }
    
    metric->num_reads++;    
    ms->blocksRead++;     
    ms->lastBlockIdx = pageNum;   
    #ifdef DEBUG_READ
        printf("Reading block: %d Offset: %lu\n",pageNum, offset);        
        for (int k = 0; k < 31; k++)
        {
            test_record_t *buf = (void *)(ms->buffer + es->headerSize + k * es->record_size);
            printf("%d: Record: %d\n", k, buf->key);
        }
    #endif
}

inline int32_t getBlockId(MinSortStateSublist *ms)
{
    return *((int32_t *) (ms->buffer));    
}

inline int16_t getNumRecordsBlock(MinSortStateSublist *ms)
{
    return *((int16_t *) (ms->buffer + BLOCK_COUNT_OFFSET));    
}

/* Returns a value of a tuple given a record number in a block (that has been previously buffered) */
inline int32_t getValue_sublist(MinSortStateSublist* ms, int recordNum, external_sort_t *es)
{      
    test_record_t *buf = (test_record_t*) (ms->buffer+es->headerSize+recordNum*es->record_size);
    return buf->key;	    
}

void init_MinSort_sublist(MinSortStateSublist* ms, external_sort_t *es, metrics_t *metric)
{
     unsigned int i = 0, j = 0, val, regionIdx=0;    

    /* Operator statistics */    
    ms->blocksRead    = 0;
    ms->tuplesRead    = 0;
    ms->tuplesOut     = 0; 
    ms->bytesRead     = 0;
            
    ms->record_size       = es->record_size;    
    ms->numBlocks         = es->num_pages;

    // Ignoring small variable overhead
    // j = (ms->memoryAvailable - 2 * SORT_KEY_SIZE - INT_SIZE) / SORT_KEY_SIZE;  
    j = (ms->memoryAvailable) / SORT_KEY_SIZE;  
    printf("Memory overhead: %d  Max regions: %d\r\n",  2 * SORT_KEY_SIZE + INT_SIZE, j);                         
   
    // Memory allocation    
    // Allocate minimum index in separate memory space (block 0 is input buffer, block 1 is output buffer)
    // Block 1 as output buffer is not being counted in this case,
    // TODO: Challenge with this as if given only 2 buffers then have no room for minimum index. Creating separate allocated arrays for now.
    // Note: Assuming MinSort does need actually count the output buffer for its use as it can produce records in iterator format and does not need an output buffer for this.
    ms->min = malloc(ms->numRegions*sizeof(int));
    ms->offset = malloc(ms->numRegions*sizeof(long));     
    printf("Page size: %d, Memory size: %d Record size: %d, Number of records: %lu, Number of blocks: %d, Regions: %d\r\n", 
                   es->page_size, ms->memoryAvailable, ms->record_size, ms->num_records, ms->numBlocks, ms->numRegions);
                    
    for (i=0; i < ms->numRegions; i++)
        ms->min[i] = INT_MAX; 
    regionIdx = ms->numRegions-1;

    /* Scan data to populate the minimum in each region */
    /* Read from back of output file to get start of each sublist (region) */

    /* Read last block of sublist into buffer */
    long lastBlock = ms->numBlocks-1;      
    while (lastBlock >= 0)
    {
        readPage_sublist(ms, lastBlock, es, metric);     
        int numBlocksSublist = *(int32_t*) &ms->buffer[0];       /* Retrieve block id (indexed from 0) to compute count of blocks in sublist */
        #if DEBUG
        printf("Read block: %d",lastBlock);
        printf(" Num: %d\n", numBlocksSublist);
          
        for (int k = 0; k < 31; k++)
        {
            test_record_t *buf = (void *)(ms->buffer + es->headerSize + k * es->record_size);
            printf("%d: Record: %d\n", k, buf->key);
        }
        #endif
        lastBlock = lastBlock - numBlocksSublist;
        readPage_sublist(ms, lastBlock, es, metric);                         
        
        val = getValue_sublist(ms, 0, es);    
        ms->min[regionIdx] = val;
        ms->offset[regionIdx] = lastBlock*es->page_size+es->headerSize+ms->fileOffset;
        #if DEBUG
        printf("New min. Index: %d", regionIdx);
        printf(" Min: %u", ms->min[regionIdx]);
        printf(" Offset: %lu\n", ms->offset[regionIdx]);
        #endif
        regionIdx--;
        lastBlock--;
    }
       
    #ifdef DEBUG   
        printf("Region summary\n");
        for (i=0; i < ms->numRegions; i++)
        { 
            printf("Reg: %d",i); 
            printf(" Min: %u", ms->min[i]);
            printf(" Offset: %lu\n", ms->offset[i]);
        }
           
    #endif
      
    ms->current = INT_MAX;
    ms->next    = INT_MAX; 
    ms->nextIdx = 0;       
    ms->lastBlockIdx = INT_MAX;
}

char* next_MinSort_sublist(MinSortStateSublist* ms, external_sort_t *es, void *tupleBuffer, metrics_t *metric)
{
    unsigned int i, curBlk;                                                     
    unsigned long int startIndex;
                 
    // Find the block with the minimum tuple value - otherwise continue on with last block            
    if (ms->nextIdx == 0)
    {    // Find new block as do not know location of next minimum tuple
        ms->current = INT_MAX;
        ms->regionIdx = INT_MAX; 
        ms->next = INT_MAX; 
        for (i=0; i < ms->numRegions; i++)
        {
             metric->num_compar++;

            if (ms->min[i] < ms->current)
            {   ms->current = ms->min[i];
                ms->regionIdx = i;
            }
        }        
        if (ms->regionIdx == INT_MAX)
            return NULL;    // Join complete - no more tuples	            		
            
        // Determine current block and record index for next smallest value based on file offset
        startIndex = ms->offset[ms->regionIdx]; 
        i = (startIndex % es->page_size - es->headerSize) / ms->record_size;
        curBlk = startIndex / es->page_size;

        // Smallest value is at current index
        if (curBlk != ms->lastBlockIdx)
        {    // Read block into buffer   
            readPage_sublist(ms, curBlk, es, metric);                               
        }     
    }
    else
    {   // Use next record in current block
        i = ms->nextIdx;
    }    

    memcpy(tupleBuffer, &(ms->buffer[ms->record_size * i+es->headerSize]), ms->record_size);
    metric->num_memcpys++;                   

    #ifdef DEBUG
        test_record_t *buf = (test_record_t*) (ms->buffer+es->headerSize+i*es->record_size);                    
        buf = (test_record_t*) tupleBuffer;
        printf("Returning tuple: %d\n", buf->key);                    
    #endif

    // Advance to next tuple in block
    i++;
    ms->nextIdx = 0;  

    if (i >= getNumRecordsBlock(ms))
    {   // Advance to next block
        i = 0;
        int32_t currentBlockId = getBlockId(ms);
        curBlk++;
        readPage_sublist(ms, curBlk, es, metric);   
        if (currentBlockId >= getBlockId(ms))
        {   // Transitioned to a block in a new sublist
            ms->offset[ms->regionIdx] = -1;
            ms->min[ms->regionIdx] = INT_MAX;
        }
        else
        {
            ms->offset[ms->regionIdx] = curBlk*es->page_size+es->headerSize;
            ms->min[ms->regionIdx] = getValue_sublist(ms,0,es);    
        }        
    }
    else
    {
        ms->offset[ms->regionIdx] += es->record_size;
        ms->min[ms->regionIdx] = getValue_sublist(ms,i,es); 
        if (ms->min[ms->regionIdx]  == ms->current)                
            ms->nextIdx = i; 	
    }       
   
    #ifdef DEBUG	
        printf("Updated minimum in block to: %d\r\n", ms->min[ms->regionIdx]);
    #endif			
   
    return tupleBuffer;		    
}

void close_MinSort_sublist(MinSortStateSublist* ms, external_sort_t *es)
{
    /*
    printf("Tuples out:  %lu\r\n", ms->op.tuples_out); 
    printf("Blocks read: %lu\r\n", ms->op.blocks_read);
    printf("Tuples read: %lu\r\n", ms->op.tuples_read);  
    printf("Bytes read:  %lu\r\n", ms->op.bytes_read);
    */
}

/**
@brief      Flash Minsort implemented that has input file with sorted sublists.
@param      iteratorState
                Structure stores state of iterator (file info etc.)
@param      tupleBuffer
                Pre-allocated space to store one tuple (row) of input being sorted
@param      outputFile
                Already opened file to store sorting output (and in-progress temporary results)
@param      buffer
                Pre-allocated space used by algorithm during sorting
@param      bufferSizeInByes
                Size of buffer in byes
@param      es
                Sorting state info (block size, record size, etc.)
@param      resultFilePtr
                Offset within output file of first output record
@param      metric
                Tracks algorithm metrics (I/Os, comparisons, memory swaps)
@param      compareFn
                Record comparison function for record ordering
@param      numSubList
                Number of sublists
*/
int flash_minsort_sublist(
        void    *iteratorState,
		void    *tupleBuffer,
        ION_FILE *outputFile,		
		char    *buffer,        
		int     bufferSizeInBytes,
		external_sort_t *es,
		long    *resultFilePtr,
		metrics_t *metric,
        int8_t  (*compareFn)(void *a, void *b),
        long    numSubList
)
{
    printf("*Flash Minsort (sorted sublist version)*\n");       

    MinSortStateSublist ms;
    ms.buffer = buffer;
    ms.iteratorState = iteratorState;
    ms.memoryAvailable = bufferSizeInBytes;
    ms.num_records = ((file_iterator_state_t*) iteratorState)->totalRecords;
    ms.numRegions = numSubList;
    ms.fileOffset = *resultFilePtr;

    init_MinSort_sublist(&ms, es, metric);
    int16_t count = 0;  
    int32_t blockIndex = 0;
    int16_t values_per_page = (es->page_size - es->headerSize) / es->record_size;
    char* outputBuffer = buffer+es->page_size;    
    unsigned long lastWritePos = ms.fileOffset +   es->num_pages * es->page_size;     

    // Write 
    while (next_MinSort_sublist(&ms, es, (char*) (outputBuffer+count*es->record_size+es->headerSize), metric) != NULL)
    {         
        // Store record in block (already done during call to next)                
        count++;

        if (count == values_per_page)
        {   // Write block
            *((int32_t *) outputBuffer) = blockIndex;                             /* Block index */
            *((int16_t *) (outputBuffer + BLOCK_COUNT_OFFSET)) = count;            /* Block record count */
            count=0;
            
            // Force seek to end of file as outputFile is also inputFile and have been reading it
            fseek(outputFile, lastWritePos, SEEK_SET);
            // fseek(outputFile, 0, SEEK_END);

            if (0 == fwrite(outputBuffer, es->page_size, 1, outputFile))
                return 9;
            lastWritePos += es->page_size;
             metric->num_writes += 1;
/*
printf("Loc2: %lu\n", ftell(outputFile));
             if (blockIndex % 16 == 0)
                printf("Last write pos: %lu Block: %d\n", lastWritePos, blockIndex);
                */
        #ifdef DEBUG_OUTPUT
            printf("Wrote output block. Block index: %d\n", blockIndex);
            for (int k = 0; k < values_per_page; k++)
            {
                test_record_t *buf = (void *)(outputBuffer + es->headerSize + k * es->record_size);
                printf("%d: Output Record: %d\n", k, buf->key);
            }
        #endif
            blockIndex++;
        }       
    }

    if (count > 0)
    {
        fseek(outputFile, lastWritePos, SEEK_SET);
        metric->num_writes += 1;
        // Write last block        
        *((int32_t *) buffer) = blockIndex;                             /* Block index */
        *((int16_t *)(buffer + BLOCK_COUNT_OFFSET)) = count;            /* Block record count */
        count=0;
        blockIndex++;
        if (0 == fwrite(buffer, es->page_size, 1, outputFile))
            return 9;
    }
     
    close_MinSort_sublist(&ms, es);   

    *resultFilePtr = 0;
    free(ms.min);
    free(ms.offset);

//    printf("Complete. Comparisons: %d  MemCopies: %d  TransferIn: %d  TransferOut: %d TransferOther: %d\n", metric->num_compar, metric->num_memcpys, numShiftIntoOutput, numShiftOutOutput, numShiftOtherBlock);

    return 0;
}
