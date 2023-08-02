#include "BlockCache.hpp"
#include <cassert>
#include <mutex>

template <class T> void BlockCache<T>::clear() {
  cache_block_vec = std::vector<T>(cache_size);
  cached_block_id = std::vector<int>(cache_size, -1);
  clock_hand = 0;
  cache_ref_count = std::vector<std::atomic_int>(cache_size);
  // cache_ref_count = std::vector<int>(cache_size, 0);

  cache_pinned_count = std::vector<std::atomic_int>(cache_size);
  cache_mtx = std::vector<std::mutex>(cache_size);
  cache_mtx2 = std::vector<std::mutex>(cache_size);
  // cache_status = std::vector<std::atomic_int>(cache_size);
  cache_status = std::vector<int>(cache_size, -1);

  for (int i = 0; i < cache_size; i++) {
    cache_ref_count[i] = 0;
    cache_pinned_count[i] = 0;
    // cache_status[i] = -1;
  }
  num_free_blocks = cache_size;
  cache_ph_map.clear();
}

template <class T> void BlockCache<T>::clock_step() {
  // Make sure have hand_mtx before calling this function
  clock_hand = (clock_hand + 1) % cache_size;
}

template <class T> int BlockCache<T>::request_block(int block_id) {

  // Is cached?
  int val;
  if (cache_ph_map.if_contains(
          block_id, [&val](const PhMap::value_type &v) { val = v.second; })) {
    std::lock_guard<std::mutex> evict_lock(cache_mtx2[val]);
    if (cached_block_id[val] == block_id) {
      cache_pinned_count[val]++;
      cache_ref_count[val]++;
      return val;
    }
  }

  std::lock_guard<std::mutex> hand_lock(hand_mtx);
  // Check again if it has been cached by another thread
  int val2;
  if (cache_ph_map.if_contains(
          block_id, [&val2](const PhMap::value_type &v) { val2 = v.second; })) {
    cache_pinned_count[val2]++;
    cache_ref_count[val2]++;
    return val2;
  }

  // 2. Free blocks left
  if (num_free_blocks > 0) {
    num_free_blocks--;
    int cb_idx;
    while (true) {
      cb_idx = clock_hand;
      if (cache_status[cb_idx] == -1) {
        assert(cache_ph_map.try_emplace_l(
                   block_id, [](PhMap::value_type &v) {}, cb_idx) == true);
        cached_block_id[cb_idx] = block_id;
        cache_pinned_count[cb_idx] = 1;
        cache_ref_count[cb_idx] = 1;
        cache_status[cb_idx] = 0;
        clock_step();
        break;
      } else
        clock_step();
    }
    return cb_idx;
  }


  // 3. Find a block to evict
  int cb_idx;
  while (true) {
    cb_idx = clock_hand;

    if (cache_pinned_count[cb_idx] == 0 && --cache_ref_count[cb_idx] == 0) {

      std::lock_guard<std::mutex> evict_lock(cache_mtx2[cb_idx]);

      if (cache_pinned_count[cb_idx] > 0) {
        clock_step();
        continue;
      }

      int old_block_id = cached_block_id[cb_idx];

      // Erase old block in map
      assert(
          cache_ph_map.erase_if(old_block_id, [&cb_idx](PhMap::value_type &v) {
            return v.second == cb_idx;
          }) == true);
      // Add old block to map
      assert(cache_ph_map.try_emplace_l(
                 block_id, [](PhMap::value_type &v) {}, cb_idx) == true);

      cached_block_id[cb_idx] = block_id;
      cache_pinned_count[cb_idx] = 1;
      cache_ref_count[cb_idx] = 1;
      cache_status[cb_idx] = 0;

      clock_step();
      break;
    } else {
      clock_step();
    }
  }
  return cb_idx;
}

template <class T> T *BlockCache<T>::get_cache_block(int cb_idx, int block_id) {
  if (cb_idx < 0) {
    fprintf(stderr, "Cache block idx %d < 0!", cb_idx);
    exit(1);
  }

  assert(block_id == cached_block_id[cb_idx]);

  // Double-checked locking
  if (cache_status[cb_idx] == 0) {
    std::lock_guard<std::mutex> read_lock(cache_mtx[cb_idx]);
    if (cache_status[cb_idx] == 0) {
      sz->read_block(block_id, &cache_block_vec[cb_idx]);
      cache_status[cb_idx] = 1;
    }
  }

  return &cache_block_vec[cb_idx];
}

template <class T> void BlockCache<T>::release_cache_block(int cb_idx) {
  if (cb_idx < 0) {
    fprintf(stderr, "Cache block idx %d < 0!", cb_idx);
    exit(1);
  }
  cache_pinned_count[cb_idx]--;
}

template class BlockCache<EdgeBlock>;
template class BlockCache<LargeEdgeBlock>;
