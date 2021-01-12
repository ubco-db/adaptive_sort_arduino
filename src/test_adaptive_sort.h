/******************************************************************************/
/**
@file		test_adaptive_sort.h
@author		Ramon Lawrence
@brief		This file performance/correctness testing of flash MinSort.
@copyright	Copyright 2020
			The University of British Columbia,
			IonDB Project Contributors (see AUTHORS.md)
@par Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

@par 1.Redistributions of source code must retain the above copyright notice,
	this list of conditions and the following disclaimer.

@par 2.Redistributions in binary form must reproduce the above copyright notice,
	this list of conditions and the following disclaimer in the documentation
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
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flash_minsort.h"
#include "in_memory_sort.h"
#include "adaptive_sort.h"

/* Used to validate each individual input data item in the sorted output */
// #define DATA_COMPARE    1

#ifdef DATA_COMPARE
int32_t sampleData[500];

int cmpfunc (const void * a, const void * b) {
   return ( *(int*)a - *(int*)b );
}
#endif

int seed;

/**
 * Generates test data records
 */
int
external_sort_write_test_data(
        ION_FILE *unsorted_file,
        int32_t num_values,
        uint16_t record_size,
        int testDataType,
        external_sort_t *es,
        int percent_random,
        int numDistinct)
{
    printf("Data Type: %d  Percent random: %d  Num. distinct: %d\n", testDataType, percent_random, numDistinct);

    char buffer[record_size];

    /* Data record is empty. Only need to reset to 0 once as reusing struct. */    
    uint32_t i, j;
    for (i = 0; i < record_size-4; i++)
        buffer[i + sizeof(int32_t)] = 0;    

    int32_t blockIndex = 0;
    int recordKey = 0;
    int count = 0;
    int32_t key;
    uint16_t values_per_page = (es->page_size - es->headerSize) / es->record_size;
    uint16_t bytesAtEndOfBlock = es->page_size-es->headerSize-values_per_page*es->record_size;
    for (i = 0; i < es->num_pages-1; i++)
    {
        /* Write out header */
        fwrite(&blockIndex, sizeof(int32_t), 1, unsorted_file);
        fwrite(&values_per_page, sizeof(int16_t), 1, unsorted_file);   
    
        for (j = 0; j < values_per_page; j++)
        {
            if (testDataType == 1)            
            {   /* Reverse data */
                key = num_values - recordKey;
            }
            else if (testDataType == 0)
            {   /* Sorted data */
                key = recordKey + 1;
            }
            else if (testDataType == 2)
            {   /* Random data */
                key = rand() % numDistinct; 
            }
            else if (testDataType == 3)
            {   /* Percentage random data */
                if ((rand() % 100) < percent_random) 
                    key = rand() % numDistinct;
                else 
                    key = recordKey + 1;
            }
            *((int32_t*)buffer) = key;
            
            #ifdef DATA_COMPARE 
            sampleData[count] = (int32_t) *((int32_t*)buffer);              
            #endif
            
            recordKey++;
            count++;
            /* Write out record */
            if (0 == fwrite(buffer, (size_t)record_size, 1, unsorted_file))            
                return 10;            
        }
        blockIndex++;

        /* Write out remainder of block (should be zeros but not worrying about that currently) */
        fwrite(buffer, bytesAtEndOfBlock, 1, unsorted_file);
    }
   
    /* Write out last page. Write out header. */
    uint16_t recordsLastPage = num_values - count;   
    bytesAtEndOfBlock = es->page_size-es->headerSize-recordsLastPage*es->record_size; 
    fwrite(&blockIndex, sizeof(int32_t), 1, unsorted_file);  
    fwrite(&recordsLastPage, sizeof(int16_t), 1, unsorted_file);   

    for (j = 0; j < recordsLastPage; j++)
    {
        if (testDataType == 1)            
        {   /* Reverse data */
            key = num_values - recordKey;
        }
        else if (testDataType == 0)
        {   /* Sorted data */
            key = recordKey + 1;
        }
        else if (testDataType == 2)
        {   /* Random data */
            key = rand() % numDistinct; 
        }
        else if (testDataType == 3)
        {   /* Percentage random data */
            if ((rand() % 100) < percent_random) 
                key = rand()%numDistinct;
            else 
                key = recordKey + 1;
        }
        *((int32_t*)buffer) = key;
        #ifdef DATA_COMPARE 
        sampleData[count] = (int32_t) *((int32_t*)buffer);
        #endif
        count++;
        recordKey++;
        /* Write out record */
        if (0 == fwrite(buffer, (size_t)record_size, 1, unsorted_file))        
            return 10;        
    }    
    /* Write out remainder of block (should be zeros but not worrying about that currently) */
    fwrite(buffer, bytesAtEndOfBlock, 1, unsorted_file);

    #ifdef DATA_COMPARE     
   	qsort(sampleData, (uint32_t) num_values, sizeof(uint32_t), cmpfunc);			    
    
    for (i = 0; i < num_values; i++) 
       printf("Key: %d\n", sampleData[i]);    
    
    #endif  
    return 0;
}

