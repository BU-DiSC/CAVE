#include "../BS_thread_pool.hpp"
#include "../Graph.hpp"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <vector>

inline static BS::thread_pool pool{0};
std::vector<uint32_t> frontier;
std::vector<uint32_t> next;
std::mutex mtx;
int nrepeats = 3;

float serial_pagerank(Graph *g) {
  int num_nodes = g->get_num_nodes();
  std::vector<float> pg_score(num_nodes);
  std::vector<bool> vis(num_nodes, true);
  float eps = 0.01f;
  int iter = 0;

  frontier.reserve(num_nodes);

  for (int i = 0; i < num_nodes; i++) {
    pg_score[i] = 1.f / g->get_node_degree(i);
    frontier.push_back(i);
  }

  while (!frontier.empty()) {
    iter++;
    for (auto &v : frontier) {
      auto edges = g->get_edges(v);
      float sum = 0.f;
      for (auto &w : edges) {
        sum += pg_score[w];
      }
      float score_new = (0.15f + 0.85f * sum) / g->get_node_degree(v);
      if (std::abs(score_new - pg_score[v]) > eps) {
        for (auto &w : edges) {
          if (!vis[w]) {
            vis[w] = true;
            next.push_back(w);
          }
        }
      }
      pg_score[v] = score_new;
      vis[v] = false;
    }
    frontier = next;
    next.clear();
  }
  return pg_score[0];
};

float parallel_pagerank(Graph *g) {
  int num_nodes = g->get_num_nodes();
  std::vector<float> pg_score(num_nodes);
  std::vector<std::atomic_bool> atomic_vis(num_nodes);
  float eps = 0.01f;
  int iter = 0;

  for (int i = 0; i < num_nodes; i++) {
    pg_score[i] = 1.f / g->get_node_degree(i);
    frontier.push_back(i);
    atomic_vis[i].store(true);
  }

  while (!frontier.empty()) {
    iter++;
    pool.push_loop(frontier.size(), [&g, &atomic_vis, &pg_score,
                                     &eps](const int a, const int b) {
      for (int i = a; i < b; i++) {
        int v = frontier[i];
        auto edges = g->get_edges(v);
        std::vector<uint32_t> next_private;
        float sum = 0.f;
        for (auto &w : edges) {
          sum += pg_score[w];
        }
        float score_new = (0.15f + 0.85f * sum) / g->get_node_degree(v);
        if (std::abs(score_new - pg_score[v]) > eps) {
          for (auto &w : edges) {
            bool is_visited = false;
            if (atomic_vis[w].compare_exchange_strong(is_visited, true)) {
              next_private.push_back(w);
            }
          }
        }
        if (next_private.size() > 0) {
          std::unique_lock next_lock(mtx);
          next.insert(next.end(), next_private.begin(), next_private.end());
        }
        pg_score[v] = score_new;
        atomic_vis[v] = false;
      }
    });
    pool.wait_for_tasks();
    frontier = next;
    next.clear();
  }
  return pg_score[0];
}

int main(int argc, char *argv[]) {

  if (argc < 3) {
    printf("[ERROR] Use case: bfs [file_path] [test case (thread, cache)] "
           "[args]\n");
    return 0;
  }

  std::filesystem::path file_fs_path(argv[1]);
  std::filesystem::path log_fs_path("..");
  log_fs_path = log_fs_path / "log" / file_fs_path.stem();
  log_fs_path += "_pagerank";

  Graph *g = new Graph();
  g->init_serializer(argv[1], MODE::SYNC_READ);
  g->init_metadata();
  g->init_vertex_data();

  if (strcmp(argv[2], "cache") == 0) {
    int size_mb = 4096;
    if (argc >= 4)
      size_mb = atoi(argv[3]);
    int thread_count = std::thread::hardware_concurrency();

    log_fs_path += "_cache.csv";
    auto log_fp = fopen(log_fs_path.string().data(), "w");
    fprintf(log_fp, "algo_name,thread,cache_mb,time,res\n");

    for (int cache_mb = std::max(1, size_mb / 8); cache_mb <= 2 * size_mb;
         cache_mb *= 2) {
      g->set_cache(cache_mb);
      printf("---[Cache size: %d MB]---\n", cache_mb);

      for (int i = 0; i < nrepeats; i++) {
        auto begin = std::chrono::high_resolution_clock::now();
        float res = parallel_pagerank(g);
        auto end = std::chrono::high_resolution_clock::now();
        auto ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        printf("[Test %d] Node 0 score: %.2f in %ld us.\n", i, res, ms_int);
        fprintf(log_fp, "p_dfs,%d,%d,%ld,%.2f\n", thread_count, cache_mb,
                ms_int, res);
      }
    }
  } else if (strcmp(argv[2], "thread") == 0) {
    int cache_mb = 1024;
    g->set_cache(cache_mb);

    log_fs_path += "_thread.csv";
    auto log_fp = fopen(log_fs_path.string().data(), "w");
    fprintf(log_fp, "algo_name,thread,cache_mb,time,res\n");

    for (int thread_count = 1; thread_count <= 256; thread_count *= 2) {
      pool.reset(thread_count);
      printf("---[Thread count: %d]---\n", thread_count);

      for (int i = 0; i < nrepeats; i++) {
        auto begin = std::chrono::high_resolution_clock::now();
        float res = parallel_pagerank(g);
        auto end = std::chrono::high_resolution_clock::now();
        auto ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        printf("[Test %d] Node 0 score: %.2f in %ld us.\n", i, res, ms_int);
        fprintf(log_fp, "p_dfs,%d,%d,%ld,%.2f\n", thread_count, cache_mb,
                ms_int, res);
      }
    }
  } else {
    printf("[ERROR] Please input test case (thread, cache).\n");
  }

  return 0;
}