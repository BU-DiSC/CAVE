#include "Serializer.hpp"
#include "parallel_hashmap/phmap.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

template <class T> class SimpleCache {
private:
  Serializer *sz;
  int cache_size;
  std::mutex hand_mtx;

  std::vector<T> cache_block_vec;
  std::vector<int> cached_block_id;

  int clock_hand = 0;

  uint32_t lock_count = 0;
  uint64_t lock_sum_ns = 0, lock_min_ns = 0, lock_max_ns = 0;
  double lock_mean_ns = 0;
  double lock_var_ns = 0;

  using PhMap = phmap::parallel_flat_hash_map<
      int, int, phmap::priv::hash_default_hash<int>,
      phmap::priv::hash_default_eq<int>,
      std::allocator<std::pair<const int, int>>, 8, std::mutex>;
  PhMap cache_ph_map;

  std::vector<int> cache_ref_count;
  std::vector<std::atomic_bool> block_pinned;
  int num_free_blocks;

  std::vector<int> cache_status; // -1: Invalid, 0: Allocated & not read yet, 1:
                                 // Reads finished.

public:
  SimpleCache(Serializer *_sz, int _cache_size)
      : sz(_sz), cache_size(_cache_size) {
    clear();
  }
  int request_block(int block_id, int ref = 1);
  void fill_block(int cb_idx, int block_id);
  T *get_cache_block(uint32_t cb_idx);
  void release_cache_block(uint32_t cb_idx);
  void clear();

  void update_lock_cost(uint64_t x) {
    lock_count++;
    lock_sum_ns += x;

    auto delta = x - lock_mean_ns;
    lock_mean_ns += delta / lock_count;
    lock_var_ns += delta * (x - lock_mean_ns);

    if (lock_count > 1) {
      lock_min_ns = std::min(lock_min_ns, x);
      lock_max_ns = std::max(lock_max_ns, x);
    } else {
      lock_min_ns = lock_max_ns = x;
    }
  }

  uint32_t get_lock_count() { return lock_count; }
  uint64_t get_lock_sum_ns() { return lock_sum_ns; }
  uint64_t get_lock_min_ns() { return lock_min_ns; }
  uint64_t get_lock_max_ns() { return lock_max_ns; }
  double get_lock_mean_ns() { return lock_mean_ns; }
  double get_lock_var_ns() { return lock_var_ns / (lock_count - 1); }
};
