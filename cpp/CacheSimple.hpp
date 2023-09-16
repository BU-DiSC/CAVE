#include "Serializer.hpp"
#include "parallel_hashmap/phmap.h"
#include <atomic>
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
  T *get_block(int block_id);
  T *get_cache_block(uint32_t cb_idx);
  void release_cache_block(uint32_t cb_idx);
  void clear();
};
