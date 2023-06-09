#include "GraphAlgorithm.hpp"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int num_keys = 5;
int num_repeats = 3;
int max_threads = 256;
int sync_fsize_thread = 8;
int async_fsize_thread = 8;
char *bin_path;
int large_graph_thres = 100000;

std::random_device rd;
std::mt19937 gen;
std::uniform_int_distribution<> dist_graph;
std::vector<int> num_threads = {1, 2, 4, 8, 16, 32, 64, 128, 256};
std::vector<int> cache_ratios = {2, 4, 6, 8, 10, 20, 50, 100};

enum GALGO { GALGO_SEARCH, GALGO_WCC };
enum TEST_CASE { TEST_THREAD, TEST_CACHE, TEST_FSIZE };

GALGO test_algo = GALGO::GALGO_SEARCH;
TEST_CASE test_case = TEST_CASE::TEST_THREAD;

std::vector<int> test_keys;
int check_key;

GraphAlgorithm *g_algo;
FILE *out_fp;

int cache_set = 2;
int cache_size_mb = 4;
double cache_ratio = 0.1; // 10% cache size in default

void set_mode(MODE mode) { g_algo->set_mode(mode); }

void init_outfp() {
  std::filesystem::path out_path("..");
  std::filesystem::path data_path(bin_path);

  out_path = out_path / "output" / data_path.stem();
  char suffix[100];

  if (test_case == TEST_CASE::TEST_CACHE) {
    std::string algo_name;
    if (test_algo == GALGO::GALGO_SEARCH) {
      algo_name = "_search";
    } else if (test_algo == GALGO::GALGO_WCC) {
      algo_name = "_wcc";
    }
    sprintf(suffix, "_%dk_test_cache.csv", num_repeats);
    out_path += algo_name;
    out_path += suffix;
    out_fp = fopen(out_path.string().data(), "w");
    printf("[INFO] Result Path: %s\n", out_path.string().data());
    fprintf(out_fp, "key,thread,cache_ratio,name,type,time,res\n");
  } else if (test_case == TEST_CASE::TEST_THREAD) {
    std::string algo_name;
    if (test_algo == GALGO::GALGO_SEARCH) {
      algo_name = "_search";
    } else if (test_algo == GALGO::GALGO_WCC) {
      algo_name = "_wcc";
    }
    if (cache_set == 0) {
      sprintf(suffix, "_%dk_%dr_NC.csv", num_keys, num_repeats);
    } else if (cache_set == 1) {
      sprintf(suffix, "_%dk_%dr_%dM.csv", num_keys, num_repeats, cache_size_mb);
    } else if (cache_set == 2) {
      sprintf(suffix, "_%dk_%dr_%.2f.csv", num_keys, num_repeats, cache_ratio);
    }
    out_path += algo_name;
    out_path += suffix;

    // Open file for csv output.
    out_fp = fopen(out_path.string().data(), "w");
    printf("[INFO] Result Path: %s\n", out_path.string().data());

    fprintf(out_fp, "key,thread,name,type,time,res\n");
  } else if (test_case == TEST_CASE::TEST_FSIZE) {
    sprintf(suffix, "_%dk_test_fsize.csv", num_repeats);
    out_path += suffix;
    out_fp = fopen(out_path.string().data(), "w");
    printf("[INFO] Result Path: %s\n", out_path.string().data());
    fprintf(out_fp, "key,thread,fsize,name,type,time,res\n");
  }
}

void init_keys() {
  std::filesystem::path data_path(bin_path);
  std::filesystem::path key_path("..");

  key_path = key_path / "key" / data_path.stem();
  key_path += "_" + std::to_string(num_keys);
  key_path += "_keys.txt";

  // Check if file for keys exists
  if (std::filesystem::exists(key_path)) {
    printf("Read key file...\n");
    auto key_file = fopen(key_path.string().data(), "r");
    fscanf(key_file, "%d", &num_keys);
    for (int i = 0; i < num_keys; i++) {
      fscanf(key_file, "%d", &test_keys[i]);
      printf("%d: %d\n", i, test_keys[i]);
    }
    fclose(key_file);
  } else {
    printf("Key file not found, write a new one.\n");
    auto key_file = fopen(key_path.string().data(), "w");
    fprintf(key_file, "%d\n", num_keys);

    for (int i = 0; i < num_keys; i++) {
      test_keys[i] = dist_graph(gen);
      fprintf(key_file, "%d ", test_keys[i]);
      printf("%d: %d\n", i, test_keys[i]);
    }
    fprintf(key_file, "\n");
    fclose(key_file);
  }

  // Set check key
  check_key = dist_graph(gen);
}

