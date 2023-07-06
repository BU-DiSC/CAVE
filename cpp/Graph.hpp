#include "BS_thread_pool.hpp"
#include "BlockCache.hpp"
#include "SegmentTree.hpp"
#include "Serializer.hpp"
#include <cassert>
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

enum CACHE_MODE { ALL_CACHE, SMALL_CACHE };

#pragma once

class GraphNode {
public:
  GraphNode() : key(-1), id(0), degree(0) {}
  int key, id, degree;
  std::vector<int> edges;
  std::set<int> edges_set; // For edgelist format
  // uint8_t num_values;
  // std::vector<uint16_t> values;
};

class Graph {
public:
  Graph() : gs_init(false), num_nodes(0), cache_mode(CACHE_MODE::SMALL_CACHE) {}
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

  void read_vb_list();

  bool wait_all_signals();

  // bool req_one_snode(int node_id, int stack_id = 0);
  // std::shared_ptr<S_Node> get_one_snode(int &stack_id);

  std::shared_ptr<MetaBlock> read_metadata();

  // For parser
  void set_node_edges(int node_id, std::vector<int> &edges);
  void add_edge(int node_id1, int node_id2);
  void init_nodes(int _num_nodes);
  void finalize_edgelist();

  void clear_nodes();
  // void clear_signals();
  // void send_end_signal();
  // void consume_one_signal();
  void prep_gs();
  int get_num_nodes();

  Serializer gs;

  int get_node_key(int node_id);
  int get_node_degree(int node_id);
  std::vector<int> get_edges(int node_id);
  void set_cache_mode(CACHE_MODE c_mode);

private:
  void dump_metadata();
  void dump_vertices();

  std::vector<GraphNode> nodes;
  bool gs_init;
  int num_nodes;
  int num_edge_blocks;
  int num_vertex_blocks;
  CACHE_MODE cache_mode;

  std::vector<std::shared_ptr<VertexBlock>> vb_list;
  std::unordered_map<int, int> reorder_node_id;
  int tmp_node_id = 0;

  bool enable_cache = false;
  BlockCache<EdgeBlock> *edge_cache;
};