/**
 * Iterates through records in a file returning NULL when no more records.
 */
int fileRecordIterator(void* state, void* buffer, external_sort_t *es)
{    
    file_iterator_state_t* fileState = (file_iterator_state_t*) state;    
    if (fileState->recordsRead >= fileState->totalRecords)
        return 0;

    /* Check if records left in block */
    if (fileState->recordsLeftInBlock == 0)
    {   /* Return next block */
        if (0 ==  fread(fileState->readBuffer, es->page_size, 1, fileState->file))
        {	printf("Failed to read block.\n");        
        }
        /* Get number of records in block */
        fileState->recordsLeftInBlock =  *((int16_t *) (fileState->readBuffer + (uint32_t) BLOCK_COUNT_OFFSET));
        fileState->currentRecord = 0;
/*        
        printf("Reading block: %d\r\n",fileState->recordsRead/31);        
        for (int k = 0; k < 31; k++)
        {
            test_record_t *buf = (void *)(fileState->readBuffer + es->headerSize + k * es->record_size);
            printf("%d: Record: %d\n", k, buf->key);
        }
*/        
    }

    /* Return next record in block */
    fileState->recordsRead++;
    fileState->recordsLeftInBlock--;

    /* Copy record from read buffer into tuple buffer */
    memcpy(buffer, fileState->readBuffer+es->headerSize+fileState->currentRecord*es->record_size, (size_t) es->record_size);
    fileState->currentRecord++;
    return 1;
}


void testRawPerformance()
{   /* Tests storage raw read and write performance */
    ION_FILE *fp;
    fp = fopen("tmpfilec.bin", "w+b");

    char buffer[512];
    // Test time to write 1000 blocks
    unsigned long startMillis = millis();

    for (int i=0; i < 1000; i++)
    {
        if (0 == fwrite(buffer, 512, 1, fp))
        {   printf("Write error.\n");             
        }
    }
    printf("Write time: %lu\n", millis()-startMillis);
    fflush(fp);

    // Time to read 1000 blocks
    fseek(fp, 0, SEEK_SET);
    startMillis = millis();
    for (int i=0; i < 1000; i++)
    {
        
        if (0 == fread(buffer, 512, 1, fp))
        {   printf("Read error.\n");             
        }
    }
    printf("Read time: %lu\n", millis()-startMillis);

    fseek(fp, 0, SEEK_SET);
    // Time to read 1000 blocks randomly    
    startMillis = millis();
    srand(1);
    for (int i=0; i < 1000; i++)
    {
        unsigned long num = rand() % 1000;         
         fseek(fp, num*512 , SEEK_SET);
        if (0 == fread(buffer, 512, 1, fp))
        {   printf("Read error.\n");             
        }
    }
    printf("Random Read time: %lu\n", millis()-startMillis);
}