void init() {
  init_outfp();

  // Init Graph Algorithm object
  g_algo = new GraphAlgorithm(bin_path, MODE::SYNC_READ);
  g_algo->set_start_id(0);

  if (cache_set == 0) {
    g_algo->disable_cache();
  } else if (cache_set == 1) {
    g_algo->set_cache_size(cache_size_mb);
  } else if (cache_set == 2) {
    g_algo->set_cache_ratio(cache_ratio);
  }

  // Set random seeds
  dist_graph = std::uniform_int_distribution<>(0, g_algo->get_num_nodes());
  gen = std::mt19937(rd());

  test_keys = std::vector<int>(num_keys);
  init_keys();
}

void sync_check() {
  // Sanity Check.
  printf("[INFO] Sanity Check!\n");
  printf("Check key: %d\n", check_key);
  g_algo->set_key(check_key);

  if (g_algo->get_num_nodes() < large_graph_thres) {
    // Serial algorithms will be too slow for very large graphs
    printf("s_bfs result: %d\n", g_algo->s_bfs());
    printf("s_dfs result: %d\n", g_algo->s_dfs());
  }

  g_algo->set_max_stack_size(4);
  printf("p_bfs result: %d\n", g_algo->p_bfs());
  printf("p_dfs result: %d\n", g_algo->p_dfs());

  if (g_algo->get_num_nodes() < large_graph_thres) {
    printf("s_wcc result: %d\n", g_algo->s_WCC());
    printf("s_wcc_2 result: %d\n", g_algo->s_WCC_2());
  }

  printf("p_wcc_1 result: %d\n", g_algo->p_WCC_1());
  printf("p_wcc_2 result: %d\n", g_algo->p_WCC_2());
}

/* Async tests */
// void async_check() {
//   printf("--------------\nSanity Check!\n");
//   printf("Test key: %d\n", check_key);
//   g_algo->set_key(check_key);

//   printf("S_BFS_async result: %d\n", g_algo->s_bfs_async());
//   g_algo->clear_signals();

//   printf("S_DFS_async result: %d\n", g_algo->s_dfs_async());
//   g_algo->clear_signals();

//   // printf("S_DFS_async_no_splits result: %d\n",
//   // g_algo->s_dfs_async_no_splits()); g_algo->clear_signals();

//   g_algo->reset_num_threads();

//   printf("P_BFS_async result: %d\n", g_algo->p_bfs_async());
//   g_algo->clear_signals();

//   printf("P_BFS_async_acc result: %d\n", g_algo->p_bfs_async_acc());
//   g_algo->clear_signals();

//   printf("P_DFS_async result: %d\n", g_algo->p_dfs_async());
//   g_algo->clear_signals();

//   printf("P_DFS_async_acc result: %d\n", g_algo->p_dfs_async_acc());
//   g_algo->clear_signals();
// }

// void test_graph_async() {

//   async_check();

//   for (int i = 0; i < num_keys; i++) {
//     int key = test_keys[i];
//     g_algo->set_key(key);
//     printf("[INFO]: ASYNC Test %d, key = %d\n", i, key);

//     for (int t = 0; t < num_repeats; t++) {
//       // S_BFS_ASYNC
//       auto begin = std::chrono::high_resolution_clock::now();
//       auto res = g_algo->s_bfs_async();
//       auto end = std::chrono::high_resolution_clock::now();
//       int64_t ms_int =
//           std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
//               .count();
//       // printf("[INFO]: s_bfs_async res %d\n", res);
//       g_algo->clear_signals();

//       fprintf(out_fp, "%d,1,s_bfs_async,s_async,%zd,%d\n", key, ms_int,
//       res);

