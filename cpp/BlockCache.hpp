#include "Serializer.hpp"
#include <memory>
#include <mutex>
#include <shared_mutex>

template <class T> class BlockCache {
private:
  Serializer *sz;
  int cache_size;
  std::mutex index_mtx;
  std::vector<std::shared_ptr<T>> cache_list;
  std::vector<int> c_to_bid;
  int clock_hand = 0;
  std::unordered_map<int, int> cache_map;
  std::vector<std::atomic<int>> cache_usage_count;
  std::vector<std::shared_mutex> cache_mtx;

public:
  BlockCache(Serializer *_sz, int _cache_size)
      : sz(_sz), cache_size(_cache_size) {}
  std::shared_ptr<T> get_block(int block_id);
  std::vector<std::shared_ptr<T>> get_blocks(int block_id, int count);
  void clear();
};
