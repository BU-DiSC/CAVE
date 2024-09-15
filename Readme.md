# CAVE - Concurrency-Aware Graph Processing System for SSD

## Overview

*CAVE* is a graph processing engine for
storing, accessing, and performing graph analytics on SSDs. CAVE considers SSD’s supported concurrency through its *internal parallelism* as a key property to exploit and it does so via issuing carefully tuned concurrent I/Os to the graphs stored on a single SSD. *CAVE* adopts a natural *blocked file* format based on adjacency
lists and uses a concurrent cache pool for data blocks to provide ease of implementation of different algorithms.

The paper: https://dl.acm.org/doi/pdf/10.1145/3654928.

![Architecture](./figures/Architecture.png)

## Build & Compile

Goto `./cpp` folder, then run `make` for building.

```bash
cd cpp
make
```

**Clang** compiler is specified by default for its better support and performance of multi-threading. Using **GCC** is okay but we won't guarantee the performance.

We tested in the following configurations:

* RedHat Linux 4.9, Clang 11, with EXT4 file system.
* Windows 11, GCC 13 and 14 toolchain using [MinGW-w64](https://www.mingw-w64.org/), with NTFS file system.

## Usage

### Prepare Data

We developed a `parser` to read common graph data into our binary file structure. It provides simple support for standard adjacent list, edge list files in plain text format, as well as binary adjacency and edge list for compact storage and faster parsing.

Please check `/scripts/graph_convert.py`, install and read documentation of [NetworKit package](https://networkit.github.io/) about how to convert other data formats and make a binary file from plain texts. *We recommend first convert the input graph to `binadj` or `binedge` format for the best parsing performance.*

Then run parser to pre-process the data file.

```bash
./bin/parser <input_data_path> -format (adjlist/edgelist/binedge/binadj)
```

Data files with suffix `.adjlist`, `.edgelist`, `.binadj` and `.binedge` will be automatically detected. Otherwise please indicate the file format by the `-format` argument. 

### Run Algorithm and Benchmark

After compilation, `/bin` will include executables of algorithms to be tested. We provide BFS, DFS, WCC, PageRank, and Random Walk algorithms out of the box. You can edit and implement your algorithms following similar codes.

Our executables supports the following arguments for benchmark usages.

```bash
./bin/[algo] <parsed_data_path> (cache/thread) [args]
```

The following are parameters for the benchmark.

* For cache tests, there's only one argument in [0,1,2,3] states that which cache size list you want to run:
  * 0: Only 1024MB. For sanity check.
  * 1: [1,2,3,4,5,10,25,50]. Suggest use for small datasets < 50MB.
  * 2: [20,40,60,80,100,200,500,1000]. For dataset like soc-LiveJounal1 sized ~1GB.
  * 3: [128,256,...,16384]. For very large dataset like com-Friendster.

* For thread tests, 3 arguments are available.  

  * The first one is the minimum number of threads, the second one is the maximum. It tests from the minimum by the power of 2 to the maximum.
  * The third one is optional, for specifying cache size (in MB) for all tests. By default it is 1024MB.

Test results will be put in `log` folder in `csv` format.

## Example:

* Benchmark BFS algorithm on [CA-GrQc dataset](https://snap.stanford.edu/data/ca-GrQc.html).

  ```bash
  # Parse data
  ./bin/parser ../data/CA-GrQc.txt -format edgelist

  # Benchmark
  ./bin/bfs ../data/CA-GrQc.bin thread 1 256
  ./bin/bfs ../data/CA-GrQc.bin cache 0
  ```
* Benchmark WCC algorithm on [soc-LiveJournal1 dataset](https://snap.stanford.edu/data/soc-LiveJournal1.html).

  ```bash
  # Parse data
  ./bin/parser ../data/soc-LiveJournal1.binadj

  # Benchmark
  ./bin/wcc ../data/soc-LiveJournal1.bin cache 1
  ```
## Obtain Datasets in our Paper

[Stanford Large Network Dataset
Collection](https://snap.stanford.edu/data/) for [Friendster](https://snap.stanford.edu/data/com-Friendster.html), [RoadNet](https://snap.stanford.edu/data/roadNet-PA.html), [LiveJournal](https://snap.stanford.edu/data/soc-LiveJournal1.html), and [YouTube](https://snap.stanford.edu/data/com-Youtube.html) dataset.

[LDBC Graph Analytics Benchmark](https://ldbcouncil.org/benchmarks/graphalytics/) for the _Twitter-mpi_ dataset.

We also provide a simple random graph generator in `/scripts/graph_gen.py` also based on NetworKit.

## Read Benchmark Results

The benchmark output will be stored in `/log` folder with naming scheme `[data]_[algo]_[testcase].csv`. The columns are

* algo_name: Name of the algorithm.
* thread: Number of threads used.
* cache_mb: Size of cache pool used (in megabytes)
* **time: Running time (in microsecond).**
* res: Output of algorithms. 0/1 for searching algorithms and a number for WCC or PageRank.

Then you can pick your favorite way to process, compare and plot the results. We use the popular [Matplotlib](https://matplotlib.org/) Python library to create figures in our paper. A Python notebook `/scripts/example_plot.ipynb` can be used a start.

## Algorithm

We implemented parallel version of breadth-first search (BFS), PageRank, weakly connected components (WCC), and random walk algorithms. We also tested to implement a parallel pseudo DFS algorithm with introductions below. All the codes of algorithms are in `/algorithm`, and can be executed and benchmarked by running corresponding executables.

### Parallel Pseudo Depth-First Search algorithm

![parallel_pdfs_figure](./figures/pdfs_example.png)

While DFS is inherently a serialized algorithm, it is possible to enhance its performance by introducing parallelism through unordered or *pseudo depth-first search* technique.

We take inspiration from this idea and we incorporate a mechanism to monitor the size of the vertex stack for each thread in our implementation. After visiting the neighbors of a vertex, we check if the size of the stack exceeds a predefined threshold. If it does, the stack is evenly divided into two smaller stacks, and one of these stacks is assigned to a new thread for further exploration.

## Link to Other Systems

* [GraphChi: Large-Scale Graph Computation on Just a PC](https://dl.acm.org/doi/10.5555/2387880.2387884).
    * Code: https://github.com/GraphChi/graphchi-cpp
    * See wiki [here](https://github.com/GraphChi/graphchi-cpp/wiki/Command-Line-Parameters) for command parameters. Use `membudget_mb` to limit cache size and `execthreads` for number of threads.

* [GridGraph: Large-Scale Graph Processing on a Single Machine Using 2-Level Hierarchical Partitioning](https://dl.acm.org/doi/10.5555/2813767.2813795).
    * Code: https://github.com/thu-pacman/GridGraph
    * The compiled binary file takes `[memory budget]` parameter for setting cache size. The number of threads can be controlled by changing value of `parallelism` in the [source file](https://github.com/thu-pacman/GridGraph/blob/master/core/graph.hpp).

* [Mosaic: Processing a Trillion-Edge Graph on a Single Machine](https://dl.acm.org/doi/10.1145/3064176.3064191). 
    * Code: https://github.com/sslab-gatech/mosaic
    * Edit the [config file](https://github.com/sslab-gatech/mosaic/blob/master/config/default.py). `SG_RB_SIZE_HOST_TILES` for cache size and `SG_NPROCESSOR` for threads.

## Reference

Thread pool library comes from [BS::thread-pool](https://github.com/bshoshany/thread-pool), a fast, lightweight, and easy-to-use C++17 thread pool library. 

To support parallel hashmap used in cache pool, we use [The Parallel Hashmap](https://github.com/greg7mdp/parallel-hashmap), a set of excellent hash map implementations, as well as a btree alternative to std::map and std::set.

Unordered parallel DFS refers to ideas in [A work-efficient algorithm for parallel unordered depth-first search](https://dl.acm.org/doi/10.1145/2807591.2807651).
