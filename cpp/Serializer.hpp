#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif __linux__
#include <asm-generic/errno-base.h>
#include <fcntl.h>
// #include <liburing.h>
#include <bits/types/struct_timespec.h>
#include <libaio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#endif

#pragma once

#define BLOCK_SIZE 4096         // 4 KB
#define LARGE_BLOCK_SIZE 524288 // 512 KB
#define QD 256

enum MODE { SYNC_READ, ASYNC_READ, WRITE, IN_MEMORY, INVALID };

struct MetaBlock {
  int num_nodes;
  int num_blocks;
  int num_vertex_blocks;
  int num_edge_blocks;
  bool enable_large_block;
} __attribute__((aligned(4096)));

struct Vertex {
  // int key;
  // int edge_block_id;
  // int edge_block_offset;
  uint32_t degree;
  uint32_t edge_block_idx_off;
};

#define VB_CAPACITY 512
struct VertexBlock {
  struct Vertex vertices[VB_CAPACITY];
} __attribute__((aligned(4096)));

#define EB_CAPACITY 1024
#define EB_DIGITS 10
struct EdgeBlock {
  uint32_t edges[EB_CAPACITY];
} __attribute__((aligned(4096)));

#define LARGE_VB_CAPACITY (LARGE_BLOCK_SIZE / 8)
struct LargeVertexBlock {
  struct Vertex vertices[LARGE_VB_CAPACITY];
} __attribute__((aligned(4096)));

#define LARGE_EB_CAPACITY (LARGE_BLOCK_SIZE / 4)
struct LargeEdgeBlock {
  uint32_t edges[LARGE_EB_CAPACITY];
} __attribute__((aligned(4096)));

class Serializer {
public:
  Serializer();
  ~Serializer();

  // bool write_block(int block_id, void *data);
  // bool write_blocks(int first_block_id, void *data, size_t count);

  // template <class T> std::shared_ptr<T> read_block(int block_id);
  // template <class T>
  // std::vector<std::shared_ptr<T>> read_blocks(int first_block_id, int count);
  template <class T>
  int read_blocks(int first_block_id, size_t count, std::vector<T> *block_vec);

  template <class T> int read_block(int block_id, T *block_ptr);
  int read_meta_block(MetaBlock *block_ptr);

  template <class T>
  bool write_blocks(int first_block_id, void *data, size_t count);
  template <class T> bool write_block(int block_id, void *data);
  bool write_meta_block(void *data);

  bool init_mapped_file();

  void open_file(std::string file_path, MODE mode);

  void clear();

  void prep_queue();
  void finish_write();

  void handle_write_cqe();

private:
  int pend_writes = 0;
  std::atomic<int> pend_reads;
  int depth;
  char *mapped_data;
#ifdef _WIN32
  HANDLE handle_file;
  HANDLE handle_port;
  int port_con = 256;
#elif __linux__
  int fd;
  io_context_t ctx;
  // struct io_uring ring;
  std::mutex mtx, mtx_cq;

#endif
  MODE mode_internal;
};
