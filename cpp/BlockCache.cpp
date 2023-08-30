#include "BlockCache.hpp"
#include <atomic>
#include <cassert>
#include <mutex>
#include <shared_mutex>

template <class T> void BlockCache<T>::clear() {
  cache_block_vec = std::vector<T>(cache_size);
  cached_block_id = std::vector<int>(cache_size, -1);
  clock_hand = 0;
  cache_ref_count = std::vector<std::atomic_int>(cache_size);
  // cache_ref_count = std::vector<int>(cache_size, 0);

  cache_pinned_count = std::vector<std::atomic_int>(cache_size);
  cache_mtx = std::vector<std::mutex>(cache_size);
  // cache_mtx2 = std::vector<std::mutex>(cache_size);
  cache_status = std::vector<int>(cache_size, -1);

  for (int i = 0; i < cache_size; i++) {
    cache_ref_count[i] = 0;
    cache_pinned_count[i] = 0;
  }
  num_free_blocks = cache_size;
  cache_ph_map.clear();
}

template <class T> void BlockCache<T>::clock_step() {
  // Make sure have hand_mtx before calling this function
  clock_hand = (clock_hand + 1) % cache_size;
}

template <class T> int BlockCache<T>::request_block(int block_id) {
  int cb_idx = -1;

  // Is cached?
  if (cache_ph_map.if_contains(block_id, [&cb_idx](const PhMap::value_type &v) {
        cb_idx = v.second;
      })) {

    int val = cache_pinned_count[cb_idx].load();
    while (true) {
      // Failed, some thread has written it
      if (val == -1 || cached_block_id[cb_idx] != block_id)
        break;

      if (cache_pinned_count[cb_idx].compare_exchange_strong(val, val + 1) &&
          cached_block_id[cb_idx] == block_id) {
        cache_ref_count[cb_idx].fetch_add(1);
        return cb_idx;
      }
    }

    // std::unique_lock evict_lock(cache_mtx2[cb_idx]);
    // if (cached_block_id[cb_idx] == block_id) {
    //   cache_pinned_count[cb_idx]++;
    //   cache_ref_count[cb_idx]++;
    //   return cb_idx;
    // }
  }

  std::unique_lock hand_lock(hand_mtx);
  // Check again if it has been cached by another thread
  if (cache_ph_map.if_contains(block_id, [&cb_idx](const PhMap::value_type &v) {
        cb_idx = v.second;
      })) {
    cache_pinned_count[cb_idx].fetch_add(1);
    cache_ref_count[cb_idx].fetch_add(1);
    return cb_idx;
  }

  // 2. Free blocks left
  if (num_free_blocks > 0) {
    num_free_blocks--;
    for (cb_idx = clock_hand;; cb_idx = (cb_idx + 1) % cache_size) {
      cb_idx = clock_hand;
      if (cache_status[cb_idx] == -1) {
        assert(cache_ph_map.try_emplace_l(
                   block_id, [](PhMap::value_type &v) {}, cb_idx) == true);
        cached_block_id[cb_idx] = block_id;
        cache_pinned_count[cb_idx].store(1);
        cache_ref_count[cb_idx].store(1);
        cache_status[cb_idx] = 0;

        clock_hand = (cb_idx + 1) % cache_size;
        break;
      }
    }
    return cb_idx;
  }

  // 3. Find a block to evict
  // int max_tries = 64;
  for (cb_idx = clock_hand;; cb_idx = (cb_idx + 1) % cache_size) {
    // if (--max_tries == 0) {
    //   return -1;
    // }

    if (cache_pinned_count[cb_idx] == 0 && --cache_ref_count[cb_idx] == 0) {
      int val = 0;
      if (!cache_pinned_count[cb_idx].compare_exchange_strong(val, -1)) {
        continue;
      }

      // std::unique_lock evict_lock(cache_mtx2[cb_idx], std::defer_lock);

      // if (!evict_lock.try_lock() || cache_pinned_count[cb_idx] > 0) {
      //   clock_step();
      //   continue;
      // }

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
      cache_pinned_count[cb_idx].store(1);
      cache_ref_count[cb_idx].store(1);
      cache_status[cb_idx] = 0;

      clock_hand = (cb_idx + 1) % cache_size;
      break;
    }
  }
  return cb_idx;
}

template <class T> T *BlockCache<T>::get_cache_block(int cb_idx, int block_id) {

  if (cb_idx == -1) {
    T *temp_block = new T();
    sz->read_block(block_id, temp_block);
    return temp_block;
  }

  assert(block_id == cached_block_id[cb_idx]);

  // Double-checked locking
  if (cache_status[cb_idx] == 0) {
    std::unique_lock read_lock(cache_mtx[cb_idx]);
    if (cache_status[cb_idx] == 0) {
      sz->read_block(block_id, &cache_block_vec[cb_idx]);
      cache_status[cb_idx] = 1;
    }
  }

  return &cache_block_vec[cb_idx];
}

template <class T>
void BlockCache<T>::release_cache_block(int cb_idx, T *block_ptr) {
  if (cb_idx == -1)
    delete block_ptr;
  else
    cache_pinned_count[cb_idx].fetch_sub(1);
}

template class BlockCache<EdgeBlock>;
