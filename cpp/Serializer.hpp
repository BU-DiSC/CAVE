#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>

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

#define S_BLOCK_SIZE 4096
#define QD 256

enum MODE { SYNC_READ, ASYNC_READ, WRITE, IN_MEMORY, INVALID };

struct MetaBlock {
  int num_nodes;
  int num_blocks;
  int num_vertex_blocks;
  int num_edge_blocks;
} __attribute__((aligned(4096)));

struct Vertex {
  int key;
  int degree;
  int edge_block_id;
  int edge_block_offset;
};

#define VB_CAPACITY 256
struct VertexBlock {
  struct Vertex vertices[VB_CAPACITY];
} __attribute__((aligned(4096)));

#define EB_CAPACITY 1024
struct EdgeBlock {
  int edges[EB_CAPACITY];
} __attribute__((aligned(4096)));

#define MAX_DEGREE 1022
struct S_Node {
  int key;                        // 4 Bytes
  uint16_t degree;                // 2 Byte, degree (0~65535)
  uint16_t p_size;                // 2 Byte, payload size (0~65535)
  uint32_t data[MAX_DEGREE];      // 4096-8 Bytes, edges then payloads
} __attribute__((aligned(4096))); // GCC extension to align a struct

#ifdef _WIN32
class EXT_OVERLAPPED : public OVERLAPPED {
public:
  std::shared_ptr<S_Node> recv_snode;
  int stack_id;
  EXT_OVERLAPPED(int _offset, int _stack_id) {
    Offset = _offset;
    OffsetHigh = 0;
    hEvent = NULL;
    recv_snode = std::make_shared<S_Node>();
    stack_id = _stack_id;
  }
};
#elif __linux__
struct Uring_data {
  std::shared_ptr<S_Node> s_node;
  int stack_id;
};
#endif

class Serializer {
public:
  Serializer();
  ~Serializer();

  bool write_block(int block_id, void *data);
  bool write_blocks(int first_block_id, void* data, size_t count);

  template <class T> std::shared_ptr<T> read_block(int block_id);
  template <class T>
  std::vector<std::shared_ptr<T>> read_blocks(int first_block_id, int count);

  // bool req_snode_async(int node_id, int stack_id);
  // std::shared_ptr<S_Node> get_one_snode(int &stack_id);

  bool init_mapped_file();

  bool wait_all_signals(int type = 0);

  void open_file(std::string file_path, MODE mode);

  void clear();
  void clear_signals();
  // void send_end_signal();
  // void consume_one_signal();

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
