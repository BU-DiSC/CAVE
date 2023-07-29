#include "BS_thread_pool.hpp"
#include "Graph.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <queue>
#include <random>
#include <unordered_set>
#include <vector>

#pragma once

#define MAX_ACTIVE_STACKS 255
#define MAX_REQ 255

class GraphAlgorithm {
private:
  std::random_device rd;
  std::mt19937 eng;

  Graph *g;
  int num_nodes;
  std::string path;

  std::vector<int> frontier;
  std::vector<int> next;

  std::vector<bool> vis;
  std::vector<std::atomic<bool>> atomic_vis;

  bool is_found = false;

  int start_id = 0;
  std::vector<int> start_ids;
  int key = 0;

  std::uniform_int_distribution<> distIndex;

  std::atomic<int> num_free_stacks;
  int max_stack_size = 4;

  // // For p_dfs_async
  // std::atomic<int> num_active_stacks;
  std::vector<int> stacks[MAX_ACTIVE_STACKS];
  std::vector<int> free_stacks;

  // std::atomic<int> q_head;
  // int q_next[MAX_ACTIVE_STACKS + 1];

  inline static BS::thread_pool pool{0};

  std::mutex mtx;

  void p_dfs_task(std::vector<int> &stack);
  // void p_dfs_async_task();

  void p_pagerank_task(int v, std::vector<float> &pg_score, float eps);

public:
  GraphAlgorithm(std::string _path, MODE mode);

  void set_max_stack_size(int _size) { max_stack_size = _size; }
  void set_start_id(int _id) { start_id = _id; }
  void set_key(int _key) { key = _key; }
  void reset_num_threads() { pool.reset(); }
  void set_num_threads(int t) { pool.reset(t); }

  void disable_cache();
  void set_cache_size(int _cache_size);
  void set_cache_ratio(double _cache_ratio);
  void set_cache_mode(CACHE_MODE c_mode);

  int get_num_nodes() { return g->get_num_nodes(); }

  void clear();
  void clear_stack();

  void set_mode(MODE mode);

  int s_WCC();
  int p_WCC();

  int s_WCC_alt();
  int p_WCC_alt();

  int p_WCC_alt2();

  bool s_bfs();
  bool s_bfs_alt();

  bool p_bfs();
  bool p_bfs_alt();
  bool p_bfs_all();

  unsigned long long s_triangle_count();
  unsigned long long s_triangle_count_alt();
  unsigned long long p_triangle_count();

  float s_pagerank();
  float p_pagerank();
  float p_pagerank_alt();

  bool s_dfs();
  bool p_dfs();

  void bench_io(int num);

  // bool s_bfs_async();
  // bool p_bfs_async();

  // bool s_dfs_async();
  // bool s_dfs_async_no_splits();
  // bool p_dfs_async();
};
