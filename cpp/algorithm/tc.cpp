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
    node_degrees[i] = g->get_node_degree(i);
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

unsigned long long serial_bfs_all(Graph *g) {
  int num_nodes = g->get_num_nodes();
  std::vector<bool> vis(num_nodes, false);

  frontier.push_back(0);
  vis[0] = true;
  unsigned long long visited_node_count = 0;

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

unsigned long long parallel_bfs_all(Graph *g) {
  int num_nodes = g->get_num_nodes();
  std::vector<std::atomic_bool> atomic_vis(num_nodes);
  for (int i = 0; i < num_nodes; i++)
    atomic_vis[i].store(false);

  frontier.push_back(0);
  atomic_vis[0] = true;

  unsigned long long visited_node_count = 0;

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

  if (strcmp(argv[2], "cache") == 0) {
    int size_mb = 4096;
    if (argc >= 4)
      size_mb = atoi(argv[3]);

    for (int cache_mb = std::max(1, size_mb / 8); cache_mb <= 2 * size_mb;
         cache_mb *= 2) {
      g->set_cache(cache_mb);
      printf("---[Cache size: %d MB]---\n", cache_mb);

      for (int i = 0; i < nrepeats; i++) {
        auto begin = std::chrono::high_resolution_clock::now();
        unsigned long long res = parallel_bfs_all(g);
        auto end = std::chrono::high_resolution_clock::now();
        auto ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        printf("[Test %d] %llu nodes visited in %ld us.\n", i, res, ms_int);
      }
    }
  } else if (strcmp(argv[2], "thread") == 0) {
    g->set_cache(0.1f);
    for (int thread_count = 1; thread_count <= 256; thread_count *= 2) {
      pool.reset(thread_count);
      printf("---[Thread count: %d]---\n", thread_count);

      for (int i = 0; i < nrepeats; i++) {
        auto begin = std::chrono::high_resolution_clock::now();
        unsigned long long res = parallel_bfs_all(g);
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