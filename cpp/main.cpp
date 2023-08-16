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
char *bin_path;
int large_graph_thres = 100000;
bool test_serial = true;

// int async_fsize_thread = 8;

std::random_device rd;
std::mt19937 gen;
std::uniform_int_distribution<> dist_graph;
std::vector<int> num_threads = {1, 2, 4, 8, 16, 32, 64, 128, 256};
std::vector<int> cache_ratios = {2, 4, 6, 8, 10, 20, 50, 100};

enum GALGO { GALGO_SEARCH, GALGO_WCC, GALGO_TC, GALGO_PAGERANK };
enum TEST_CASE { TEST_THREAD, TEST_CACHE, TEST_FSIZE };

GALGO test_algo = GALGO::GALGO_SEARCH;
TEST_CASE test_case = TEST_CASE::TEST_THREAD;

std::vector<int> test_keys;
int check_key;

GraphAlgorithm *g_algo;
FILE *out_fp;

int cache_set = 1;
int cache_mb = 8192;
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
    } else if (test_algo == GALGO::GALGO_TC) {
      algo_name = "_tc";
    } else if (test_algo == GALGO::GALGO_PAGERANK) {
      algo_name = "_pagerank";
    }
    sprintf(suffix, "_%dr_test_cache.csv", num_repeats);
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
    } else if (test_algo == GALGO::GALGO_TC) {
      algo_name = "_tc";
    } else if (test_algo == GALGO::GALGO_PAGERANK) {
      algo_name = "_pagerank";
    }
    if (cache_set == 0) {
      sprintf(suffix, "_%dk_%dr_NC.csv", num_keys, num_repeats);
    } else if (cache_set == 1) {
      sprintf(suffix, "_%dk_%dr_%dM.csv", num_keys, num_repeats, cache_mb);
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
    g_algo->set_cache_size(cache_mb);
  } else if (cache_set == 2) {
    g_algo->set_cache_ratio(cache_ratio);
  }

  // Set random seeds
  dist_graph = std::uniform_int_distribution<>(0, g_algo->get_num_nodes());
  gen = std::mt19937(rd());

  test_keys = std::vector<int>(num_keys);
  init_keys();

  if (g_algo->get_num_nodes() >= large_graph_thres)
    test_serial = false;
}

void sync_check() {
  // Sanity Check.
  printf("[INFO] Sanity Check!\n");

  if (test_algo == GALGO_SEARCH) {
    printf("Search check key: %d\n", check_key);
    g_algo->set_key(check_key);
    if (test_serial) {
      printf("s_bfs result: %d\n", g_algo->s_bfs());
      printf("s_dfs result: %d\n", g_algo->s_dfs());
    }

    g_algo->set_max_stack_size(4);
    printf("p_bfs result: %d\n", g_algo->p_bfs());
    printf("p_dfs result: %d\n", g_algo->p_dfs());
  } else if (test_algo == GALGO_WCC) {
    if (test_serial) {
      printf("s_wcc result: %d\n", g_algo->s_WCC());
    }
    printf("p_wcc result: %d\n", g_algo->p_WCC());
  } else if (test_algo == GALGO_TC) {
    if (test_serial) {
      printf("s_tc result: %llu\n", g_algo->s_triangle_count());
    }
    printf("p_tc result: %llu\n", g_algo->p_triangle_count());
  } else if (test_algo == GALGO_PAGERANK) {
    if (test_serial) {
      printf("s_pagerank result: %.2f\n", g_algo->s_pagerank());
    }
    printf("p_pagerank result: %.2f\n", g_algo->p_pagerank());
    // printf("p_pagerank_alt result: %.2f\n", g_algo->p_pagerank_alt());
  }
}

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
        auto res = g_algo->p_WCC();
        auto end = std::chrono::high_resolution_clock::now();
        auto ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        fprintf(out_fp, "%d,%d,%d,p_wcc,small_cache,%zd,%d\n", t, num_threads,
                0, ms_int, res);
      } else if (test_algo == GALGO::GALGO_PAGERANK) {
        auto begin = std::chrono::high_resolution_clock::now();
        auto res = g_algo->p_pagerank_alt();
        auto end = std::chrono::high_resolution_clock::now();
        auto ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        fprintf(out_fp, "%d,%d,%d,p_pagerank,small_cache,%zd,%.2f\n", t,
                num_threads, 0, ms_int, res);
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
        auto res = g_algo->p_WCC();
        auto end = std::chrono::high_resolution_clock::now();
        auto ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        fprintf(out_fp, "%d,%d,%d,p_wcc,small_cache,%zd,%d\n", t, num_threads,
                cache_ratios[i], ms_int, res);
      } else if (test_algo == GALGO::GALGO_PAGERANK) {
        auto begin = std::chrono::high_resolution_clock::now();
        auto res = g_algo->p_pagerank_alt();
        auto end = std::chrono::high_resolution_clock::now();
        auto ms_int =
            std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
                .count();
        fprintf(out_fp, "%d,%d,%d,p_pagerank,small_cache,%zd,%.2f\n", t,
                num_threads, cache_ratios[i], ms_int, res);
      }
    }
  }
}

