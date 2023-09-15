#include "../BS_thread_pool.hpp"
#include "../Graph.hpp"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

inline static BS::thread_pool pool{0};
std::vector<uint32_t> frontier;
std::vector<uint32_t> next;
std::mutex mtx;
int nrepeats = 3;

unsigned long long serial_tc(Graph *g) {
  unsigned long long res = 0;

  int num_nodes = g->get_num_nodes();

  // Sort nodes in ascending degree
  std::vector<int> node_degrees(num_nodes);
  for (int i = 0; i < num_nodes; i++) {
    node_degrees[i] = g->get_degree(i);
  }

  for (int u = 0; u < num_nodes; u++) {
    auto u_edges = g->get_edges(u);
    std::unordered_set<int> marked(u_edges.begin(), u_edges.end());

    for (auto &v : u_edges) {
      if (node_degrees[v] > node_degrees[u] ||
          (node_degrees[v] == node_degrees[u] &&
           v > u)) { // degree(v) > degree(u)
        auto v_edges = g->get_edges(v);
        for (auto &w : v_edges) {
          // degree(w) > degree(v)
          if (node_degrees[w] > node_degrees[v] ||
              (node_degrees[w] == node_degrees[v] && w > v)) {
            if (marked.find(w) != marked.end())
              res++;
          }
        }
      }
    }
  }
  return res;
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

  if (strcmp(argv[2], "cache") == 0) {
    int size_mb = 4096;
    if (argc >= 4)
      size_mb = atoi(argv[3]);

    for (int cache_mb = std::max(1, size_mb / 8); cache_mb <= 2 * size_mb;
         cache_mb *= 2) {
      g->set_cache_size(cache_mb);
      printf("---[Cache size: %d MB]---\n", cache_mb);

      for (int i = 0; i < nrepeats; i++) {
        g->clear_cache();

        auto begin = std::chrono::high_resolution_clock::now();
        unsigned long long res = serial_tc(g);
        auto end = std::chrono::high_resolution_clock::now();
        auto ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        printf("[Test %d] %llu nodes visited in %ld us.\n", i, res, ms_int);
      }
    }
  } else if (strcmp(argv[2], "thread") == 0) {
    g->set_cache_size(0.1f);
    for (int thread_count = 1; thread_count <= 256; thread_count *= 2) {
      pool.reset(thread_count);
      printf("---[Thread count: %d]---\n", thread_count);

      for (int i = 0; i < nrepeats; i++) {
        g->clear_cache();

        auto begin = std::chrono::high_resolution_clock::now();
        unsigned long long res = serial_tc(g);
        auto end = std::chrono::high_resolution_clock::now();
        auto ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        printf("[Test %d] %llu nodes visited in %ld us.\n", i, res, ms_int);
      }
    }
  } else {
    printf("[ERROR] Please input test case (thread, cache).\n");
  }

  return 0;
}