void runalltests_adaptive_sort()
{
    int8_t          numRuns = 3;
    metrics_t       metric[numRuns];
    external_sort_t es;

    testRawPerformance();
    return;

    /* Set random seed */
    seed = time(0);  
    seed = 2020;  
    printf("Seed: %d\n", seed);
    srand(seed);       

    int mem;
    for(mem = 2; mem <= 2; mem++) 
    {
        printf("<---- New Tests M=%d ---->\n", mem);
        int t; 
        for (t = 1; t < 12; t++) 
        {
            printf("--- Test Number %d ---\n", t);
            for (int r=0; r < numRuns; r++)
            {            
                printf("--- Run Number %d ---\n", (r+1));
                int buffer_max_pages = mem;
                    
                metric[r].num_reads = 0;
                metric[r].num_writes = 0;
                metric[r].num_compar = 0;
                metric[r].num_memcpys = 0;
                metric[r].num_runs = 0;

                es.key_size = sizeof(int32_t); 
                es.value_size = 12;
                es.headerSize = BLOCK_HEADER_SIZE ;
                es.record_size = es.key_size + es.value_size;
                es.page_size = 512;

                int32_t values_per_page = (es.page_size - es.headerSize) / es.record_size;
                int32_t num_test_values = values_per_page;
												   
                int k;
                for (k = 0; k < t; k++)
                {                  
                    num_test_values *= buffer_max_pages;                  
                }
                // num_test_values = values_per_page * 10000;
                /* Add variable number of records so pages not completely full (optional) */
                // num_test_values += rand() % 10;
                es.num_pages = (uint32_t) (num_test_values + values_per_page - 1) / values_per_page; 
                es.compare_fcn = merge_sort_int32_comparator;

                /* Buffers and file offsets used by sorting algorithim*/
                long result_file_ptr;
                char *buffer = (char*) malloc((size_t) buffer_max_pages * es.page_size + es.record_size);
                char *tuple_buffer = buffer + es.page_size * buffer_max_pages;
                if (NULL == buffer) {
                    printf("Error: Out of memory!\n");
                    return;
                }

                /* Create the file and fill it with test data */
                ION_FILE *fp;
                fp = fopen("myfile4.bin", "w+b");
                if (NULL == fp) {
                    printf("Error: Can't open file!\n");
                    return;
                }
                
                // 0 - sorted, 1 - reverse sorted, 2 - random, 3 - percentage random
                external_sort_write_test_data(fp, num_test_values, es.record_size, 2, &es, 0, 256);

                fflush(fp);
                fseek(fp, 0, SEEK_SET);

                file_iterator_state_t iteratorState; 
                iteratorState.file = fp;
                iteratorState.recordsRead = 0;
                iteratorState.totalRecords = num_test_values;
                iteratorState.recordSize = es.record_size;
                iteratorState.readBuffer = malloc(es.page_size);
                iteratorState.recordsLeftInBlock = 0;  

                /* Open output file */
                ION_FILE *outFilePtr;
                outFilePtr = fopen("tmpsort4.bin", "w+b");

                if (NULL == outFilePtr)
                {
                    printf("Error: Can't open output file!\n");			
                }

                /* Run and time the algorithim */
                printf("num test values: %li\n", num_test_values);
                printf("blocks:%li\n", es.num_pages);
                #if defined(ARDUINO)
                unsigned long start = millis(); /* initial start time */
                #else
                clock_t start = clock();
                #endif                    
                
                int8_t runGenOnly = 0;
                int8_t writeReadRatio = 30;
                int err = adaptive_sort(&fileRecordIterator, &iteratorState, tuple_buffer, outFilePtr, buffer, buffer_max_pages, &es, &result_file_ptr, &metric[r], merge_sort_int32_comparator, runGenOnly, writeReadRatio);

                /* Close input data file */
                if (0 != fclose(fp)) {
                    printf("Error input file not closed!");
                }
                if (8 == err) {
                    printf("Out of memory!\n");
                } else if (10 == err) {
                    printf("File Read Error!\n");
                } else if (9 == err) {
                    printf("File Write Error!\n");
                    result_file_ptr = 0;
                    printf("Sort failed.\n");
                    continue;
                }

                
                unsigned long end = millis(); /* initial start time */
                unsigned long duration = (end - start);
                printf("Elapsed Time: %lu s\n", duration);
                metric[r].time = duration;
                metric[r].genTime = 0;

                /* Verify the data is sorted*/
                int sorted = 1;                
                fp = outFilePtr;            

                fseek(fp, result_file_ptr, SEEK_SET);

                uint32_t i;
                test_record_t last, *buf;
                int32_t numvals = 0, numerrors = 0;


                /* Read blocks of output file to check if sorted */
                for (i=0; i < es.num_pages; i++)
                {
                   //  printf("Page: %d\n",i);
                    if (0 == fread(buffer, es.page_size, 1, outFilePtr))
                    {	printf("Failed to read block.\n");
                        sorted = 0;
                    }

                    /* Read records from file */
                    int count = *((int16_t*) (buffer+BLOCK_COUNT_OFFSET));
                    /* printf("Block: %d Count: %d\n", *((int16_t*) buffer), count); */
                    char* addr = &(buffer[0]);
           
                    for (int j=0; j < count; j++)
                    {	
                        buf = (test_record_t*) (buffer+es.headerSize+j*es.record_size);	
                        // printf("Key: %d\n", buf->key);			                        
                        numvals++;
                        #ifdef DATA_COMPARE
                        if (sampleData[numvals-1] != buf->key)
                        {
                            printf("Num: %d", numvals);   
                            printf(" Expected: %d", sampleData[numvals-1]);   
                            printf(" Actual: %d\n", buf->key);   
                            sorted = 0;                     
                        }
                        #endif
                        
                        if (i > 0 && last.key > buf->key)
                        {                            
                            sorted = 0;
                            numerrors++;
                            if (numerrors < 50)
                            {
                                printf("VERIFICATION ERROR Offset: %lu Block header: %d",ftell(outFilePtr)-es.page_size,*((int32_t*) addr));
                                printf(" Records: %d Record key: %d\n", *((int16_t*) (addr+BLOCK_COUNT_OFFSET)), ((test_record_t*) (addr+BLOCK_HEADER_SIZE))->key);
                                printf("%d not less than %d\n", last.key, buf->key);
                            }
                        }

                        memcpy(&last, buf, es.record_size);				
                    }
                    /* Need to preserve buf between page loads as buffer is repalced */
                }		

                if (numvals != num_test_values)
                {
                    printf("ERROR: Missing values: %d\n", (num_test_values-numvals));
                    sorted = 0;
                };
                printf("Number of errors: %d\n", numerrors);

                /* Print Results*/
                printf("Sorted: %d\n", sorted);
                printf("Reads:%li\n", metric[r].num_reads);
                printf("Writes:%li\n", metric[r].num_writes);
                printf("I/Os:%li\n\n", metric[r].num_reads + metric[r].num_writes);
                printf("Num Comparisons:%li\n", metric[r].num_compar);
                printf("Num Memcpys:%li\n", metric[r].num_memcpys);
                printf("Num Runs:%li\n", metric[r].num_runs);

                /* Clean up and print final result*/
                free(buffer);
                free(iteratorState.readBuffer);
                if (0 != fclose(outFilePtr)) {
                    printf("Error file not closed!");
                }
                if (sorted)
                    printf("SUCCESS");
                else
                    printf("FAILURE");
                printf("\n\n");

            }
            /* Print Average Results*/
             int32_t value = 0;
             double v = 0;
             int32_t vals[7];
             printf("Time:\t\t");
             for (int i=0; i < numRuns; i++)
             {                
                printf("%li\t", (long) (metric[i].time));
                v+= metric[i].time;
             }
             printf("%li\n", (long) (v/numRuns));
             vals[0] = (long) (v/numRuns);

			v = 0;
            printf("GenTime:\t");
             for (int i=0; i < numRuns; i++)
             {                
                printf("%li\t", (long) (metric[i].genTime));
                v+= metric[i].genTime;
             }
             printf("%li\n", (long) (v/numRuns));
             vals[1] = (long) (v/numRuns);

			v = 0;
            printf("Runs:\t\t");
             for (int i=0; i < numRuns; i++)
             {                
                printf("%li\t", (long) (metric[i].num_runs));
                v+= metric[i].num_runs;
             }
             printf("%li\n", (long) (v/numRuns));
             vals[2] = (long) (v/numRuns);

             printf("Reads:\t\t");
             for (int i=0; i < numRuns; i++)
             {                
                printf("%li\t", metric[i].num_reads);
                value += metric[i].num_reads;
             }
             printf("%li\n", value/numRuns);
             vals[3] = value/numRuns;
             value = 0;
             printf("Writes: \t");
             for (int i=0; i < numRuns; i++)
             {                
                printf("%li\t", metric[i].num_writes);
                value += metric[i].num_writes;
             }
             printf("%li\n", value/numRuns);
             vals[4] = value/numRuns;
             value = 0;
             printf("Compares: \t");
             for (int i=0; i < numRuns; i++)
             {                
                printf("%li\t", metric[i].num_compar);
                value += metric[i].num_compar;
             }
             printf("%li\n", value/numRuns);
             vals[5] = value/numRuns;
             value = 0;
             printf("Copies: \t");
             for (int i=0; i < numRuns; i++)
             {                
                printf("%li\t", metric[i].num_memcpys);
                value += metric[i].num_memcpys;
             }
             printf("%li\n", value/numRuns);   
             vals[6] = value/numRuns;        
             // printf("%li\t%li\t%li\t%li\t%li\t%li\t%li\n",vals[0], vals[1], vals[2], vals[3], vals[4], vals[5], vals[6]);
             printf("%li\t%li\t%li\t%li\t%li\n",vals[0], vals[3], vals[4], vals[5], vals[6]);
        }
    }
}
