#include "BS_thread_pool.hpp"
#include "BlockCache.hpp"
#include "SegmentTree.hpp"
#include "Serializer.hpp"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

enum CACHE_MODE { NORMAL_CACHE, SMALL_CACHE };

#pragma once

class GraphNode {
public:
  GraphNode() : key(-1), id(0), degree(0) {}
  int key, id, degree;
  std::vector<int> edges;
};

class Graph {
public:
  Graph()
      : gs_init(false), num_nodes(0), cache_mode(CACHE_MODE::NORMAL_CACHE) {}
  ~Graph() {}

  void init_serializer(std::string path, MODE mode);
  void clear_serializer();

  void init_metadata();
  void init_vertex_data();
  // void init_cache();

  void set_cache(int cache_size);
  void set_cache(double cache_ratio);
  void disable_cache();
  void clear_cache();

  void dump_graph();

  void read_vertex_blocks();

  bool wait_all_signals();

  std::shared_ptr<MetaBlock> read_metadata();

  // For parser
  void set_node_edges(int node_id, std::vector<int> &edges);
  void add_edge(int src_id, int dst_id);
  void init_nodes(int _num_nodes);
  void finalize_edgelist();

  void clear_nodes();

  void prep_gs();
  int get_num_nodes();

  Serializer gs;

  int get_node_key(int node_id);
  int get_node_degree(int node_id);
  std::vector<uint32_t> get_edges(int node_id);
  void set_cache_mode(CACHE_MODE c_mode);

private:
  void dump_metadata();

  template <class TV, class TE> void dump_vertices();
  std::vector<uint32_t> _get_edges(int node_id);
  std::vector<uint32_t> _get_edges_large(int node_id);
  void _set_cache(int num_cache_blocks);

  std::vector<GraphNode> nodes;
  bool gs_init;
  int num_nodes;
  uint64_t num_edges;
  int num_edge_blocks, num_vertex_blocks;
  int tmp_node_id = 0; // For parsing

  std::vector<VertexBlock> vb_vec;
  std::vector<LargeVertexBlock> vb_vec_large;

  std::unordered_map<int, int> reorder_node_id;

  BlockCache<EdgeBlock> *edge_cache;
  BlockCache<LargeEdgeBlock> *edge_cache_large;

  bool enable_cache = false;
  bool enable_large_block = false;
  CACHE_MODE cache_mode;
};
