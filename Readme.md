# CAVE - Concurrency-Aware Graph Processing System for SSD

## Overview

*CAVE* is a graph processing engine for
storing, accessing, and performing graph analytics on SSDs. CAVE considers SSD’s supported concurrency through its *internal parallelism* as a key property to exploit and it does so via issuing carefully tuned concurrent I/Os to the graphs stored on a single SSD. *CAVE* adopts a natural *blocked file* format based on adjacency
lists and uses a concurrent cache pool for data blocks to provide ease of implementation of different algorithms.

## Build & Compile

Goto `./cpp` folder, then run `make` for building.

```bash
cd cpp
make
```

**Clang** compiler is specified by default for its better support and performance of multi-threading. Using **GCC** is okay but we won't guarantee the performance.

We tested in the following configurations:

* RedHat Linux 4.9, Clang 11, EXT4 file system.
* Windows 11, GCC 13 + LLVM + UCRT toolchain from winlibs.com, NTFS file system.

## Usage

### Data File Parser

We developed a `parser` to convert common graph data into our binary file structure. It provides simple support for standard adjacent list and edge list files in plain text format for testing purpose. For more robust conversion, please check the `graph_parser.py` in `scripts` folder and documentation of [NetworkX package](https://networkx.org/).

```bash
./Parser <input_data_path> -format (adjlist/edgelist)
```

### Testing

```bash
./main <data_path> (-arg_name arg_val)
```

* `-test_type`: `thread`, `cache`, or `fsize`.
* `-test_algo`. `search` for BFS+DFS, `wcc` for WCC.
* `-nkeys`. Number of keys in testing.
* `-ntests`. Number of runs of testing.
* `-max_threads`. Maximum thread count.
* `-cache_size_mb`. Cache size in megebytes.

Test results will be put in `output` folder in csv format. 

## Algorithm

To demonstrate the benefits of our system, we implemented parallel algorithms of breadth-first search (BFS), depth-first search (DFS), and weakly connected components (WCC). These algorithms are in `GraphAlgorithm.cpp`, and can be executed and benchmarked by running `main` with the `-test_type` argument.

## Reference

Thread pool library comes from [BS::thread-pool]( https://github.com/bshoshany/thread-pool), a fast, lightweight, and easy-to-use C++17 thread pool library. 

Unordered parallel DFS is referred to ideas in [A work-efficient algorithm for parallel unordered depth-first search](https://dl.acm.org/doi/10.1145/2807591.2807651).
