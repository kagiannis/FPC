# FPC - Fast Prefix Coder

## Features
 * Adaptive block subdivision
 * Optimal length limited prefix codes 
 using fast implementation of package - merge algorithm
 * Configurable number of streams (default 3)
 * Configurable max bit length (default 11)
 * Fast even for inorder cpus
 * Fast even when compression ratio is low,because FPC decodes 1 byte a time
 * License ISC
## Current Limitations
 * The bit length encoding is simple and hurts compression ratio
 * Works only for little endian cpus
 * May segfault when decompressing not trusted files
## Benchmark
### file enwik8
#### i7-6500U
```
16KB block
100000000 -> 63155808, 63.16% of original,ratio = 1.583
compression speed 438.36 MB/s, decompression speed 731.63 MB/s

32KB block
100000000 -> 63415354, 63.42% of original,ratio = 1.577
compression speed 503.01 MB/s, decompression speed 803.38 MB/s
  
adaptive
100000000 -> 62662924, 62.66% of original,ratio = 1.596
compression speed 49.70 MB/s, decompression speed 698.06 MB/s
  
huff0
100000000 ->  63258831 (63.26%),  576.3 MB/s ,  694.0 MB/s 

actual output file size for huff0 (non benchmark mode) 63423952
for fpc actual file size difference is 4 bytes
(2 bytes magic number + 2 bytes end of stream)
```
#### orange pi pc plus allwinner h3
```
16KB block
100000000 -> 63155808, 63.16% of original,ratio = 1.583
compression speed 55.50 MB/s, decompression speed 78.11 MB/s

32KB block
100000000 -> 63415354, 63.42% of original,ratio = 1.577
compression speed 59.00 MB/s, decompression speed 79.64 MB/s

adaptive
100000000 -> 62662924, 62.66% of original,ratio = 1.596
compression speed 4.04 MB/s, decompression speed 72.70 MB/s

huff0
100000000 ->  63258831 (63.26%),   52.5 MB/s ,   52.6 MB/s

actual output file size for huff0 (non benchmark mode) 63423952
for fpc actual file size difference is 4 bytes
(2 bytes magic number + 2 bytes end of stream)
```
