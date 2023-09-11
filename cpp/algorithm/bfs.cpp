#include "../BS_thread_pool.hpp"
#include "../Graph.hpp"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

inline static BS::thread_pool pool{0};
std::vector<uint32_t> frontier;
std::vector<uint32_t> next;
std::mutex mtx;
int nrepeats = 3;
std::string algo_name = "bfs";

int serial_bfs_all(Graph *g) {
  int num_nodes = g->get_num_nodes();
  std::vector<bool> vis(num_nodes, false);

  frontier.push_back(0);
  vis[0] = true;
  int visited_node_count = 0;

  while (frontier.size() > 0) {
    visited_node_count += frontier.size();

    for (auto &id : frontier) {
      int node_degree = g->get_node_degree(id);
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

int parallel_bfs_all(Graph *g) {
  int num_nodes = g->get_num_nodes();
  std::vector<std::atomic_bool> atomic_vis(num_nodes);
  for (int i = 0; i < num_nodes; i++)
    atomic_vis[i].store(false);

  frontier.push_back(0);
  atomic_vis[0] = true;

  int visited_node_count = 0;

  while (frontier.size() > 0) {
    visited_node_count += frontier.size();
    pool.push_loop(
        frontier.size(), [&g, &atomic_vis](const int a, const int b) {
          for (int i = a; i < b; i++) {
            int id = frontier[i];
            int node_degree = g->get_node_degree(id);
            if (node_degree == 0)
              continue;

            auto node_edges = g->get_edges(id);
            std::vector<uint32_t> next_private;
            for (auto &id2 : node_edges) {
              bool is_visited = false;
              if (atomic_vis[id2].compare_exchange_strong(is_visited, true)) {
                next_private.push_back(id2);
              }
            }

            if (next_private.size() > 0) {
              std::unique_lock next_lock(mtx);
              next.insert(next.end(), next_private.begin(), next_private.end());
            }
          }
        });
    pool.wait_for_tasks();
    frontier = next;
    next.clear();
  }
  return visited_node_count;
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
  log_fs_path += "_" + algo_name;

  if (strcmp(argv[2], "cache") == 0) {
    int min_size_mb = 1024;
    int max_size_mb = 8 * min_size_mb;

    unsigned int thread_count = std::thread::hardware_concurrency();

    if (argc >= 4)
      min_size_mb = atoi(argv[3]);
    if (argc >= 5)
      max_size_mb = atoi(argv[4]);

    log_fs_path += "_cache.csv";
    auto log_fp = fopen(log_fs_path.string().data(), "w");
    fprintf(log_fp, "algo_name,thread,cache_mb,time,res\n");

    for (int cache_mb = std::max(64, min_size_mb); cache_mb <= max_size_mb;
         cache_mb *= 2) {
      g->set_cache(cache_mb);
      printf("---[Cache size: %d MB]---\n", cache_mb);

      long total_ms_int = 0;

      for (int i = 0; i < nrepeats; i++) {
        g->clear_cache();
        auto begin = std::chrono::high_resolution_clock::now();
        int res = parallel_bfs_all(g);
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
  } else if (strcmp(argv[2], "thread") == 0) {
    int cache_mb = 4096;

    int min_thread = 1;
    int max_thread = 256;

    if (argc >= 4)
      min_thread = atoi(argv[3]);
    if (argc >= 5)
      max_thread = atoi(argv[4]);

    g->set_cache(cache_mb);

    log_fs_path += "_thread.csv";
    auto log_fp = fopen(log_fs_path.string().data(), "w");
    fprintf(log_fp, "algo_name,thread,cache_mb,time,res\n");

    for (int thread_count = min_thread; thread_count <= max_thread;
         thread_count *= 2) {
      pool.reset(thread_count);
      printf("---[Thread count: %d]---\n", thread_count);

      long total_ms_int = 0;

      for (int i = 0; i < nrepeats; i++) {
        g->clear_cache();
        auto begin = std::chrono::high_resolution_clock::now();
        int res = parallel_bfs_all(g);
        auto end = std::chrono::high_resolution_clock::now();
        auto ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        total_ms_int += ms_int;
        printf("[Test %d] %d nodes visited in %ld us.\n", i, res, ms_int);
        fprintf(log_fp, "%s,%d,%d,%ld,%d\n", algo_name.c_str(), thread_count,
                cache_mb, ms_int, res);
        printf("[Total] Average time: %ld us.\n", total_ms_int / nrepeats);
      }
    }
  } else {
    printf("[ERROR] Please input test case (thread, cache).\n");
  }

  return 0;
}