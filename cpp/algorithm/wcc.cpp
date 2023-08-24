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

int serial_wcc(Graph *g) {
  int num_nodes = g->get_num_nodes();
  std::vector<int> wcc_id_vec(num_nodes, -1);
  int num_wccs = 0;

  for (int id = 0; id < num_nodes; id++) {
    if (wcc_id_vec[id] != -1)
      continue;
    num_wccs++;
    wcc_id_vec[id] = id;
    frontier.push_back(id);

    while (!frontier.empty()) {
      for (int j = 0; j < frontier.size(); j++) {
        int node_id = frontier[j];
        auto node_edges = g->get_edges(node_id);
        for (int k = 0; k < node_edges.size(); k++) {
          if (wcc_id_vec[node_edges[k]] == -1) {
            wcc_id_vec[node_edges[k]] = id;
            next.push_back(node_edges[k]);
          }
        }
      }
      frontier = next;
      next.clear();
    }
  }

  return num_wccs;
};

int parallel_wcc(Graph *g) {
  int num_nodes = g->get_num_nodes();
  std::vector<std::atomic_int> wcc_id_vec(num_nodes);

  for (int i = 0; i < num_nodes; i++)
    wcc_id_vec[i] = -1;
  std::atomic<int> num_wccs = 0;

  for (int id = 0; id < num_nodes; id++) {
    if (wcc_id_vec[id] != -1)
      continue;
    num_wccs++;
    wcc_id_vec[id] = id;
    frontier.push_back(id);

    while (!frontier.empty()) {
      pool.push_loop(frontier.size(), [&g, &wcc_id_vec, &id](const int a,
                                                             const int b) {
        for (int j = a; j < b; j++) {
          std::vector<uint32_t> next_private;

          int node_id = frontier[j];
          auto node_edges = g->get_edges(node_id);
          for (int k = 0; k < node_edges.size(); k++) {
            if (wcc_id_vec[node_edges[k]].exchange(id) == -1) {
              next_private.push_back(node_edges[k]);
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
  }

  return num_wccs;
}

int main(int argc, char *argv[]) {

  if (argc < 3) {
    printf("[ERROR] Use case: wcc [file_path] [test case (thread, cache)] "
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
  log_fs_path += "_wcc";

  if (strcmp(argv[2], "cache") == 0) {
    int size_mb = 4096;
    int thread_count = std::thread::hardware_concurrency();
    if (argc >= 4)
      size_mb = atoi(argv[3]);

    log_fs_path += "_cache.csv";
    auto log_fp = fopen(log_fs_path.string().data(), "w");
    fprintf(log_fp, "algo_name,thread,cache_mb,time,res\n");

    for (int cache_mb = std::max(1, size_mb / 8); cache_mb <= 2 * size_mb;
         cache_mb *= 2) {
      g->set_cache(cache_mb);
      printf("---[Cache size: %d MB]---\n", cache_mb);

      for (int i = 0; i < nrepeats; i++) {
        g->clear_cache();
        auto begin = std::chrono::high_resolution_clock::now();
        int res = parallel_wcc(g);
        auto end = std::chrono::high_resolution_clock::now();
        auto ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        printf("[Test %d] %d wcc components in %ld us.\n", i, res, ms_int);
        fprintf(log_fp, "p_bfs,%d,%d,%ld,%d\n", thread_count, cache_mb, ms_int,
                res);
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
        g->clear_cache();
        auto begin = std::chrono::high_resolution_clock::now();
        int res = parallel_wcc(g);
        auto end = std::chrono::high_resolution_clock::now();
        auto ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        printf("[Test %d] %d wcc components in %ld us.\n", i, res, ms_int);
        fprintf(log_fp, "p_bfs,%d,%d,%ld,%d\n", thread_count, cache_mb, ms_int,
                res);
      }
    }
  } else {
    printf("[ERROR] Please input test case (thread, cache).\n");
  }

  return 0;
}