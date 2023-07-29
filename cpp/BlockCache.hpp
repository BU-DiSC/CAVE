#include "Serializer.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

template <class T> class BlockCache {
private:
  Serializer *sz;
  int cache_size;
  std::mutex index_mtx;
  std::vector<T> cache_block_vec;
  std::vector<int> c_to_bid;
  int clock_hand = 0;
  std::unordered_map<int, int> cache_map;
  std::vector<std::atomic<int>> cache_ref_count;
  std::vector<std::atomic<int>> cache_pinned_count;

public:
  BlockCache(Serializer *_sz, int _cache_size)
      : sz(_sz), cache_size(_cache_size) {}
  int request_block(int block_id);
  T *get_cache_block(int cache_block_idx);
  void release_cache_block(int cache_block_idx);
  // std::vector<T *> get_blocks(int block_id, int count);
  void clear();
};