//       // S_DFS_ASYNC
//       begin = std::chrono::high_resolution_clock::now();
//       res = g_algo->s_dfs_async();
//       end = std::chrono::high_resolution_clock::now();
//       ms_int =
//           std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
//               .count();
//       g_algo->clear_signals();
//       fprintf(out_fp, "%d,1,s_dfs_async,s_async,%zd,%d\n", key, ms_int,
//       res);

//       // S_DFS_ASYNC_NO_SPLITS

//       // begin = std::chrono::high_resolution_clock::now();
//       // res = g_algo->s_dfs_async_no_splits();
//       // end = std::chrono::high_resolution_clock::now();
//       // ms_int =
//       //     std::chrono::duration_cast<std::chrono::microseconds>(end -
//       begin)
//       //         .count();
//       // g_algo->clear_signals();
//       // fprintf(out_fp, "%d,1,s_dfs_async_no_splits,s_async,%zd,%d\n", key,
//       //         ms_int, res);

//       /* P_BFS_ASYNC */
//       for (size_t k = 0; k < num_threads.size(); k++) {
//         int thread_num = num_threads.at(k);
//         if (thread_num > max_threads)
//           break;
//         g_algo->set_num_threads(thread_num);

//         auto begin = std::chrono::high_resolution_clock::now();
//         auto res = g_algo->p_bfs_async();
//         auto end = std::chrono::high_resolution_clock::now();
//         int64_t ms_int =
//             std::chrono::duration_cast<std::chrono::microseconds>(end -
//             begin)
//                 .count();
//         // printf("[INFO]: p_bfs_async res %d\n", res);
//         fprintf(out_fp, "%d,%d,p_bfs_async,p_async,%zd,%d\n", key,
//                 thread_num, ms_int, res);
//         g_algo->clear_signals();
//       }

//       /* P_BFS_ASYNC_ACC */
//       for (size_t k = 0; k < num_threads.size(); k++) {
//         int thread_num = num_threads.at(k);
//         if (thread_num > max_threads)
//           break;
//         g_algo->set_num_threads(thread_num);

//         auto begin = std::chrono::high_resolution_clock::now();
//         auto res = g_algo->p_bfs_async_acc();
//         auto end = std::chrono::high_resolution_clock::now();
//         int64_t ms_int =
//             std::chrono::duration_cast<std::chrono::microseconds>(end -
//             begin)
//                 .count();
//         // printf("[INFO]: p_bfs_async res %d\n", res);
//         fprintf(out_fp, "%d,%d,p_bfs_async_acc,p_async,%zd,%d\n", key,
//                 thread_num, ms_int, res);
//         g_algo->clear_signals();
//       }

//       /* P_DFS_ASYNC */
//       for (size_t k = 0; k < num_threads.size(); k++) {
//         int thread_num = num_threads.at(k);
//         if (thread_num > max_threads)
//           break;
//         g_algo->set_num_threads(thread_num);
//         auto begin = std::chrono::high_resolution_clock::now();
//         auto res = g_algo->p_dfs_async();
//         auto end = std::chrono::high_resolution_clock::now();
//         int64_t ms_int =
//             std::chrono::duration_cast<std::chrono::microseconds>(end -
//             begin)
//                 .count();
//         // fprintf(stderr, "[INFO]: Threads %d, p_dfs_async res %d\n",
//         //         thread_num, res);
//         fprintf(out_fp, "%d,%d,p_dfs_async,p_async,%zd,%d\n", key,
//                 thread_num, ms_int, res);
//         g_algo->clear_signals();
//       }

//       /* P_DFS_ASYNC_ACC */
//       for (size_t k = 0; k < num_threads.size(); k++) {
//         int thread_num = num_threads.at(k);
//         if (thread_num > max_threads)
//           break;
//         g_algo->set_num_threads(thread_num);
//         auto begin = std::chrono::high_resolution_clock::now();
//         auto res = g_algo->p_dfs_async_acc();
//         auto end = std::chrono::high_resolution_clock::now();
//         int64_t ms_int =
//             std::chrono::duration_cast<std::chrono::microseconds>(end -
//             begin)
//                 .count();
//         // fprintf(stderr, "[INFO]: Threads %d, p_dfs_async res %d\n",
//         //         thread_num, res);
//         fprintf(out_fp, "%d,%d,p_dfs_async_acc,p_async,%zd,%d\n", key,
//                 thread_num, ms_int, res);
//         g_algo->clear_signals();
//       }
//     }
//   }
// }

