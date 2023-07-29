#include "BlockCache.hpp"
#include <cassert>
#include <memory>
#include <vector>

template <class T> void BlockCache<T>::clear() {
  cache_block_vec = std::vector<T>(cache_size);
  c_to_bid = std::vector<int>(cache_size, -1);
  clock_hand = 0;
  cache_ref_count = std::vector<std::atomic<int>>(cache_size);
  cache_pinned_count = std::vector<std::atomic<int>>(cache_size);
  for (int i = 0; i < cache_size; i++) {
    cache_ref_count[i] = 0;
    cache_pinned_count[i] = 0;
  }
  cache_map.clear();
}

template <class T> int BlockCache<T>::request_block(int block_id) {
  int cache_block_idx = -1;

  index_mtx.lock();
  auto cache_map_iter = cache_map.find(block_id);

  // If cached
  if (cache_map_iter != cache_map.end()) {
    // Get the cached block
    cache_block_idx = cache_map_iter->second;
    assert(c_to_bid[cache_block_idx] == block_id);

    cache_pinned_count[cache_block_idx]++;
    cache_ref_count[cache_block_idx]++;

    // Unlock index mtx
    index_mtx.unlock();
  } else { // Not cached
    while (true) {
      if (cache_pinned_count[clock_hand] > 0) {
        // Cache block in use (pinned), do nothing.
      } else {
        // Not in use (not pinned)
        if (cache_ref_count[clock_hand].fetch_sub(1) == 1) {
          // and ref count = 0
          int old_block_id = c_to_bid[clock_hand];
          if (old_block_id != -1)
            cache_map.erase(old_block_id);
          cache_map[block_id] = clock_hand;

          // Modify flags on cached block
          cache_block_idx = clock_hand;
          c_to_bid[cache_block_idx] = block_id;
          cache_pinned_count[cache_block_idx] = 1;
          cache_ref_count[cache_block_idx] = 1;

          // Move the clock hand
          clock_hand++;
          if (clock_hand == cache_size)
            clock_hand = 0;

          index_mtx.unlock();

          sz->read_block(block_id, &cache_block_vec[cache_block_idx]);
          break;
        }
      }
    }
  }

  return cache_block_idx;
}

template <class T> T *BlockCache<T>::get_cache_block(int cache_block_idx) {
  if (cache_block_idx < 0) {
    fprintf(stderr, "Cache block idx %d < 0!", cache_block_idx);
    exit(1);
  }
  return &cache_block_vec[cache_block_idx];
}

template <class T>
void BlockCache<T>::release_cache_block(int cache_block_idx) {
  if (cache_block_idx < 0) {
    fprintf(stderr, "Cache block idx %d < 0!", cache_block_idx);
    exit(1);
  }
  cache_pinned_count[cache_block_idx]--;
}

// template <class T>
// std::vector<T *> BlockCache<T>::get_blocks(int block_id, int count) {
//   std::vector<T *> ret_ptrs(count);

//   for (int i = 0; i < count; i++) {
//     ret_ptrs[i] = this->get_block(block_id + i);
//   }
//   return ret_ptrs;
// }

template class BlockCache<EdgeBlock>;
