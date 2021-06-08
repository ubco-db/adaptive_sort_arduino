# Adaptive Sort for Flash Memory

The Flash Adaptive Sort was published at SAC 2021 ([paper](https://dl.acm.org/doi/10.1145/3412841.3441914)). This version is specifically for Arduino.

The adaptive sort either uses external merge sort or index-based minsort to sort data depending on the data distribution and flash performance (read vs write times). It can be used for embedded devices with low memory.

The adaptive sort has the following benefits:

1. Minimum memory usage is only two page buffers. The memory usage is less than 1.5 KB for 512 byte pages.
2. No use of dynamic memory (i.e. malloc()). 
3. Easy to use and include in existing projects. 
4. Open source license. Free to use for commerical and open source projects.

## License
[![License](https://img.shields.io/badge/License-BSD%203--Clause-blue.svg)](https://opensource.org/licenses/BSD-3-Clause)

## Code Files

* adaptive_sort.c, adaptive_sort.h - implementation for adaptive sort
* flash_minsort.c, flash_minsort.h - implementation of index-based minimum value sort
* flash_minsort_sublist.c, flash_minsort_sublist.h - sorted sublist variant of minimum value sort
* in_memory_sort.c, in_memory_sort.h - implementation of quick sort
* no_output_heap.c, no_output_heap.h - used for replacement selection
* serial_c_interface.c, serial_c_interface.h - serial output for Arduino

#### Ramon Lawrence<br>University of British Columbia Okanagan
