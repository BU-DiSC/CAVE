#include "CacheSimple.hpp"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <mutex>
#include <shared_mutex>
#include <vector>

template <class T> void SimpleCache<T>::clear() {
  cache_block_vec = std::vector<T>(cache_size);
  cached_block_id = std::vector<int>(cache_size, -1);
  cache_status = std::vector<int>(cache_size, -1);
  block_pinned = std::vector<std::atomic_bool>(cache_size);
  cache_ref_count = std::vector<int>(cache_size, 0);

  for (int i = 0; i < cache_size; i++) {
    block_pinned[i].store(false);
  }

  num_free_blocks = cache_size;
  cache_ph_map.clear();
  clock_hand = 0;
}

template <class T> int SimpleCache<T>::request_block(int block_id, int ref) {
  int cb_idx = -1;

  // Is cached?
  if (cache_ph_map.if_contains(block_id, [&cb_idx](const PhMap::value_type &v) {
        cb_idx = v.second;
      })) {

    bool is_pinned = false;
    if (block_pinned[cb_idx].compare_exchange_strong(is_pinned, true)) {
      cache_ref_count[cb_idx] += ref;
      return cb_idx;
    }
  }

  std::unique_lock hand_lock(hand_mtx);
  // 2. Free blocks left
  if (num_free_blocks > 0) {
    num_free_blocks--;
    for (cb_idx = clock_hand;; cb_idx = (cb_idx + 1) % cache_size) {
      if (cache_status[cb_idx] == -1) {
        assert(cache_ph_map.try_emplace_l(
                   block_id, [](PhMap::value_type &v) {}, cb_idx) == true);
        cached_block_id[cb_idx] = block_id;
        block_pinned[cb_idx].store(true);
        cache_ref_count[cb_idx] = ref;
        cache_status[cb_idx] = 0;
        clock_hand = (cb_idx + 1) % cache_size;
        break;
      }
    }
    return cb_idx;
  }

  // 3. Find a block to evict
  for (cb_idx = clock_hand;; cb_idx = (cb_idx + 1) % cache_size) {
    if (!block_pinned[cb_idx] && --cache_ref_count[cb_idx] == 0) {
      bool is_pinned = false;
      if (!block_pinned[cb_idx].compare_exchange_strong(is_pinned, true)) {
        continue;
      }

      int old_block_id = cached_block_id[cb_idx];

      // Erase old block in map
      assert(
          cache_ph_map.erase_if(old_block_id, [&cb_idx](PhMap::value_type &v) {
            return v.second == cb_idx;
          }) == true);
      // Add new block to map
      assert(cache_ph_map.try_emplace_l(
                 block_id, [](PhMap::value_type &v) {}, cb_idx) == true);

      cached_block_id[cb_idx] = block_id;
      block_pinned[cb_idx].store(true);
      cache_ref_count[cb_idx] = ref;
      cache_status[cb_idx] = 0;

      clock_hand = (cb_idx + 1) % cache_size;
      break;
    }
  }
  return cb_idx;
}

template <class T> void SimpleCache<T>::fill_block(int cb_idx, int block_id) {
  assert(block_id == cached_block_id[cb_idx]);

  sz->read_block(block_id, &cache_block_vec[cb_idx]);
  cache_status[cb_idx] = 1;
}

template <class T> T *SimpleCache<T>::get_block(int block_id) {
  int cb_idx = cache_ph_map[block_id];
  return &cache_block_vec[cb_idx];
}

template <class T> void SimpleCache<T>::release_cache_block(int block_id) {
  int cb_idx = cache_ph_map[block_id];
  block_pinned[cb_idx].store(false);
}

template class SimpleCache<EdgeBlock>;
