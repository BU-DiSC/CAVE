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
int iterations = 5;
std::string algo_name = "pagerank";

float serial_pagerank(Graph *g, float eps = 0.01f) {
  int num_nodes = g->get_num_nodes();
  std::vector<float> pr(num_nodes);
  std::vector<float> pr_next(num_nodes);
  std::vector<bool> vis(num_nodes, false);
  int iter = 0;

  frontier.reserve(num_nodes);

  for (int i = 0; i < num_nodes; i++) {
    pr[i] = pr_next[i] = 1.f / g->get_degree(i);
    frontier.push_back(i);
  }

  while (!frontier.empty()) {
    iter++;
    for (auto &v : frontier) {
      auto edges = g->get_edges(v);
      float sum = 0.f;
      for (auto &w : edges) {
        sum += pr[w];
      }
      float pr_new = (0.15f + 0.85f * sum) / g->get_degree(v);
      if (std::abs(pr_new - pr[v]) > eps) {
        for (auto &w : edges) {
          if (!vis[w]) {
            vis[w] = true;
            next.push_back(w);
          }
        }
      }
      pr_next[v] = pr_new;
    }
    frontier = next;
    next.clear();
    pr = pr_next;
  }
  return pr[0];
};

float parallel_pagerank(Graph *g, float eps = 0.01f) {
  int num_nodes = g->get_num_nodes();
  std::vector<float> pr(num_nodes);
  std::vector<float> pr_next(num_nodes);
  std::vector<int> degrees(num_nodes);
  std::vector<std::atomic_bool> atomic_vis(num_nodes);
  int iter = 0;

  frontier.reserve(num_nodes);

  for (int i = 0; i < num_nodes; i++) {
    degrees[i] = g->get_degree(i);
    pr[i] = pr_next[i] = 1.f / degrees[i];
    frontier.push_back(i);
    atomic_vis[i].store(false);
  }

  while (!frontier.empty()) {
    iter++;
    g->process_queue(
        frontier, next, [&pr_next](uint32_t v_id) { pr_next[v_id] = 0.f; },
        [&pr, &pr_next](uint32_t v_id, uint32_t v_id2) {
          pr_next[v_id] += pr[v_id2];
        },
        [&pr_next, &degrees](uint32_t v_id) {
          pr_next[v_id] = (0.15f + 0.85f * pr_next[v_id]) / degrees[v_id];
        },
        [&atomic_vis, &pr, &pr_next,
         &eps](uint32_t v_id, uint32_t v_id2,
               std::vector<uint32_t> &next_private) {
          if (std::abs(pr_next[v_id] - pr[v_id]) > eps) {
            bool is_visited = false;
            if (atomic_vis[v_id2].compare_exchange_strong(is_visited, true)) {
              next_private.push_back(v_id2);
            }
          }
        });

    frontier = next;
    next.clear();
    pr = pr_next;
  }
  return pr[0];
}

float parallel_pagerank(Graph *g, int iteration) {
  int num_nodes = g->get_num_nodes();
  std::vector<float> pr(num_nodes);
  std::vector<float> pr_next(num_nodes);
  std::vector<int> degrees(num_nodes);

  frontier.reserve(num_nodes);

  for (int i = 0; i < num_nodes; i++) {
    degrees[i] = g->get_degree(i);
    pr[i] = pr_next[i] = 1.f / degrees[i];
    frontier.push_back(i);
  }

  while (--iteration >= 0) {
    g->process_queue(
        frontier, next, [&pr_next](uint32_t v_id) { pr_next[v_id] = 0.f; },
        [&pr, &pr_next](uint32_t v_id, uint32_t v_id2) {
          pr_next[v_id] += pr[v_id2];
        },
        [&pr_next, &degrees](uint32_t v_id) {
          pr_next[v_id] = (0.15f + 0.85f * pr_next[v_id]) / degrees[v_id];
        },
        [](uint32_t v_id, uint32_t v_id2, std::vector<uint32_t> &next_private) {
        });
    pr = pr_next;
  }
  return pr[0];
}

float parallel_pagerank_in_blocks(Graph *g, int iteration) {
  int num_nodes = g->get_num_nodes();
  std::vector<float> pr(num_nodes);
  std::vector<float> pr_next(num_nodes);
  std::vector<int> degrees(num_nodes);

  frontier.reserve(num_nodes);

  for (int i = 0; i < num_nodes; i++) {
    degrees[i] = g->get_degree(i);
    pr[i] = pr_next[i] = 1.f / degrees[i];
    frontier.push_back(i);
  }

  while (--iteration >= 0) {
    g->process_queue_in_blocks(
        frontier, next, [&pr_next](uint32_t v_id) { pr_next[v_id] = 0.f; },
        [&pr, &pr_next](uint32_t v_id, uint32_t v_id2) {
          pr_next[v_id] += pr[v_id2];
        },
        [&pr_next, &degrees](uint32_t v_id) {
          pr_next[v_id] = (0.15f + 0.85f * pr_next[v_id]) / degrees[v_id];
        },
        [](uint32_t v_id, uint32_t v_id2, std::vector<uint32_t> &next_private) {
        });
    pr = pr_next;
  }
  return pr[0];
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
  log_fs_path += "_" + algo_name;

  Graph *g = new Graph();
  g->init_serializer(argv[1], MODE::SYNC_READ);
  g->init_metadata();
  g->init_vertex_data();

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
      g->set_cache_size(cache_mb);
      printf("---[Cache size: %d MB]---\n", cache_mb);

      for (int i = 0; i < nrepeats; i++) {
        g->clear_cache();
        auto begin = std::chrono::high_resolution_clock::now();
        float res = parallel_pagerank(g);
        auto end = std::chrono::high_resolution_clock::now();
        auto ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        printf("[Test %d] Node 0 score: %.2f in %ld us.\n", i, res, ms_int);
        fprintf(log_fp, "%s,%u,%d,%ld,%.2f\n", algo_name.c_str(), thread_count,
                cache_mb, ms_int, res);
      }
    }
  } else if (strcmp(argv[2], "thread") == 0) {
    int cache_mb = 4096;

    int min_thread = 1;
    int max_thread = 256;

    if (argc >= 4)
      min_thread = atoi(argv[3]);
    if (argc >= 5)
      max_thread = atoi(argv[4]);

    g->set_cache_size(cache_mb);

    log_fs_path += "_thread.csv";
    auto log_fp = fopen(log_fs_path.string().data(), "w");
    fprintf(log_fp, "algo_name,thread,cache_mb,time,res\n");

    for (int thread_count = min_thread; thread_count <= max_thread;
         thread_count *= 2) {
      pool.reset(thread_count);
      printf("---[Thread count: %d]---\n", thread_count);

      for (int i = 0; i < nrepeats; i++) {
        g->clear_cache();
        auto begin = std::chrono::high_resolution_clock::now();
        float res = parallel_pagerank(g, iterations);
        auto end = std::chrono::high_resolution_clock::now();
        auto ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        printf("[Test %d] Node 0 score: %.2f in %ld us.\n", i, res, ms_int);
        fprintf(log_fp, "%s,%d,%d,%ld,%.2f\n", algo_name.c_str(), thread_count,
                cache_mb, ms_int, res);
      }
    }
  } else {
    printf("[ERROR] Please input test case (thread, cache).\n");
  }

  return 0;
}
