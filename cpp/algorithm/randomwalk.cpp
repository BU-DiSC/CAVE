#include "../BS_thread_pool.hpp"
#include "../Graph.hpp"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_set>
#include <vector>

std::vector<uint32_t> frontier;
std::vector<uint32_t> next;
std::mutex mtx;
int nrepeats = 3;
int n_walks = 1000, n_steps = 1000;
std::string algo_name = "randomwalk";
std::mt19937 mt;

int parallel_randomwalk(Graph *g, int num_walks, int steps) {

  std::uniform_int_distribution<uint32_t> rnd_dist(0, g->get_num_nodes());

  for (int i = 0; i < num_walks; i++) {
    frontier.push_back(rnd_dist(mt));
  }

  int visited_node_count = 0;

  while (--steps >= 0) {
    visited_node_count += frontier.size();
    g->process_queue(frontier, next,
                     [&rnd_dist](uint32_t v_id,
                                 std::vector<uint32_t> &neighbors,
                                 std::vector<uint32_t> &next_private) {
                       uint32_t rnd_next = rnd_dist(mt) % neighbors.size();
                       next_private.push_back(neighbors[rnd_next]);
                     });

    frontier = next;
    next.clear();
  }
  frontier.clear();
  return visited_node_count;
};

int parallel_randomwalk(Graph *g, int num_walks) {

  int steps = g->get_num_nodes() / num_walks;

  return parallel_randomwalk(g, num_walks, steps);
};

int parallel_randomwalk_in_blocks(Graph *g, int num_walks, int steps) {

  std::uniform_int_distribution<uint32_t> rnd_dist(0, g->get_num_nodes());

  for (int i = 0; i < num_walks; i++) {
    frontier.push_back(rnd_dist(mt));
  }

  int visited_node_count = 0;

  while (--steps >= 0) {
    visited_node_count += frontier.size();
    g->process_queue_in_blocks(
        frontier, next,
        [&rnd_dist](uint32_t v_id, std::vector<uint32_t> &neighbors,
                    std::vector<uint32_t> &next_private) {
          uint32_t rnd_next = rnd_dist(mt) % neighbors.size();
          next_private.push_back(neighbors[rnd_next]);
        });

    frontier = next;
    next.clear();
  }
  frontier.clear();
  return visited_node_count;
}

int parallel_randomwalk_in_blocks(Graph *g, int num_walks) {

  int steps = g->get_num_nodes() / num_walks;

  return parallel_randomwalk_in_blocks(g, num_walks, steps);
};

std::vector<int> cache_mb_list1 = {1, 2, 3, 4, 5, 10, 25, 50};
std::vector<int> cache_mb_list2 = {20, 40, 60, 80, 100, 200, 500, 1000};
std::vector<int> cache_mb_list3 = {128,  256,  512,  1024,
                                   2048, 4096, 8192, 16384};
std::vector<int> cache_mb_list0 = {1024};

int main(int argc, char *argv[]) {

  if (argc < 3) {
    printf("[ERROR] Use case: bfs [file_path] [test case (thread, cache)] "
           "[args]\n");
    return 0;
  }

  mt.seed(42);

  Graph *g = new Graph();
  g->init_serializer(argv[1], MODE::SYNC_READ);
  g->init_metadata();
  g->init_vertex_data();

  std::filesystem::path file_fs_path(argv[1]);
  std::filesystem::path log_fs_path("..");
  log_fs_path = log_fs_path / "log" / file_fs_path.stem();
  log_fs_path += "_" + algo_name;

  if (strcmp(argv[2], "cache") == 0) {

    unsigned int thread_count = std::thread::hardware_concurrency();

    log_fs_path += "_cache.csv";
    auto log_fp = fopen(log_fs_path.string().data(), "w");
    fprintf(log_fp, "algo_name,thread,cache_mb,time,res\n");

    int test_id = 3;

    if (argc >= 4)
      test_id = atoi(argv[3]);
    std::vector<int> cache_mb_l;
    if (test_id == 0)
      cache_mb_l = cache_mb_list0;
    else if (test_id == 1)
      cache_mb_l = cache_mb_list1;
    else if (test_id == 2)
      cache_mb_l = cache_mb_list2;
    else
      cache_mb_l = cache_mb_list3;

    g->set_cache_mode(SIMPLE_CACHE);

    for (int cache_mb : cache_mb_l) {
      g->set_cache_size(cache_mb);
      printf("---[Cache size: %d MB]---\n", cache_mb);

      long total_ms_int = 0;

      for (int i = 0; i < nrepeats; i++) {
        g->clear_cache();
        auto begin = std::chrono::high_resolution_clock::now();
        int res = parallel_randomwalk_in_blocks(g, n_walks);
        auto end = std::chrono::high_resolution_clock::now();
        auto ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        total_ms_int += ms_int;
        printf("[Test %d] %d nodes visited in %ld us.\n", i, res, ms_int);
        fprintf(log_fp, "%s,%u,%d,%ld,%d\n", (algo_name + "_blocked").c_str(),
                thread_count, cache_mb, ms_int, res);
      }
      printf("[Total] Average time: %ld us.\n", total_ms_int / nrepeats);
    }

    g->set_cache_mode(NORMAL_CACHE);

    for (int cache_mb : cache_mb_l) {
      g->set_cache_size(cache_mb);
      printf("---[Cache size: %d MB]---\n", cache_mb);

      long total_ms_int = 0;

      for (int i = 0; i < nrepeats; i++) {
        g->clear_cache();
        auto begin = std::chrono::high_resolution_clock::now();
        int res = parallel_randomwalk(g, n_walks);
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
    auto log_fp = fopen(log_fs_path.string().data(), "w");
    fprintf(log_fp, "algo_name,thread,cache_mb,time,res\n");

    g->set_cache_mode(SIMPLE_CACHE);

    for (int thread_count = min_thread; thread_count <= max_thread;
         thread_count *= 2) {
      g->set_thread_pool_size(thread_count);
      printf("---[Thread count: %d]---\n", thread_count);

      long total_ms_int = 0;

      for (int i = 0; i < nrepeats; i++) {
        g->clear_cache();
        auto begin = std::chrono::high_resolution_clock::now();
        int res = parallel_randomwalk_in_blocks(g, n_walks);
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

    // g->set_cache_mode(NORMAL_CACHE);

    // for (int thread_count = min_thread; thread_count <= max_thread;
    //      thread_count *= 2) {
    //   g->set_thread_pool_size(thread_count);
    //   printf("---[Thread count: %d]---\n", thread_count);

    //   long total_ms_int = 0;

    //   for (int i = 0; i < nrepeats; i++) {
    //     g->clear_cache();
    //     auto begin = std::chrono::high_resolution_clock::now();
    //     int res = parallel_randomwalk(g, n_walks);
    //     auto end = std::chrono::high_resolution_clock::now();
    //     auto ms_int =
    //         std::chrono::duration_cast<std::chrono::microseconds>(end -
    //         begin)
    //             .count();
    //     total_ms_int += ms_int;
    //     printf("[Test %d] %d nodes visited in %ld us.\n", i, res, ms_int);
    //     fprintf(log_fp, "%s,%d,%d,%ld,%d\n", algo_name.c_str(), thread_count,
    //             cache_mb, ms_int, res);
    //   }
    //   printf("[Total] Average time: %ld us.\n", total_ms_int / nrepeats);
    // }

  } else {
    printf("[ERROR] Please input test case (thread, cache).\n");
  }

  return 0;
}