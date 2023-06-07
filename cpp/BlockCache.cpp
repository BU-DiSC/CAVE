#include "BlockCache.hpp"
#include <cassert>
#include <memory>

template <class T> void BlockCache<T>::clear() {
  cache_list = std::vector<std::shared_ptr<T>>(cache_size);
  c_to_bid = std::vector<int>(cache_size, -1);
  clock_hand = 0;
  cache_usage_count = std::vector<std::atomic<int>>(cache_size);
  cache_mtx = std::vector<std::shared_mutex>(cache_size);
  for (int i = 0; i < cache_size; i++) {
    cache_usage_count[i] = 0;
  }
  cache_map.clear();
}

template <class T> std::shared_ptr<T> BlockCache<T>::get_block(int block_id) {
  std::shared_ptr<T> ret_ptr;

  index_mtx.lock();
  bool is_cached = cache_map.find(block_id) != cache_map.end();

  // If cached
  if (is_cached) {
    // Get the cached block
    int cache_idx = cache_map.at(block_id);
    assert(c_to_bid[cache_idx] == block_id);
    {
      std::shared_lock read_lk(cache_mtx[cache_idx]);
      index_mtx.unlock();
      ret_ptr = cache_list[cache_idx];
      cache_usage_count[cache_idx]++;
    }
  } else {
    int new_cache_idx;
    while (true) {
      if (cache_usage_count[clock_hand] == 0 &&
          cache_mtx[clock_hand].try_lock()) {
        int old_block_id = c_to_bid[clock_hand];
        if (old_block_id != -1)
          cache_map.erase(old_block_id);
        cache_map[block_id] = clock_hand;

        c_to_bid[clock_hand] = block_id;
        cache_usage_count[clock_hand] = 1;
        new_cache_idx = clock_hand;

        // Move the clock hand
        clock_hand++;
        if (clock_hand == cache_size)
          clock_hand = 0;

        index_mtx.unlock();

        std::shared_ptr<T> eb = sz->read_block<T>(block_id);
        ret_ptr = eb;
        cache_list[new_cache_idx] = eb;
        cache_mtx[new_cache_idx].unlock();
        break;
      }
      if (cache_usage_count[clock_hand] > 0)
        cache_usage_count[clock_hand]--;
      // Move the clock hand
      clock_hand++;
      if (clock_hand == cache_size)
        clock_hand = 0;
    }
  }

  return ret_ptr;
}

template <class T>
std::vector<std::shared_ptr<T>> BlockCache<T>::get_blocks(int block_id,
                                                          int count) {
  std::vector<std::shared_ptr<T>> ret_ptrs(count);

  for (int i = 0; i < count; i++) {
    ret_ptrs[i] = this->get_block(block_id + i);
  }
  return ret_ptrs;
}

template class BlockCache<EdgeBlock>;