// Cache tests
void cache_test() {
  int num_threads = 64;
  g_algo->set_num_threads(num_threads);
  g_algo->set_cache_mode(CACHE_MODE::SMALL_CACHE);

  printf("[INFO] Thread count = %d\n", num_threads);

  for (int t = 0; t < num_repeats; t++) {
    printf("-------\nTest #%d:\n", t);

    if (g_algo->get_num_nodes() < large_graph_thres) {
      printf("[INFO] No Cache tests.\n");
      g_algo->disable_cache();
      if (test_algo == GALGO::GALGO_SEARCH) {
        auto begin = std::chrono::high_resolution_clock::now();
        auto res = g_algo->p_bfs_all();
        auto end = std::chrono::high_resolution_clock::now();
        auto ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        fprintf(out_fp, "%d,%d,%d,p_bfs_all,small_cache,%zd,%d\n", t,
                num_threads, 0, ms_int, res);
      } else if (test_algo == GALGO::GALGO_WCC) {
        auto begin = std::chrono::high_resolution_clock::now();
        auto res = g_algo->p_WCC_1();
        auto end = std::chrono::high_resolution_clock::now();
        auto ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        fprintf(out_fp, "%d,%d,%d,p_wcc_1,small_cache,%zd,%d\n", t, num_threads,
                0, ms_int, res);
      }
    }

    for (int i = 0; i < cache_ratios.size(); i++) {
      double ratio = cache_ratios[i] / 100.0;
      printf("[INFO] Test cache ratio %d\n", cache_ratios[i]);
      g_algo->set_cache_ratio(ratio);

      if (test_algo == GALGO::GALGO_SEARCH) {
        auto begin = std::chrono::high_resolution_clock::now();
        auto res = g_algo->p_bfs_all();
        auto end = std::chrono::high_resolution_clock::now();
        auto ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        fprintf(out_fp, "%d,%d,%d,p_bfs_all,small_cache,%zd,%d\n", t,
                num_threads, cache_ratios[i], ms_int, res);
      } else if (test_algo == GALGO::GALGO_WCC) {
        auto begin = std::chrono::high_resolution_clock::now();
        auto res = g_algo->p_WCC_1();
        auto end = std::chrono::high_resolution_clock::now();
        auto ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        fprintf(out_fp, "%d,%d,%d,p_wcc_1,small_cache,%zd,%d\n", t, num_threads,
                cache_ratios[i], ms_int, res);
      }
    }
  }
}

void thread_wcc_test() {
  printf("[INFO] # of keys (repeats): %d\n", num_keys);

  // WCC tests
  for (int t = 0; t < num_repeats; t++) {
    printf("[INFO] WCC test %d\n", t);
    // S_WCC_1
    auto begin = std::chrono::high_resolution_clock::now();
    auto res = g_algo->s_WCC();
    auto end = std::chrono::high_resolution_clock::now();
    int64_t ms_int =
        std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
            .count();
    fprintf(out_fp, "%d,1,s_wcc_1,wcc_sync,%zd,%d\n", t, ms_int, res);

    // S_WCC_2
    begin = std::chrono::high_resolution_clock::now();
    res = g_algo->s_WCC_2();
    end = std::chrono::high_resolution_clock::now();
    ms_int = std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                 .count();
    fprintf(out_fp, "%d,1,s_wcc_2,wcc_sync,%zd,%d\n", t, ms_int, res);

    for (size_t k = 0; k < num_threads.size(); k++) {
      int thread_num = num_threads.at(k);
      if (thread_num > max_threads)
        break;
      g_algo->set_num_threads(thread_num);

      begin = std::chrono::high_resolution_clock::now();
      res = g_algo->p_WCC_1();
      end = std::chrono::high_resolution_clock::now();
      ms_int =
          std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
              .count();
      fprintf(out_fp, "%d,%d,p_wcc_1,wcc_sync,%zd,%d\n", t, thread_num, ms_int,
              res);

      begin = std::chrono::high_resolution_clock::now();
      res = g_algo->p_WCC_2();
      end = std::chrono::high_resolution_clock::now();
      ms_int =
          std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
              .count();
      fprintf(out_fp, "%d,%d,p_wcc_2,wcc_sync,%zd,%d\n", t, thread_num, ms_int,
              res);
    }
  }
}