void thread_wcc_test() {
  printf("[INFO] # of repeats: %d\n", num_repeats);

  // WCC tests
  for (int t = 0; t < num_repeats; t++) {
    printf("[INFO] WCC test %d\n", t);

    if (test_serial) {
      // S_WCC
      printf("[INFO] Serial WCC test...\n");
      auto begin = std::chrono::high_resolution_clock::now();
      auto res = g_algo->s_WCC();
      auto end = std::chrono::high_resolution_clock::now();
      int64_t ms_int =
          std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
              .count();
      fprintf(out_fp, "%d,1,s_wcc,wcc_sync,%zd,%d\n", t, ms_int, res);
    }

    for (size_t k = 0; k < num_threads.size(); k++) {
      int thread_num = num_threads.at(k);
      if (thread_num > max_threads)
        break;
      printf("[INFO] Parallel WCC test %d...\n", thread_num);
      g_algo->set_num_threads(thread_num);

      auto begin = std::chrono::high_resolution_clock::now();
      auto res = g_algo->p_WCC();
      auto end = std::chrono::high_resolution_clock::now();
      auto ms_int =
          std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
              .count();
      fprintf(out_fp, "%d,%d,p_wcc,wcc_sync,%zd,%d\n", t, thread_num, ms_int,
              res);
    }
  }
}

void thread_tc_test() {
  printf("[INFO] # of repeats: %d\n", num_repeats);

  // WCC tests
  for (int t = 0; t < num_repeats; t++) {
    printf("[INFO] TC test %d\n", t);

    if (test_serial) {
      // S_TC
      printf("[INFO] Serial TC test...\n");
      auto begin = std::chrono::high_resolution_clock::now();
      auto res = g_algo->s_triangle_count();
      auto end = std::chrono::high_resolution_clock::now();
      int64_t ms_int =
          std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
              .count();
      fprintf(out_fp, "%d,1,s_tc,tc_sync,%zd,%llu\n", t, ms_int, res);
    }

    // P_TC
    for (size_t k = 0; k < num_threads.size(); k++) {
      int thread_num = num_threads.at(k);
      if (thread_num > max_threads)
        break;
      printf("[INFO] Parallel TC test %d...\n", thread_num);
      g_algo->set_num_threads(thread_num);

      auto begin = std::chrono::high_resolution_clock::now();
      auto res = g_algo->p_triangle_count();
      auto end = std::chrono::high_resolution_clock::now();
      auto ms_int =
          std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
              .count();
      fprintf(out_fp, "%d,%d,p_tc,tc_sync,%zd,%llu\n", t, thread_num, ms_int,
              res);
    }
  }
}

void thread_pagerank_test() {
  printf("[INFO] # of repeats: %d\n", num_repeats);

  // WCC tests
  for (int t = 0; t < num_repeats; t++) {
    printf("[INFO] Pagerank test %d\n", t);

    if (test_serial) {
      // S_TC
      printf("[INFO] Serial Pagerank test...\n");
      auto begin = std::chrono::high_resolution_clock::now();
      auto res = g_algo->s_pagerank();
      auto end = std::chrono::high_resolution_clock::now();
      int64_t ms_int =
          std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
              .count();
      fprintf(out_fp, "%d,1,s_pg,pg_sync,%zd,%.2f\n", t, ms_int, res);
    }

    // P_TC
    for (size_t k = 0; k < num_threads.size(); k++) {
      int thread_num = num_threads.at(k);
      if (thread_num > max_threads)
        break;
      printf("[INFO] Parallel Pagerank test %d...\n", thread_num);
      g_algo->set_num_threads(thread_num);

      auto begin = std::chrono::high_resolution_clock::now();
      auto res = g_algo->p_pagerank();
      auto end = std::chrono::high_resolution_clock::now();
      auto ms_int =
          std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
              .count();
      fprintf(out_fp, "%d,%d,p_pg,pg_sync,%zd,%.2f\n", t, thread_num, ms_int,
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

      if (test_serial) {
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
      }

      for (size_t k = 0; k < num_threads.size(); k++) {
        int thread_num = num_threads.at(k);
        if (thread_num > max_threads)
          break;
        g_algo->set_num_threads(thread_num);

        // P_BFS
        auto begin = std::chrono::high_resolution_clock::now();
        auto res = g_algo->p_bfs();
        auto end = std::chrono::high_resolution_clock::now();
        auto ms_int =
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
  } else if (test_algo == GALGO::GALGO_TC) {
    thread_tc_test();
  } else if (test_algo == GALGO::GALGO_PAGERANK) {
    thread_pagerank_test();
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "[ERROR]: Usage is ./main <data_path>\n"
                    "(-test_algo, -test_case, -nkeys, -nrepeats, -max_threads, "
                    "-cache_mb, "
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
    } else if (strcmp(argv[i], "-cache_mb") == 0) {
      int size_mb = std::stoi(argv[i + 1]);
      if (size_mb <= 0) {
        cache_set = 0;
      } else {
        cache_set = 1;
        cache_mb = std::stoi(argv[i + 1]);
      }
    } else if (strcmp(argv[i], "-cache_ratio") == 0) {
      cache_set = 2;
      cache_ratio = std::stod(argv[i + 1]);
    } else if (strcmp(argv[i], "-test_algo") == 0) {
      if (strcmp(argv[i + 1], "search") == 0) {
        test_algo = GALGO::GALGO_SEARCH;
      } else if (strcmp(argv[i + 1], "wcc") == 0) {
        test_algo = GALGO::GALGO_WCC;
      } else if (strcmp(argv[i + 1], "tc") == 0) {
        test_algo = GALGO::GALGO_TC;
      } else if (strcmp(argv[i + 1], "pagerank") == 0) {
        test_algo = GALGO::GALGO_PAGERANK;
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
