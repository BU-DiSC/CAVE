#include "Serializer.hpp"
#include "parallel_hashmap/phmap.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>


template <class T> class BlockCache {
private:
  Serializer *sz;
  int cache_size;
  std::mutex hand_mtx;

  std::vector<T> cache_block_vec;
  std::vector<int> cached_block_id;

  // std::atomic_int clock_hand;
  int clock_hand = 0;
  

  // std::unordered_map<int, int> cache_map;
  using PhMap = phmap::parallel_flat_hash_map<int, int, phmap::priv::hash_default_hash<int>,
                                phmap::priv::hash_default_eq<int>,
                                std::allocator<std::pair<const int, int>>, 6,
                                std::mutex>;
  PhMap cache_ph_map;

  std::vector<std::atomic_int> cache_ref_count;
  // std::vector<int> cache_ref_count;

  std::vector<std::atomic_int> cache_pinned_count;
  std::vector<std::mutex> cache_mtx;
  std::vector<std::mutex> cache_mtx2;

  // std::atomic_int num_free_blocks;
  int num_free_blocks;

  // std::vector<std::atomic_int> cache_status;
  std::vector<int> cache_status;
  void clock_step();

public:
  BlockCache(Serializer *_sz, int _cache_size)
      : sz(_sz), cache_size(_cache_size) {
    clear();
  }
  int request_block(int block_id);
  T *get_cache_block(int cb_idx, int block_id);
  void release_cache_block(int cb_idx);
  void clear();
};
