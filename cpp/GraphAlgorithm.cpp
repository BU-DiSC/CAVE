#include "GraphAlgorithm.hpp"
#include <algorithm>
#include <cstdio>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

GraphAlgorithm::GraphAlgorithm(std::string _path, MODE mode) {
  eng.seed(42);
  g = new Graph();
  path = _path;

  // Init graph data
  g->init_serializer(path, mode);
  g->init_metadata();
  g->init_vertex_data();

  num_nodes = g->get_num_nodes();

  // Random gen for node index
  distIndex = std::uniform_int_distribution<>(0, num_nodes);
  for (int i = 0; i < 8; i++) {
    int id = distIndex(eng);
    start_ids.push_back(id);
  }

  // Initialize visited array
  vis = std::vector<bool>(num_nodes, false);
  atomic_vis = std::vector<std::atomic<bool>>(num_nodes);

  // Init atomic_vis
  for (int i = 0; i < num_nodes; i++)
    std::atomic_init(&atomic_vis[i], false);

  // Reset thread pool
  pool.reset();

  // Read vertex block list
}

void GraphAlgorithm::clear() {
  frontier.clear();
  for (int i = 0; i < num_nodes; i++) {
    vis[i] = false;
    atomic_vis[i] = false;
  }
  is_found = false;
  g->clear_cache();
}

void GraphAlgorithm::clear_stack() {
  for (int i = 0; i < MAX_ACTIVE_STACKS; i++)
    stacks[i].clear();
  free_stacks.clear();
}

void GraphAlgorithm::set_mode(MODE mode) {
  g->clear_serializer();
  g->init_serializer(path, mode);
  g->init_metadata();
  g->clear_cache();
}

void GraphAlgorithm::disable_cache() { g->disable_cache(); }

void GraphAlgorithm::set_cache_ratio(double _cache_ratio) {
  g->set_cache(_cache_ratio);
}

void GraphAlgorithm::set_cache_size(int _cache_size) {
  g->set_cache(_cache_size);
}

void GraphAlgorithm::set_cache_mode(CACHE_MODE c_mode) {
  printf("[INFO] Cache mode set: ");
  switch (c_mode) {
  case CACHE_MODE::ALL_CACHE:
    printf("All cache.\n");
    break;
  case CACHE_MODE::SMALL_CACHE:
    printf("Small cache.\n");
    break;
  default:
    printf("Unknown type, error.\n");
    return;
  }
  g->set_cache_mode(c_mode);
}

