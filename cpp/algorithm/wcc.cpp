#include "../BS_thread_pool.hpp"
#include "../Graph.hpp"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <vector>

std::vector<uint32_t> frontier;
std::vector<uint32_t> next;
std::mutex mtx;
int nrepeats = 3;
std::string proj_name = "wcc";
FILE *log_fp;

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
  int num_wccs = 0;

  for (int id = 0; id < num_nodes; id++) {
    if (wcc_id_vec[id] != -1)
      continue;
    num_wccs++;
    wcc_id_vec[id] = id;
    frontier.push_back(id);

    while (!frontier.empty()) {
      g->process_queue(
          frontier, next,
          [&id, &wcc_id_vec](uint32_t v_id, uint32_t v_id2,
                             std::vector<uint32_t> &next_private) {
            int val = -1;
            if (wcc_id_vec[v_id2].compare_exchange_strong(val, id)) {
              next_private.push_back(v_id2);
            }
          });
      frontier = next;
      next.clear();
    }
  }

  return num_wccs;
}

int parallel_wcc_in_blocks(Graph *g) {
  int num_nodes = g->get_num_nodes();
  std::vector<std::atomic_int> wcc_id_vec(num_nodes);

  for (int i = 0; i < num_nodes; i++)
    wcc_id_vec[i] = -1;
  int num_wccs = 0;

  for (int id = 0; id < num_nodes; id++) {
    if (wcc_id_vec[id] != -1)
      continue;
    num_wccs++;
    wcc_id_vec[id] = id;
    frontier.push_back(id);

    while (!frontier.empty()) {
      g->process_queue_in_blocks(
          frontier, next,
          [&id, &wcc_id_vec](uint32_t v_id, uint32_t v_id2,
                             std::vector<uint32_t> &next_private) {
            int val = -1;
            if (wcc_id_vec[v_id2].compare_exchange_strong(val, id)) {
              next_private.push_back(v_id2);
            }
          });
      frontier = next;
      next.clear();
    }
  }

  return num_wccs;
}

int parallel_wcc_hashmin(Graph *g) {
  int num_nodes = g->get_num_nodes();
  std::vector<uint32_t> hash_min(num_nodes);
  std::vector<std::atomic_uint32_t> hash_min_next(num_nodes);
  std::vector<std::atomic_bool> flags(num_nodes);

  for (int i = 0; i < num_nodes; i++) {
    hash_min[i] = i;
    hash_min_next[i].store(i);
    frontier.push_back(i);
  }

  while (!frontier.empty()) {
    for (int i = 0; i < num_nodes; i++)
      flags[i].store(false);

    g->process_queue(frontier, next,
                     [&hash_min, &hash_min_next,
                      &flags](uint32_t v_id, std::vector<uint32_t> &neighbors,
                              std::vector<uint32_t> &next_private) {
                       for (auto v_id2 : neighbors) {
                         uint32_t val = hash_min_next[v_id2].load();
                         while (hash_min[v_id] < val) {
                           if (hash_min_next[v_id2].compare_exchange_strong(
                                   val, hash_min[v_id])) {
                             if (flags[v_id2].exchange(true) == false) {
                               next_private.push_back(v_id2);
                             }
                             break;
                           }
                         }
                       }
                     });
    frontier = next;
    next.clear();
    for (int i = 0; i < num_nodes; i++) {
      hash_min[i] = hash_min_next[i].load();
    }
  }

  std::vector<uint32_t> wcc_size(num_nodes, 0);
  int num_wccs = 0;
  for (int i = 0; i < num_nodes; i++) {
    if (wcc_size[hash_min[i]]++ == 0)
      num_wccs++;
  }

  return num_wccs;
}

int parallel_wcc_hashmin_in_blocks(Graph *g) {
  int num_nodes = g->get_num_nodes();
  std::vector<uint32_t> hash_min(num_nodes);
  std::vector<std::atomic_uint32_t> hash_min_next(num_nodes);
  std::vector<std::atomic_bool> flags(num_nodes);

  for (int i = 0; i < num_nodes; i++) {
    hash_min[i] = i;
    hash_min_next[i].store(i);
    frontier.push_back(i);
  }

  while (!frontier.empty()) {
    for (int i = 0; i < num_nodes; i++)
      flags[i].store(false);

    g->process_queue_in_blocks(
        frontier, next,
        [&hash_min, &hash_min_next,
         &flags](uint32_t v_id, std::vector<uint32_t> &neighbors,
                 std::vector<uint32_t> &next_private) {
          for (auto v_id2 : neighbors) {
            uint32_t val = hash_min_next[v_id2].load();
            while (hash_min[v_id] < val) {
              if (hash_min_next[v_id2].compare_exchange_strong(
                      val, hash_min[v_id])) {
                if (flags[v_id2].exchange(true) == false) {
                  next_private.push_back(v_id2);
                }
                break;
              }
            }
          }
        });
    frontier = next;
    next.clear();
    for (int i = 0; i < num_nodes; i++) {
      hash_min[i] = hash_min_next[i].load();
    }
  }

  std::vector<uint32_t> wcc_size(num_nodes, 0);
  int num_wccs = 0;
  for (int i = 0; i < num_nodes; i++) {
    if (wcc_size[hash_min[i]]++ == 0)
      num_wccs++;
  }

  return num_wccs;
}

void run_cache_tests(Graph *g, std::string algo_name, int min_mb, int max_mb,
                     int thread_count, std::function<int(Graph *)> func) {
  for (int cache_mb = min_mb; cache_mb <= max_mb; cache_mb *= 2) {
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
      printf("[Test %d] %d wcc components found in %ld us.\n", i, res, ms_int);
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

    for (int i = 0; i < nrepeats; i++) {
      g->clear_cache();
      auto begin = std::chrono::high_resolution_clock::now();
      int res = parallel_wcc(g);
      auto end = std::chrono::high_resolution_clock::now();
      auto ms_int =
          std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
              .count();
      printf("[Test %d] %d wcc components found in %ld us.\n", i, res, ms_int);
      fprintf(log_fp, "%s,%d,%d,%ld,%d\n", algo_name.c_str(), thread_count,
              cache_mb, ms_int, res);
    }
  }
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
  log_fs_path += "_" + proj_name;

  if (strcmp(argv[2], "cache") == 0) {
    int min_mb = 1024;
    int max_mb = 8 * min_mb;

    unsigned int thread_count = std::thread::hardware_concurrency();

    if (argc >= 4)
      min_mb = atoi(argv[3]);
    if (argc >= 5)
      max_mb = atoi(argv[4]);

    log_fs_path += "_cache.csv";
    log_fp = fopen(log_fs_path.string().data(), "w");
    fprintf(log_fp, "algo_name,thread,cache_mb,time,res\n");

    g->set_cache_mode(SIMPLE_CACHE);
    run_cache_tests(g, proj_name + "_blocked", min_mb, max_mb, thread_count,
                    parallel_wcc_in_blocks);

    g->set_cache_mode(NORMAL_CACHE);
    run_cache_tests(g, proj_name, min_mb, max_mb, thread_count, parallel_wcc);

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

    g->set_cache_mode(SIMPLE_CACHE);
    run_thread_tests(g, proj_name + "_blocked", min_thread, max_thread,
                     cache_mb, parallel_wcc_in_blocks);
    g->set_cache_mode(NORMAL_CACHE);
    run_thread_tests(g, proj_name, min_thread, max_thread, cache_mb,
                     parallel_wcc);

  } else {
    printf("[ERROR] Please input test case (thread, cache).\n");
  }

  return 0;
}