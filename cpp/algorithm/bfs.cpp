#include "../BS_thread_pool.hpp"
#include "../Graph.hpp"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

std::vector<uint32_t> frontier;
std::vector<uint32_t> next;
std::mutex mtx;
int nrepeats = 3;
std::string proj_name = "bfs";
FILE *log_fp;

int serial_bfs(Graph *g) {
  int num_nodes = g->get_num_nodes();
  std::vector<bool> vis(num_nodes, false);

  frontier.push_back(0);
  vis[0] = true;
  int visited_node_count = 0;

  while (frontier.size() > 0) {
    visited_node_count += frontier.size();

    for (auto &id : frontier) {
      int node_degree = g->get_degree(id);
      if (node_degree > 0) {
        auto node_edges = g->get_edges(id);
        for (auto &id2 : node_edges) {
          if (!vis[id2]) {
            vis[id] = true;
            next.push_back(id2);
          }
        }
      }
    }
    frontier = next;
    next.clear();
  }
  return visited_node_count;
};

int parallel_bfs_in_blocks(Graph *g) {
  int num_nodes = g->get_num_nodes();
  std::vector<std::atomic_bool> atomic_vis(num_nodes);
  for (int i = 0; i < num_nodes; i++)
    atomic_vis[i].store(false);

  frontier.push_back(0);
  atomic_vis[0] = true;

  int visited_node_count = 0;

  while (frontier.size() > 0) {
    visited_node_count += frontier.size();
    g->process_queue_in_blocks(
        frontier, next,
        [&atomic_vis](uint32_t v_id, uint32_t v_id2,
                      std::vector<uint32_t> &next_private) {
          bool is_visited = false;
          if (atomic_vis[v_id2].compare_exchange_strong(is_visited, true)) {
            next_private.push_back(v_id2);
          }
        });
    frontier = next;
    next.clear();
  }
  return visited_node_count;
}

int parallel_bfs(Graph *g) {
  int num_nodes = g->get_num_nodes();
  std::vector<std::atomic_bool> atomic_vis(num_nodes);
  for (int i = 0; i < num_nodes; i++)
    atomic_vis[i].store(false);

  frontier.push_back(0);
  atomic_vis[0] = true;

  int visited_node_count = 0;

  while (frontier.size() > 0) {
    visited_node_count += frontier.size();
    g->process_queue(
        frontier, next,
        [&atomic_vis](uint32_t v_id, uint32_t v_id2,
                      std::vector<uint32_t> &next_private) {
          bool is_visited = false;
          if (atomic_vis[v_id2].compare_exchange_strong(is_visited, true)) {
            next_private.push_back(v_id2);
          }
        });
    frontier = next;
    next.clear();
  }
  return visited_node_count;
}

std::vector<int> cache_mb_list1 = {1, 2, 3, 4, 5, 10, 25, 50};
std::vector<int> cache_mb_list2 = {20, 40, 60, 80, 100, 200, 500, 1000};
std::vector<int> cache_mb_list3 = {128,  256,  512,  1024,
                                   2048, 4096, 8192, 16384};
std::vector<int> cache_mb_list0 = {1024};
void run_cache_tests(Graph *g, std::string algo_name, int list_idx,
                     int thread_count, std::function<int(Graph *)> func) {

  std::vector<int> cache_mb_l;
  if (list_idx == 0)
    cache_mb_l = cache_mb_list0;
  else if (list_idx == 1)
    cache_mb_l = cache_mb_list1;
  else if (list_idx == 2)
    cache_mb_l = cache_mb_list2;
  else
    cache_mb_l = cache_mb_list3;

  for (int cache_mb : cache_mb_l) {
    g->set_cache_size(cache_mb);
    printf("---[Cache size: %d MB]---\n", cache_mb);

    long total_ms_int = 0;

    for (int i = 0; i < nrepeats; i++) {
      g->clear_cache();
      auto begin = std::chrono::high_resolution_clock::now();
      int res = func(g);
      auto end = std::chrono::high_resolution_clock::now();
      auto ms_int =
          std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
              .count();
      total_ms_int += ms_int;
      printf("[Test %d] %d nodes visited in %ld us.\n", i, res, ms_int);
      fprintf(log_fp, "%s,%u,%d,%ld,%d\n", algo_name.c_str(), thread_count,
              cache_mb, ms_int, res);
    }
    printf("[Total] Average time: %ld us.\n", total_ms_int / nrepeats);
  }
}