int GraphAlgorithm::s_WCC() {
  clear();
  std::vector<int> wcc_id(num_nodes, -1);
  int num_wccs = 0;

  for (int id = 0; id < num_nodes; id++) {
    if (wcc_id[id] != -1)
      continue;
    num_wccs++;
    wcc_id[id] = id;
    frontier.push_back(id);

    while (!frontier.empty()) {
      for (int j = 0; j < frontier.size(); j++) {
        int node_id = frontier[j];
        auto node_edges = g->get_edges(node_id);
        for (int k = 0; k < node_edges.size(); k++) {
          if (wcc_id[node_edges[k]] == -1) {
            wcc_id[node_edges[k]] = id;
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

int GraphAlgorithm::p_WCC() {
  clear();
  std::vector<std::atomic<int>> wcc_id(num_nodes);
  for (int i = 0; i < num_nodes; i++)
    wcc_id[i] = -1;
  std::atomic<int> num_wccs = 0;

  for (int id = 0; id < num_nodes; id++) {
    if (wcc_id[id] != -1)
      continue;
    num_wccs++;
    wcc_id[id] = id;
    frontier.push_back(id);

    while (!frontier.empty()) {
      pool.push_loop(frontier.size(), [this, &wcc_id, &id](const int a,
                                                           const int b) {
        for (int j = a; j < b; j++) {
          std::vector<int> next_private;

          int node_id = frontier[j];
          auto node_edges = g->get_edges(node_id);
          for (int k = 0; k < node_edges.size(); k++) {
            if (wcc_id[node_edges[k]].exchange(id) == -1) {
              next_private.push_back(node_edges[k]);
            }
          }

          if (next_private.size() > 0) {
            mtx.lock();
            next.insert(next.end(), next_private.begin(), next_private.end());
            mtx.unlock();
          }
        }
      });
      pool.wait_for_tasks();
      frontier = next;
      next.clear();
    }
  }

  return num_wccs;
};

int GraphAlgorithm::s_WCC_alt() {
  clear();
  std::vector<int> wcc_id(num_nodes);
  std::vector<int> recv_id(num_nodes);
  std::vector<bool> is_updated(num_nodes, true);
  for (int i = 0; i < num_nodes; i++) {
    wcc_id[i] = recv_id[i] = i;
  }

  while (1) {
    // Broadcast labels
    for (int id1 = 0; id1 < num_nodes; id1++) {
      // If node id1 is updated last iteration
      if (!is_updated[id1])
        continue;
      // Read edges
      auto node_edges = g->get_edges(id1);
      // Broadcast
      for (int j = 0; j < node_edges.size(); j++) {
        int id2 = node_edges[j];
        // Maintain minimal WCC labels transferred to id2
        if (wcc_id[id1] < recv_id[id2])
          recv_id[id2] = wcc_id[id1];
      }
    }
    int count_updates = 0;
    // Update WCC id
    for (int id1 = 0; id1 < num_nodes; id1++) {
      if (recv_id[id1] < wcc_id[id1]) {
        wcc_id[id1] = recv_id[id1];
        is_updated[id1] = true;
        count_updates++;
      } else {
        is_updated[id1] = false;
      }
    }
    // No node is updated, finished.
    if (count_updates == 0)
      break;
  }

  int num_wccs = 0;
  std::vector<bool> distinct_wcc(num_nodes, false);
  for (int id1 = 0; id1 < num_nodes; id1++) {
    if (!distinct_wcc[wcc_id[id1]]) {
      num_wccs++;
      distinct_wcc[wcc_id[id1]] = true;
    }
  }

  return num_wccs;
};

int GraphAlgorithm::p_WCC_alt() {
  clear();
  std::vector<int> wcc_id(num_nodes);
  std::vector<std::atomic<int>> min_recv_id(num_nodes);
  std::vector<int> updated_nodes;

  for (int i = 0; i < num_nodes; i++) {
    wcc_id[i] = min_recv_id[i] = i;
    updated_nodes.push_back(i);
  }

  while (1) {
    // Broadcast labels in updated_nodes list
    pool.push_loop(updated_nodes.size(),
                   [this, &updated_nodes, &wcc_id, &min_recv_id](const int a,
                                                                 const int b) {
                     for (int i = a; i < b; i++) {
                       int id1 = updated_nodes[i];
                       // Read edges
                       auto node_edges = g->get_edges(id1);
                       // Broadcast
                       for (int j = 0; j < node_edges.size(); j++) {
                         int id2 = node_edges[j];
                         int stored_min = min_recv_id[id2];
                         while (wcc_id[id1] < stored_min) {
                           if (min_recv_id[id2].compare_exchange_strong(
                                   stored_min, wcc_id[id1])) {
                             break;
                           }
                         }
                       }
                     }
                   });
    pool.wait_for_tasks();

    updated_nodes.clear();
    for (int id1 = 0; id1 < num_nodes; id1++) {
      if (min_recv_id[id1] < wcc_id[id1]) {
        wcc_id[id1] = min_recv_id[id1];
        updated_nodes.push_back(id1);
      }
    }

    if (updated_nodes.size() == 0)
      break;
  }
  int num_wccs = 0;
  std::vector<bool> distinct_wcc(num_nodes, false);
  for (int id1 = 0; id1 < num_nodes; id1++) {
    if (!distinct_wcc[wcc_id[id1]]) {
      num_wccs++;
      distinct_wcc[wcc_id[id1]] = true;
    }
  }
  return num_wccs;
};

int GraphAlgorithm::p_WCC_alt2() {
  clear();
  std::vector<int> wcc_id(num_nodes);
  std::vector<std::atomic<int>> min_recv_id(num_nodes);
  std::vector<bool> is_updated(num_nodes, true);

  for (int i = 0; i < num_nodes; i++) {
    wcc_id[i] = min_recv_id[i] = i;
  }

  while (1) {
    // Broadcast labels in updated_nodes list

    pool.push_loop(num_nodes, [this, &is_updated, &wcc_id,
                               &min_recv_id](const int a, const int b) {
      for (int id1 = a; id1 < b; id1++) {
        if (!is_updated[id1])
          continue;
        // Read edges
        auto node_edges = g->get_edges(id1);
        // Broadcast
        for (int j = 0; j < node_edges.size(); j++) {
          int id2 = node_edges[j];
          int stored_min = min_recv_id[id2];
          while (wcc_id[id1] < stored_min) {
            if (min_recv_id[id2].compare_exchange_strong(stored_min,
                                                         wcc_id[id1])) {
              break;
            }
          }
        }
      }
    });
    pool.wait_for_tasks();

    std::atomic<int> updates_count = 0;

    pool.push_loop(num_nodes, [&updates_count, &is_updated, &wcc_id,
                               &min_recv_id](const int a, const int b) {
      for (int id1 = a; id1 < b; id1++) {
        if (min_recv_id[id1] < wcc_id[id1]) {
          wcc_id[id1] = min_recv_id[id1];
          is_updated[id1] = true;
          updates_count++;
        } else {
          is_updated[id1] = false;
        }
      }
    });
    pool.wait_for_tasks();

    if (updates_count == 0)
      break;
  }

  std::unordered_set<int> wccs;
  for (int id1 = 0; id1 < num_nodes; id1++) {
    wccs.insert(wcc_id[id1]);
  }
  int num_wccs = wccs.size();

  return num_wccs;
};

bool GraphAlgorithm::s_bfs_alt() {
  clear();
  frontier.push_back(start_id);

  while (!is_found && frontier.size() > 0) {
    for (size_t i = 0; i < frontier.size(); i++) {
      int id = frontier[i];
      if (vis[id])
        continue;
      vis[id] = true;
      int node_key = g->get_node_key(id);
      int node_degree = g->get_node_degree(id);
      if (node_key == key) {
        is_found = true;
        break;
      }
      if (node_degree > 0) {
        auto node_edges = g->get_edges(id);
        next.insert(next.end(), node_edges.begin(), node_edges.end());
      }
    }
    frontier = next;
    next.clear();
  }
  return is_found;
}

bool GraphAlgorithm::s_bfs() {
  clear();
  frontier.push_back(start_id);
  vis[start_id] = true;

  while (!is_found && frontier.size() > 0) {
    for (size_t i = 0; i < frontier.size(); i++) {
      int id = frontier[i];
      int node_key = g->get_node_key(id);
      int node_degree = g->get_node_degree(id);
      if (node_key == key) {
        is_found = true;
        break;
      }
      if (node_degree > 0) {
        auto edges = g->get_edges(id);
        for (auto j = 0; j < node_degree; j++) {
          int id = edges[j];
          if (!vis[id]) {
            vis[id] = true;
            next.push_back(id);
          }
        }
      }
    }
    frontier = next;
    next.clear();
  }
  return is_found;
}

bool GraphAlgorithm::p_bfs_alt() {
  clear();
  frontier.push_back(start_id);

  while (!is_found && frontier.size() > 0) {
    pool.push_loop(frontier.size(), [this](const int a, const int b) {
      for (int i = a; i < b; i++) {
        if (is_found)
          continue;
        int id = frontier[i];
        bool is_visited = atomic_vis[id].exchange(true);
        if (is_visited)
          continue;
        int node_key = g->get_node_key(id);
        int node_degree = g->get_node_degree(id);
        if (node_key == key) {
          is_found = true;
          continue;
        }
        if (node_degree == 0)
          continue;
        auto edges = g->get_edges(id);

        mtx.lock();
        next.insert(next.end(), edges.begin(), edges.end());
        mtx.unlock();
      }
    });
    pool.wait_for_tasks();
    frontier = next;
    next.clear();
  }
  return is_found;
}

bool GraphAlgorithm::p_bfs() {
  clear();
  frontier.push_back(start_id);
  atomic_vis[start_id] = true;

  while (!is_found && frontier.size() > 0) {
    pool.push_loop(frontier.size(), [this](const int a, const int b) {
      for (int i = a; i < b; i++) {
        if (is_found)
          continue;
        int id1 = frontier[i];
        int node_key = g->get_node_key(id1);
        int node_degree = g->get_node_degree(id1);
        if (node_key == key) {
          is_found = true;
          continue;
        }
        if (node_degree == 0)
          continue;

        std::vector<int> node_edges = g->get_edges(id1);

        std::vector<int> next_private;
        for (auto j = 0; j < node_degree; j++) {
          int id2 = node_edges[j];
          bool is_visited = atomic_vis[id2].exchange(true);
          if (!is_visited) {
            next_private.push_back(id2);
          }
        }

        // Not found yet & next_private is not empty
        if (!is_found && next_private.size() > 0) {
          mtx.lock();
          next.insert(next.end(), next_private.begin(), next_private.end());
          mtx.unlock();
        }
      }
    });
    pool.wait_for_tasks();
    frontier = next;
    next.clear();
  }
  return is_found;
}

bool GraphAlgorithm::p_bfs_all() {
  clear();
  frontier.push_back(start_id);
  atomic_vis[start_id] = true;

  while (!is_found && frontier.size() > 0) {
    pool.push_loop(frontier.size(), [this](const int a, const int b) {
      for (int i = a; i < b; i++) {
        int id = frontier[i];
        int node_degree = g->get_node_degree(id);
        if (node_degree == 0)
          continue;

        std::vector<int> node_edges = g->get_edges(id);
        std::vector<int> next_private;
        for (auto j = 0; j < node_degree; j++) {
          int id = node_edges[j];
          bool is_visited = atomic_vis[id].exchange(true);
          if (!is_visited) {
            next_private.push_back(id);
          }
        }

        // Not found yet & next_private is not empty
        if (!is_found && next_private.size() > 0) {
          mtx.lock();
          next.insert(next.end(), next_private.begin(), next_private.end());
          mtx.unlock();
        }
      }
    });
    pool.wait_for_tasks();
    frontier = next;
    next.clear();
  }
  return true;
}

bool GraphAlgorithm::s_dfs() {
  clear();

  frontier.push_back(start_id);
  while (!frontier.empty()) {
    int id = frontier.back();
    if (vis[id]) {
      frontier.pop_back();
      continue;
    }
    vis[id] = true;

    int node_key = g->get_node_key(id);

    if (node_key == key)
      return true;

    frontier.pop_back();

    auto node_edges = g->get_edges(id);
    frontier.insert(frontier.end(), node_edges.begin(), node_edges.end());
  }
  return false;
}

void GraphAlgorithm::p_dfs_task(std::vector<int> &stack) {
  while (!stack.empty()) {
    if (is_found)
      break;
    // Read stack top
    int id = stack.back();
    int node_key = g->get_node_key(id);
    int node_degree = g->get_node_degree(id);

    // Found it?
    if (node_key == key)
      is_found = true;
    if (is_found)
      break;

    // Pop stack top
    stack.pop_back();

    // Insert its children
    auto node_edges = g->get_edges(id);
    for (int i = 0; i < node_degree; i++) {
      int id2 = node_edges[i];
      bool is_visited = atomic_vis[id2].exchange(true);
      if (!is_visited)
        stack.push_back(id2);
    }

    // If stack size larger than max_stack_size:
    while (stack.size() > (size_t)max_stack_size) {
      if (is_found)
        break;
      // Try to get a stack...
      int tmp = num_free_stacks.fetch_sub(1);
      if (tmp >= 1) {
        // Yes, split in two
        std::vector<int> stack_new(stack.begin() + stack.size() / 2,
                                   stack.end());
        stack.resize(stack.size() / 2);
        pool.push_task(&GraphAlgorithm::p_dfs_task, this, stack_new);
      } else {
        num_free_stacks++;
        break;
      }
    }
  }
  num_free_stacks++;
}

bool GraphAlgorithm::p_dfs() {
  clear();
  std::vector<int> init_stack;
  num_free_stacks = MAX_ACTIVE_STACKS;

  atomic_vis[start_id] = true;
  init_stack.push_back(start_id);
  num_free_stacks--;

  pool.push_task(&GraphAlgorithm::p_dfs_task, this, std::ref(init_stack));

  pool.wait_for_tasks();
  return is_found;
}

unsigned long long GraphAlgorithm::s_triangle_count_alt() {
  clear();
  unsigned long long res = 0;

  // Sort nodes in ascending degree
  std::vector<int> node_degrees(num_nodes);
  std::vector<int> node_indexes(num_nodes);
  std::vector<int> rev_node_indexes(num_nodes);
  for (int i = 0; i < num_nodes; i++) {
    node_indexes[i] = i;
    node_degrees[i] = g->get_node_degree(i);
  }

  std::sort(node_indexes.begin(), node_indexes.end(),
            [&node_degrees](int &a, int &b) {
              return node_degrees[a] < node_degrees[b];
            });

  for (int i = 0; i < num_nodes; i++) {
    rev_node_indexes[node_indexes[i]] = i;
  }

  for (int u = 0; u < num_nodes; u++) {
    auto u_edges = g->get_edges(u);
    std::unordered_set<int> marked(u_edges.begin(), u_edges.end());

    for (int v : u_edges) {
      if (rev_node_indexes[v] > rev_node_indexes[u]) { // degree(v) > degree(u)
        auto v_edges = g->get_edges(v);
        for (int w : v_edges) {
          // degree(w) > degree(v)
          if (rev_node_indexes[w] > rev_node_indexes[v]) {
            if (marked.find(w) != marked.end())
              res++;
          }
        }
      }
    }
  }
  return res;
}

unsigned long long GraphAlgorithm::s_triangle_count() {
  clear();
  unsigned long long res = 0;

  // Sort nodes in ascending degree
  std::vector<int> node_degrees(num_nodes);
  for (int i = 0; i < num_nodes; i++) {
    node_degrees[i] = g->get_node_degree(i);
  }

  for (int u = 0; u < num_nodes; u++) {
    auto u_edges = g->get_edges(u);
    std::unordered_set<int> marked(u_edges.begin(), u_edges.end());

    for (int v : u_edges) {
      if (node_degrees[v] > node_degrees[u] ||
          (node_degrees[v] == node_degrees[u] &&
           v > u)) { // degree(v) > degree(u)
        auto v_edges = g->get_edges(v);
        for (int w : v_edges) {
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

unsigned long long GraphAlgorithm::p_triangle_count() {
  clear();
  std::atomic<unsigned long long> res = 0;

  // Sort nodes in ascending degree
  std::vector<int> node_degrees(num_nodes);
  std::vector<int> node_indexes(num_nodes);
  for (int i = 0; i < num_nodes; i++) {
    node_indexes[i] = i;
    node_degrees[i] = g->get_node_degree(i);
  }

  std::sort(node_indexes.begin(), node_indexes.end(),
            [&node_degrees](int &a, int &b) {
              return node_degrees[a] > node_degrees[b];
            });

  std::vector<bool> is_counted(num_nodes, false);

  for (int u : node_indexes) {
    is_counted[u] = true;
    auto u_edges = g->get_edges(u);
    std::unordered_set<int> marked(u_edges.begin(), u_edges.end());

    pool.push_loop(u_edges.size(), [this, &u_edges, &is_counted, &marked,
                                    &res](const int a, const int b) {
      for (int i = a; i < b; i++) {
        int v = u_edges[i];
        unsigned long long local_count = 0;
        if (!is_counted[v]) {
          auto v_edges = g->get_edges(v);
          for (int w : v_edges) { // w > u
            if (v != w && !is_counted[w] && marked.find(w) != marked.end()) {
              local_count++;
            }
          }
        }
        res += local_count;
      }
    });
    pool.wait_for_tasks();
  }
  return res / 2; // (u, v, w), (u, w, v) repeat twice
}

float GraphAlgorithm::s_pagerank() {
  clear();
  std::vector<float> pg_score(num_nodes);
  float eps = 0.01f;

  frontier.reserve(num_nodes);

  for (int i = 0; i < num_nodes; i++) {
    pg_score[i] = 1.f / g->get_node_degree(i);
    frontier.push_back(i);
    vis[i] = true;
  }

  while (!frontier.empty()) {
    // printf("size = %d\n", frontier.size());
    for (int v : frontier) {
      std::vector<int> edges = g->get_edges(v);
      float sum = 0.f;
      for (int w : edges) {
        sum += pg_score[w];
      }
      float score_new = (0.15f + 0.85f * sum) / g->get_node_degree(v);
      if (std::abs(score_new - pg_score[v]) > eps) {
        for (int w : edges) {
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
}

float GraphAlgorithm::p_pagerank_alt() {
  clear();
  std::vector<float> pg_score(num_nodes);
  float eps = 0.01f;

  for (int i = 0; i < num_nodes; i++) {
    pg_score[i] = 1.f / g->get_node_degree(i);
    frontier.push_back(i);
    atomic_vis[i] = true;
  }

  while (!frontier.empty()) {
    // printf("size = %d\n", frontier.size());
    pool.push_loop(
        frontier.size(), [this, &pg_score, &eps](const int a, const int b) {
          for (int i = a; i < b; i++) {
            int v = frontier[i];
            std::vector<int> edges = g->get_edges(v);
            std::vector<int> next_private;
            float sum = 0.f;
            for (int w : edges) {
              sum += pg_score[w];
            }
            float score_new = (0.15f + 0.85f * sum) / g->get_node_degree(v);
            if (std::abs(score_new - pg_score[v]) > eps) {
              for (int w : edges) {
                bool is_visited = atomic_vis[w].exchange(true);
                if (!is_visited) {
                  next_private.push_back(w);
                }
              }
            }
            if (next_private.size() > 0) {
              mtx.lock();
              next.insert(next.end(), next_private.begin(), next_private.end());
              mtx.unlock();
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

void GraphAlgorithm::p_pagerank_task(int v, std::vector<float> &pg_score,
                                     float eps) {
  std::vector<int> edges = g->get_edges(v);
  float sum = 0.f;
  for (int w : edges) {
    sum += pg_score[w];
  }
  // printf("v %d, sum = %.2f\n", v, sum);
  float score_new = (0.15f + 0.85f * sum) / g->get_node_degree(v);
  if (std::abs(score_new - pg_score[v]) > eps) {
    for (int w : edges) {
      bool is_visited = atomic_vis[w].exchange(true);
      if (!is_visited) {
        pool.push_task(&GraphAlgorithm::p_pagerank_task, this, w,
                       std::ref(pg_score), eps);
      }
    }
  }
  pg_score[v] = score_new;
  atomic_vis[v] = false;
}

float GraphAlgorithm::p_pagerank() {
  clear();
  std::vector<float> pg_score(num_nodes);
  float eps = 0.01f;

  for (int i = 0; i < num_nodes; i++) {
    pg_score[i] = 1.f / g->get_node_degree(i);
    atomic_vis[i] = true;
  }

  for (int i = 0; i < num_nodes; i++) {
    pool.push_task(&GraphAlgorithm::p_pagerank_task, this, i,
                   std::ref(pg_score), eps);
  }

  pool.wait_for_tasks();

  return pg_score[0];
}

// bool GraphAlgorithm::s_bfs_async() {
//   clear();
//   vis[start_id] = true;
//   frontier.push_back(start_id);

//   while (!is_found && frontier.size() > 0) {
//     int num_nodes = frontier.size();
//     int start = 0;
//     while (!is_found && start < num_nodes) {
//       int end = std::min(start + MAX_REQ, num_nodes);
//       // Make requests
//       for (int i = start; i < end; i++) {
//         if (!g->req_one_snode(frontier[i], 0))
//           exit(1);
//       }

//       // Get nodes
//       for (int i = start; i < end; i++) {
//         int stack_id;
//         auto recv_snode = g->get_one_snode(stack_id);
//         // nullptr, has ended or error.
//         if (!recv_snode)
//           continue;
//         // Found the key!
//         if (recv_snode->key == key) {
//           is_found = true;
//           continue;
//         }
//         // Insert children
//         for (int i = 0; i < recv_snode->degree; i++) {
//           int id = recv_snode->data[i];
//           bool is_visited = vis[id];
//           if (!is_visited) {
//             next.push_back(id);
//             vis[id] = true;
//           }
//         }
//       }
//       start = end;
//     }
//     if (!is_found) {
//       frontier = next;
//       next.clear();
//     }
//   }
//   return is_found;
// }

// bool GraphAlgorithm::p_bfs_async() {
//   clear();
//   atomic_vis[start_id] = true;
//   frontier.push_back(start_id);

//   // Loop through frontier
//   while (!is_found && frontier.size() > 0) {
//     int num_nodes = frontier.size();
//     int start = 0;
//     while (!is_found && start < num_nodes) {
//       int end = std::min(start + MAX_REQ, num_nodes);
//       pool.push_loop(start, end, [this](const int a, const int b) {
//         for (int i = a; i < b; i++) {
//           if (!g->req_one_snode(frontier[i], 0))
//             exit(1);
//         }
//       });
//       pool.push_loop(start, end, [this](const int a, const int b) {
//         for (int i = a; i < b; i++) {
//           std::vector<int> next_private;
//           int stack_id;
//           auto recv_snode = g->get_one_snode(stack_id);
//           // nullptr, has ended or error.
//           if (!recv_snode)
//             continue;

//           // Found the key!
//           if (recv_snode->key == key) {
//             is_found = true;
//             continue;
//           }
//           // Insert children
//           for (int i = 0; i < recv_snode->degree; i++) {
//             int id = recv_snode->data[i];
//             bool is_visited = atomic_vis[id].exchange(true);
//             if (!is_visited)
//               next_private.push_back(id);
//           }
//           if (!is_found && next_private.size() > 0) {
//             mtx.lock();
//             next.insert(next.end(), next_private.begin(),
//             next_private.end()); mtx.unlock();
//           }
//         }
//       });
//       pool.wait_for_tasks();
//       start = end;
//     }
//     if (!is_found) {
//       frontier = next;
//       next.clear();
//     }
//   }
//   return is_found;
// }

// bool GraphAlgorithm::s_dfs_async() {
//   clear();
//   clear_stack();

//   // Init stack 0 with start_id
//   vis[start_id] = true;

//   // Initialize empty stack list.
//   for (int i = MAX_ACTIVE_STACKS - 1; i >= 1; i--) {
//     free_stacks.push_back(i);
//   }

//   is_found = false;
//   int num_active_stacks = 1;

//   // Make initial request.
//   g->req_one_snode(start_id, 0);

//   // Process request results.
//   while (!is_found && num_active_stacks > 0) {
//     int stack_id = -1;
//     auto recv_snode = g->get_one_snode(stack_id);

//     if (!recv_snode)
//       break;
//     // printf("-------------\n[INFO]: Stack_id = %d\n", stack_id);

//     // Check stack id returned
//     if (stack_id < 0 || stack_id >= MAX_ACTIVE_STACKS) {
//       fprintf(stderr, "[ERROR]: Bad stack_id = %d\n", stack_id);
//       return false;
//     }

//     // Found key!
//     if (recv_snode->key == key) {
//       is_found = true;
//       break;
//     }

//     // A reference to current stack
//     std::vector<int> &cur_stack = stacks[stack_id];

//     // Push s_node's children to the same stack.
//     for (int i = 0; i < recv_snode->degree; i++) {
//       int node_id = recv_snode->data[i];
//       if (!vis[node_id]) {
//         vis[node_id] = true;
//         cur_stack.push_back(node_id);
//       }
//     }

//     // Check if current stack is empty now.
//     if (cur_stack.size() == 0) {
//       // Decrease active stack count
//       num_active_stacks--;

//       // Recycle empty stack.
//       free_stacks.push_back(stack_id);

//       // No active stacks, search finished.
//       if (num_active_stacks == 0) {
//         g->send_end_signal();
//         break;
//       }
//       continue;
//     }

//     while (num_active_stacks < MAX_ACTIVE_STACKS &&
//            cur_stack.size() > (size_t)max_stack_size) {
//       // Find an empty stack.
//       int stack_id2 = -1;
//       if (!free_stacks.empty()) {
//         stack_id2 = free_stacks.back();
//         free_stacks.pop_back();
//       }

//       // No free stacks
//       if (stack_id2 == -1)
//         break;

//       // Increase active stack count
//       num_active_stacks++;

//       // Split stack.
//       stacks[stack_id2] = std::vector<int>(
//           cur_stack.begin() + cur_stack.size() / 2, cur_stack.end());
//       cur_stack.resize(cur_stack.size() / 2);

//       // printf("[INFO]: Stack %d -> %d\n", stack_id, stack_id2);

//       // Pop new stack.
//       int node_id2 = stacks[stack_id2].back();
//       stacks[stack_id2].pop_back();
//       g->req_one_snode(node_id2, stack_id2);

//       // printf("[INFO]: NEW Stack %d request id %d\n", stack_id2, node_id2);
//     }

//     // Pop original stack.
//     int node_id = cur_stack.back();
//     cur_stack.pop_back();
//     g->req_one_snode(node_id, stack_id);
//     // printf("[INFO]: Stack %d request id %d\n", stack_id, node_id);
//   }
//   return is_found;
// }

// bool GraphAlgorithm::s_dfs_async_no_splits() {
//   clear();
//   frontier.push_back(start_id);
//   vis[start_id] = true;

//   // Make initial request.
//   g->req_one_snode(start_id, 0);

//   while (!is_found) {
//     int stack_id;
//     auto recv_snode = g->get_one_snode(stack_id);

//     if (stack_id != 0) {
//       fprintf(stderr, "[ERROR]: stack_id = %d != 0 in single stack DFS\n",
//               stack_id);
//       exit(1);
//     }

//     if (!recv_snode)
//       break;
//     if (recv_snode->key == key) {
//       is_found = true;
//       g->send_end_signal();
//       break;
//     }

//     for (auto i = 0; i < recv_snode->degree; i++) {
//       int node_id = recv_snode->data[i];
//       if (!vis[node_id]) {
//         vis[node_id] = true;
//         frontier.push_back(node_id);
//       }
//     }

//     if (frontier.size() == 0)
//       break;

//     int node_id = frontier.back();
//     frontier.pop_back();
//     g->req_one_snode(node_id, 0);
//   }
//   return is_found;
// }

// void GraphAlgorithm::p_dfs_async_task() {
//   while (!is_found && num_active_stacks > 0) {
//     int stack_id = -1;
//     auto recv_snode = g->get_one_snode(stack_id);

//     if (!recv_snode) {
//       // printf("[INFO]: <T%d> breaks.\n", omp_get_thread_num());
//       break;
//     }

//     // printf("stack_id = %d\n", stack_id);

//     // Check stack_id returned.
//     if (stack_id < 0 || stack_id >= MAX_ACTIVE_STACKS) {
//       fprintf(stderr, "[ERROR]: Bad stack_id = %d\n", stack_id);
//       exit(1);
//     }

//     // Found key!
//     if (recv_snode->key == key) {
//       is_found = true;
//       g->send_end_signal();
//       // printf("[INFO]: recv_snode->key = %d\n", recv_snode->key);
//       break;
//     }

//     // Reference to current stack
//     std::vector<int> &cur_stack = stacks[stack_id];

//     // Push s_node's children to the same stack.
//     for (int i = 0; i < recv_snode->degree; i++) {
//       int node_id = recv_snode->data[i];
//       if (node_id < 0) {
//         printf("[ERROR]: Bad Node_id = %d\n", node_id);
//         exit(1);
//       }
//       bool is_visited = atomic_vis[node_id].exchange(true);
//       if (!is_visited)
//         cur_stack.push_back(node_id);
//     }

//     // Check if current stack is empty now.
//     if (cur_stack.size() == 0) {
//       // Recycle empty stack.

//       // printf("Recollect stack %d\n", stack_id);

//       mtx.lock();
//       free_stacks.push_back(stack_id);
//       mtx.unlock();

//       // while (1) {
//       //   int old_head = q_head;
//       //   q_next[stack_id] = old_head;
//       //   if (q_head.compare_exchange_strong(old_head, stack_id))
//       //     break;
//       // }

//       // Decrease active stack count
//       int tmp = num_active_stacks.fetch_sub(1);
//       // printf("num_active_stacks = %d\n", tmp - 1);
//       if (tmp == 1) {
//         // No active stacks
//         g->send_end_signal();
//         break;
//       } else
//         continue;
//     }

//     while (cur_stack.size() > (size_t)max_stack_size) {
//       // Find an empty stack.
//       int stack_id2 = -1;
//       mtx.lock();
//       if (!free_stacks.empty()) {
//         stack_id2 = free_stacks.back();
//         free_stacks.pop_back();
//       }
//       mtx.unlock();

//       // while (1) {
//       //   stack_id2 = q_head;
//       //   if (q_head.compare_exchange_strong(stack_id2, q_next[stack_id2]))
//       //     break;
//       // }

//       // No free stack, stop spliting.
//       if (stack_id2 == -1)
//         break;

//       num_active_stacks++; // Increase active stack count

//       // Split stack.
//       stacks[stack_id2] = std::vector<int>(
//           cur_stack.begin() + cur_stack.size() / 2, cur_stack.end());
//       cur_stack.resize(cur_stack.size() / 2);

//       // printf("[INFO]: <T%d> Stack %d -> %d\n", omp_get_thread_num(),
//       // stack_id, stack_id2);

//       // Pop new stack.
//       int node_id2 = stacks[stack_id2].back();
//       stacks[stack_id2].pop_back();
//       g->req_one_snode(node_id2, stack_id2);

//       // printf("[INFO]: NEW Stack %d request id %d\n", stack_id2,
//       // node_id2);
//     }

//     // Pop original stack.
//     int node_id = cur_stack.back();
//     cur_stack.pop_back();
//     g->req_one_snode(node_id, stack_id);
//     // printf("[INFO]: Stack %d request id %d\n", stack_id, node_id);
//   }
// }

// bool GraphAlgorithm::p_dfs_async() {
//   clear();
//   clear_stack();

//   // Note: stack 0 is already used.
//   num_active_stacks = 1;

//   // Initialize free stack list.
//   for (int i = MAX_ACTIVE_STACKS - 1; i >= 1; i--) {
//     free_stacks.push_back(i);
//   }

//   // Make initial request.
//   atomic_vis[start_id] = true;
//   g->req_one_snode(start_id, 0);

//   for (int i = 0; i < pool.get_thread_count(); i++) {
//     pool.push_task(&GraphAlgorithm::p_dfs_async_task, this);
//   }
//   pool.wait_for_tasks();

//   return is_found;
// }