void thread_search_test() {
  printf("[INFO] # of keys (runs): %d\n", num_keys);
  printf("[INFO] # of tests for each key: %d\n", num_repeats);

  // Searching tests
  for (int i = 0; i < num_keys; i++) {
    int key = test_keys[i];
    g_algo->set_key(key);
    printf("[INFO] Search test %d, key = %d\n", i, key);

    for (int t = 0; t < num_repeats; t++) {
      auto begin = std::chrono::high_resolution_clock::now();
      auto res = g_algo->s_bfs();
      auto end = std::chrono::high_resolution_clock::now();
      int64_t ms_int =
          std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
              .count();

      fprintf(out_fp, "%d,1,s_bfs,search_sync,%zd,%d\n", key, ms_int, res);

      begin = std::chrono::high_resolution_clock::now();
      res = g_algo->s_dfs();
      end = std::chrono::high_resolution_clock::now();
      ms_int =
          std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
              .count();
      fprintf(out_fp, "%d,1,s_dfs,search_sync,%zd,%d\n", key, ms_int, res);

      for (size_t k = 0; k < num_threads.size(); k++) {
        int thread_num = num_threads.at(k);
        if (thread_num > max_threads)
          break;
        g_algo->set_num_threads(thread_num);

        // P_BFS
        begin = std::chrono::high_resolution_clock::now();
        res = g_algo->p_bfs();
        end = std::chrono::high_resolution_clock::now();
        ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        fprintf(out_fp, "%d,%d,p_bfs,search_sync,%zd,%d\n", key, thread_num,
                ms_int, res);

        // P_DFS
        begin = std::chrono::high_resolution_clock::now();
        res = g_algo->p_dfs();
        end = std::chrono::high_resolution_clock::now();
        ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        fprintf(out_fp, "%d,%d,p_dfs,search_sync,%zd,%d\n", key, thread_num,
                ms_int, res);
      }
    }
  }
}

void fsize_test() {
  std::vector<int> fsize_list = {1, 2, 4, 8, 16, 32, 64, 128, 256};

  // std::vector<int> sync_num_threads = {sync_fsize_thread};
  // std::vector<int> async_num_threads = {async_fsize_thread};
  int thread_count = 16;

  g_algo->set_start_id(0);
  g_algo->set_num_threads(thread_count);

  // Sanity Check
  g_algo->set_key(check_key);

  // Test
  printf("------------\n[INFO] Fsize test.\n");
  // SYNC tests
  for (int i = 0; i < num_keys; i++) {
    int key = test_keys[i];
    g_algo->set_key(key);
    printf("[INFO] SYNC Test %d, key = %d\n", i, key);
    for (int t = 0; t < num_repeats; t++) {
      for (size_t k = 0; k < fsize_list.size(); k++) {
        int fsize = fsize_list.at(k);
        g_algo->set_max_stack_size(fsize);

        auto begin = std::chrono::high_resolution_clock::now();
        auto res = g_algo->p_dfs();
        auto end = std::chrono::high_resolution_clock::now();
        int64_t ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        fprintf(out_fp, "%d,%d,%d,p_dfs,p_sync,%zd,%d\n", key, thread_count,
                fsize, ms_int, res);
      }
    }
  }

  // // ASYNC tests.

  // // Reopen an aysnc serializer
  // g_algo->set_mode(MODE::ASYNC_READ);

  // g_algo->reset_num_threads();
  // printf("--------------\nSanity Check!\n");
  // printf("Test key: %d\n", check_key);
  // printf("P_DFS_async result: %d\n", g_algo->p_dfs_async());
  // g_algo->clear_signals();

  // for (size_t i = 0; i < async_num_threads.size(); i++) {
  //   int num_threads = async_num_threads[i];
  //   g_algo->set_num_threads(num_threads);
  //   printf("------------\nUse %d thread for ASYNC test.\n", num_threads);
  //   // P_DFS_ASYNC
  //   for (int j = 0; j < num_keys; j++) {
  //     int key = test_keys[j];
  //     g_algo->set_key(key);

  //     printf("[INFO]: ASYNC Test %d, key = %d\n", j, key);
  //     for (int t = 0; t < num_repeats; t++) {
  //       for (size_t k = 0; k < fsize_list.size(); k++) {
  //         int fsize = fsize_list.at(k);
  //         g_algo->set_max_stack_size(fsize);

  //         auto begin = std::chrono::high_resolution_clock::now();
  //         auto res = g_algo->p_dfs_async();
  //         auto end = std::chrono::high_resolution_clock::now();
  //         int64_t ms_int =
  //             std::chrono::duration_cast<std::chrono::microseconds>(end -
  //             begin)
  //                 .count();
  //         fprintf(out_fp, "%d,%d,p_dfs_async,p_async,%zd,%d,%d\n", key,
  //                 num_threads, ms_int, fsize, res);
  //         g_algo->clear_signals();
  //       }
  //     }
  //   }
  // }
  fclose(out_fp);
}

