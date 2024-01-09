#include "BS_thread_pool.hpp"
#include "BlockCache.hpp"
#include "CacheSimple.hpp"
#include "SegmentTree.hpp"
#include "Serializer.hpp"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
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

enum CACHE_MODE { NORMAL_CACHE, SIMPLE_CACHE, NO_CACHE };

#pragma once

class GraphNode {
public:
  GraphNode() {}
  GraphNode(int _id) : id(_id) {}
  GraphNode(int _id, int _degree) : id(_id), degree(_degree) {}
  int id = 0, degree = 0;
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

  void set_cache_size(int cache_mb);
  void set_cache_size(double cache_ratio);
  void set_cache_mode(CACHE_MODE c_mode);
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

  int get_key(int v_id);
  uint32_t get_degree(uint32_t v_id);
  std::vector<uint32_t> get_edges(uint32_t v_id);

  uint64_t get_data_mb();

  void process_queue(
      std::vector<uint32_t> &frontier, std::vector<uint32_t> &next,
      std::function<void(uint32_t, uint32_t, std::vector<uint32_t> &)> update);
  // void process_queue(
  //     std::vector<uint32_t> &frontier, std::vector<uint32_t> &next,
  //     std::function<void(uint32_t)> ready,
  //     std::function<void(uint32_t, uint32_t)> compute,
  //     std::function<void(uint32_t)> finish,
  //     std::function<void(uint32_t, uint32_t, std::vector<uint32_t> &)>
  //     update);

  void process_queue(std::vector<uint32_t> &frontier,
                     std::vector<uint32_t> &next,
                     std::function<void(uint32_t, std::vector<uint32_t> &,
                                        std::vector<uint32_t> &)>
                         process);
  void process_queue(std::vector<uint32_t> &frontier,
                     std::vector<uint32_t> &next,
                     std::function<void(uint32_t, std::vector<uint32_t> &,
                                        std::vector<std::atomic_bool> &)>
                         process);

  void process_queue_in_blocks(
      std::vector<uint32_t> &frontier, std::vector<uint32_t> &next,
      std::function<void(uint32_t, uint32_t, std::vector<uint32_t> &)> func);

  // void process_queue_in_blocks(
  //     std::vector<uint32_t> &frontier, std::vector<uint32_t> &next,
  //     std::function<void(uint32_t)> ready,
  //     std::function<void(uint32_t, uint32_t)> compute,
  //     std::function<void(uint32_t)> finish,
  //     std::function<void(uint32_t, uint32_t, std::vector<uint32_t> &)>
  //     update);
  void
  process_queue_in_blocks(std::vector<uint32_t> &frontier,
                          std::vector<uint32_t> &next,
                          std::function<void(uint32_t, std::vector<uint32_t> &,
                                             std::vector<uint32_t> &)>
                              process);
  void
  process_queue_in_blocks(std::vector<uint32_t> &frontier,
                          std::vector<uint32_t> &next,
                          std::function<void(uint32_t, std::vector<uint32_t> &,
                                             std::vector<std::atomic_bool> &)>
                              process);
  void set_thread_pool_size(uint32_t tp_size);

private:
  void dump_metadata();
  void dump_vertices();
  void reset_cache();

  uint32_t get_cache_block_idx(uint32_t eb_id);
  void set_active_vertices(std::vector<uint32_t> &v_id_vec);
  std::vector<uint32_t> get_neighbors(uint32_t cb_idx, uint32_t v_id);
  void finish_block(uint32_t cb_idx);

  std::vector<GraphNode> nodes;
  bool gs_init;
  int num_nodes;
  uint64_t num_edges;
  int num_edge_blocks, num_vertex_blocks;
  int tmp_node_id = 0; // For parsing

  std::vector<VertexBlock> vb_vec;
  std::unordered_map<int, int> reorder_node_id;

  uint32_t get_eb_id(uint32_t v_id);
  uint32_t get_eb_offset(uint32_t v_id);

  std::vector<uint32_t> active_vertices;
  std::vector<uint32_t> active_edge_blocks;
  std::vector<std::vector<uint32_t>> active_vid_in_eb;

  BlockCache<EdgeBlock> *edge_cache;
  SimpleCache<EdgeBlock> *simple_cache;
  CACHE_MODE cache_mode = NORMAL_CACHE;
  int num_cache_blocks;

  inline static BS::thread_pool pool{0};
  std::mutex mtx;
};