void run_thread_tests(Graph *g, std::string algo_name, int min_thread,
                      int max_thread, int cache_mb,
                      std::function<int(Graph *)> func) {
  for (int thread_count = min_thread; thread_count <= max_thread;
       thread_count *= 2) {
    g->set_thread_pool_size(thread_count);
    printf("---[Thread count: %d]---\n", thread_count);

    long total_ms_int = 0;

    for (int i = 0; i < nrepeats; i++) {
      g->clear_cache();
      auto begin = std::chrono::high_resolution_clock::now();
      int res = func(g);
      auto end = std::chrono::high_resolution_clock::now();
      auto ms_int =
          std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
              .count();
      total_ms_int += ms_int;
      printf("[Test %d] %d nodes visited in %ld us.\n", i, res, ms_int);
      fprintf(log_fp, "%s,%d,%d,%ld,%d\n", (algo_name + "_blocked").c_str(),
              thread_count, cache_mb, ms_int, res);
    }
    printf("[Total] Average time: %ld us.\n", total_ms_int / nrepeats);
  }
}

int main(int argc, char *argv[]) {

  if (argc < 3) {
    printf("[ERROR] Use case: bfs [file_path] [test case (thread, cache)] "
           "[args]\n");
    return 0;
  }

  Graph *g = new Graph();
  g->init_serializer(argv[1], MODE::SYNC_READ);
  g->init_metadata();
  g->init_vertex_data();

  std::filesystem::path file_fs_path(argv[1]);
  std::filesystem::path log_fs_path("..");
  log_fs_path = log_fs_path / "log" / file_fs_path.stem();
  log_fs_path += "_" + proj_name;

  if (strcmp(argv[2], "cache") == 0) {
    unsigned int thread_count = std::thread::hardware_concurrency();

    int test_id = 3;
    if (argc >= 4)
      test_id = atoi(argv[3]);

    log_fs_path += "_cache.csv";
    log_fp = fopen(log_fs_path.string().data(), "w");
    fprintf(log_fp, "algo_name,thread,cache_mb,time,res\n");

    g->set_cache_mode(SIMPLE_CACHE);
    run_cache_tests(g, proj_name + "_blocked", test_id, thread_count,
                    parallel_bfs_in_blocks);

    g->set_cache_mode(NORMAL_CACHE);
    run_cache_tests(g, proj_name, test_id, thread_count, parallel_bfs);

  } else if (strcmp(argv[2], "thread") == 0) {
    int cache_mb = 1024;

    int min_thread = 1;
    int max_thread = 256;

    if (argc >= 4)
      min_thread = atoi(argv[3]);
    if (argc >= 5)
      max_thread = atoi(argv[4]);
    if (argc >= 6)
      cache_mb = atoi(argv[5]);

    g->set_cache_size(cache_mb);

    log_fs_path += "_thread.csv";
    log_fp = fopen(log_fs_path.string().data(), "w");
    fprintf(log_fp, "algo_name,thread,cache_mb,time,res\n");

    g->set_cache_mode(SIMPLE_CACHE);
    run_thread_tests(g, proj_name + "_blocked", min_thread, max_thread,
                     cache_mb, parallel_bfs_in_blocks);

    // g->set_cache_mode(NORMAL_CACHE);
    // run_thread_tests(g, proj_name, min_thread, max_thread, cache_mb,
    //                  parallel_bfs);

  } else {
    printf("[ERROR] Please input test case (thread, cache).\n");
  }

  return 0;
}