void thread_test() {
  printf("[INFO] Test thread counts: ");
  for (int i = 0; i < num_threads.size(); i++) {
    printf("%d ", num_threads[i]);
  }
  printf("\n");

  if (test_algo == GALGO::GALGO_WCC) {
    thread_wcc_test();
  } else if (test_algo == GALGO::GALGO_SEARCH) {
    thread_search_test();
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "[ERROR]: Usage is ./main <data_path>\n"
                    "(-test_algo, -test_case, -nkeys, -nrepeats, -max_threads, "
                    "-cache_size_mb, "
                    "-cache_ratio)\n");
    return 0;
  }

  bin_path = argv[1];

  for (int i = 2; i < argc; i++) {
    if (strcmp(argv[i], "-nkeys") == 0) {
      num_keys = std::stoi(argv[i + 1]);
    } else if (strcmp(argv[i], "-nrepeats") == 0) {
      num_repeats = std::stoi(argv[i + 1]);
    } else if (strcmp(argv[i], "-max_threads") == 0) {
      max_threads = std::stoi(argv[i + 1]);
    } else if (strcmp(argv[i], "-cache_size_mb") == 0) {
      int size_mb = std::stoi(argv[i + 1]);
      if (size_mb <= 0) {
        cache_set = 0;
      } else {
        cache_set = 1;
        cache_size_mb = std::stoi(argv[i + 1]);
      }
    } else if (strcmp(argv[i], "-cache_ratio") == 0) {
      cache_set = 2;
      cache_ratio = std::stod(argv[i + 1]);
    } else if (strcmp(argv[i], "-test_algo") == 0) {
      if (strcmp(argv[i + 1], "search") == 0) {
        test_algo = GALGO::GALGO_SEARCH;
      } else if (strcmp(argv[i + 1], "wcc") == 0) {
        test_algo = GALGO::GALGO_WCC;
      }
    } else if (strcmp(argv[i], "-test_case") == 0) {
      if (strcmp(argv[i + 1], "thread") == 0) {
        test_case = TEST_CASE::TEST_THREAD;
      } else if (strcmp(argv[i + 1], "cache") == 0) {
        test_case = TEST_CASE::TEST_CACHE;
      } else if (strcmp(argv[i + 1], "fsize") == 0) {
        test_case = TEST_CASE::TEST_FSIZE;
      }
    }
  }

  init();

  sync_check();

  // Start tests
  if (test_case == TEST_CASE::TEST_THREAD) {
    thread_test();
  } else if (test_case == TEST_CASE::TEST_CACHE) {
    cache_test();
  } else if (test_case == TEST_CASE::TEST_FSIZE) {
    fsize_test();
  }

  fclose(out_fp);

  return 0;
}
