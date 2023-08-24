#include "../BS_thread_pool.hpp"
#include "../Graph.hpp"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#define MAX_ACTIVE_STACKS 256

inline static BS::thread_pool pool{0};
std::vector<uint32_t> frontier;
std::vector<uint32_t> next;
std::mutex mtx;
int nrepeats = 3;

int serial_dfs_all(Graph *g) {
  int num_nodes = g->get_num_nodes();
  std::vector<bool> vis(num_nodes, false);

  frontier.push_back(0);
  vis[0] = true;
  int visited_nodes = 0;

  while (!frontier.empty()) {
    visited_nodes += frontier.size();
    int id = frontier.back();
    frontier.pop_back();

    auto node_edges = g->get_edges(id);
    for (auto &u : node_edges) {
      if (!vis[u]) {
        vis[u] = true;
        frontier.push_back(u);
      }
    }
  }

  return visited_nodes;
};

std::atomic_int num_free_stacks;
std::atomic_int visited_nodes;
std::vector<std::atomic_bool> atomic_vis;
int max_stack_size = 4;

void p_dfs_task(Graph *&g, std::vector<int> &stack) {
  int private_visited_nodes = 0;
  while (!stack.empty()) {
    private_visited_nodes++;
    // Read stack top
    int id = stack.back();

    // Pop stack top
    stack.pop_back();

    // Insert its children
    auto node_edges = g->get_edges(id);
    for (auto &id2 : node_edges) {
      bool is_visited = atomic_vis[id2].exchange(true);
      if (!is_visited)
        stack.push_back(id2);
    }

    // If stack size larger than max_stack_size:
    while (stack.size() > (size_t)max_stack_size) {
      // Try to get a stack...
      if (--num_free_stacks >= 0) {
        // Yes, split in two
        std::vector<int> stack_new(stack.begin() + stack.size() / 2,
                                   stack.end());
        stack.resize(stack.size() / 2);
        pool.push_task(&p_dfs_task, std::ref(g), stack_new);
      } else {
        num_free_stacks++;
        break;
      }
    }
  }
  num_free_stacks++;
  visited_nodes += private_visited_nodes;
}

int parallel_dfs_all(Graph *g) {
  std::vector<int> init_stack;
  num_free_stacks.store(MAX_ACTIVE_STACKS);
  visited_nodes.store(0);

  int num_nodes = g->get_num_nodes();
  atomic_vis = std::vector<std::atomic_bool>(num_nodes);
  for (int i = 0; i < num_nodes; i++)
    atomic_vis[i].store(false);

  atomic_vis[0] = true;
  init_stack.push_back(0);
  num_free_stacks--;

  pool.push_task(&p_dfs_task, std::ref(g), std::ref(init_stack));
  pool.wait_for_tasks();

  return visited_nodes.load();
}

int main(int argc, char *argv[]) {

  if (argc < 3) {
    printf("[ERROR] Use case: dfs [file_path] [test case (thread, cache)] "
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
        unsigned long long res = parallel_dfs_all(g);
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
        unsigned long long res = parallel_dfs_all(g);
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