#include "Serializer.hpp"
#include <cstddef>
#include <cwchar>
#include <memory>
#include <vector>

Serializer::Serializer() : mode_internal(MODE::INVALID) {
#ifndef _WIN32
  fd = -1;
#endif
  pend_writes = 0;
  pend_reads = 0;
}

Serializer::~Serializer() { this->clear(); }

void Serializer::clear() {
#ifdef _WIN32
  CancelIo(handle_file);
  CloseHandle(handle_file);
  if (mode_internal == MODE::ASYNC_READ)
    CloseHandle(handle_port);
#else
  close(fd);
  if (mode_internal == MODE::ASYNC_READ || mode_internal == MODE::WRITE) {
    io_queue_release(ctx);
    // io_uring_queue_exit(&ring);
  }
#endif
  // delete mapped_data;
}

void Serializer::clear_signals() {
  if (mode_internal != MODE::ASYNC_READ)
    return;
#ifdef _WIN32
  CancelIo(handle_file);
  this->wait_all_signals();
#else
  this->wait_all_signals();
  io_queue_release(ctx);
  // io_uring_queue_exit(&ring);
  this->prep_queue();
#endif
}

void Serializer::open_file(std::string file_path, MODE mode) {
  mode_internal = mode;
#ifdef _WIN32
  DWORD dwflags =
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED;
  switch (mode) {
  case MODE::IN_MEMORY:
  case MODE::SYNC_READ: // SYNC_READ
    handle_file = CreateFileA(file_path.c_str(), GENERIC_READ, 0, NULL,
                              OPEN_EXISTING, dwflags, NULL);
    if (handle_file == INVALID_HANDLE_VALUE) {
      fprintf(stderr, "[ERROR]: Failed to create SYNC_READ file handle.\n");
      exit(1);
    }
    break;
  case MODE::ASYNC_READ: // ASYNC READ
    handle_file = CreateFileA(file_path.c_str(), GENERIC_READ, 0, NULL,
                              OPEN_EXISTING, dwflags, NULL);
    if (handle_file == INVALID_HANDLE_VALUE) {
      fprintf(stderr, "[ERROR]: Failed to create ASYNC_READ file handle.\n");
      exit(1);
    }
    // Use IOCP for better async read performance
    handle_port = CreateIoCompletionPort(handle_file, NULL, 0, port_con);
    if (handle_port == INVALID_HANDLE_VALUE) {
      fprintf(stderr, "[ERROR]: Failed to create ASYNC_READ IOCP handle.\n");
      exit(1);
    }
    break;
  case MODE::WRITE: // WRITE
    handle_file = CreateFileA(file_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                              0, NULL, CREATE_ALWAYS, dwflags, NULL);
    if (handle_file == INVALID_HANDLE_VALUE) {
      fprintf(stderr, "[ERROR]: Failed to create WRITE file handle.\n");
      exit(1);
    }
    handle_port = CreateIoCompletionPort(handle_file, NULL, 0, port_con);
    if (handle_port == INVALID_HANDLE_VALUE) {
      fprintf(stderr, "[ERROR]: Failed to create ASYNC_READ IOCP handle.\n");
      exit(1);
    }
    break;
  default:
    break;
  }
#else
  int flags = 0;
  switch (mode) {
  case MODE::IN_MEMORY:
  case MODE::SYNC_READ: // SYNC_READ
    flags |= O_RDONLY | O_DIRECT;
    break;
  case MODE::ASYNC_READ: // ASYNC_READ
    flags |= O_RDONLY | O_DIRECT;
    break;
  case MODE::WRITE: // WRITE
    flags |= O_RDWR | O_CREAT | O_DIRECT;
    break;
  default:
    break;
  }
  fd = open(file_path.c_str(), flags, 0777);
  if (fd < 0) {
    fprintf(stderr, "[ERROR]: Open file error.\n");
    exit(1);
  }
#endif
  if (mode == MODE::IN_MEMORY) {
    if (!init_mapped_file()) {
      fprintf(stderr, "Map file error.\n");
    }
  }
}

bool Serializer::init_mapped_file() {
  if (this->mode_internal != MODE::IN_MEMORY)
    return false;
  auto begin = std::chrono::high_resolution_clock::now();
  void *buf;
#ifdef _WIN32
  HANDLE hMappedFile =
      CreateFileMappingA(handle_file, NULL, PAGE_READONLY, 0, 0, NULL);
  if (hMappedFile == NULL) {
    printf("File Mapping Error %ld.\n", GetLastError());
    exit(1);
  }
  buf = MapViewOfFile(hMappedFile, FILE_MAP_READ, 0, 0, 0);
  if (buf == NULL) {
    printf("MapViewOfFile Error %ld.\n", GetLastError());
    exit(1);
  }
#elif __linux__
  struct stat desc;
  fstat(fd, &desc);
  buf = mmap(NULL, desc.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if (buf == NULL) {
    printf("File Mapping Error %d.\n", errno);
    exit(1);
  }
#endif
  this->mapped_data = (char *)buf;

  auto end = std::chrono::high_resolution_clock::now();
  int64_t ms_int =
      std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
          .count();
  fprintf(stderr, "Mapped file in %zd microseconds.\n", ms_int);
  return true;
}

void Serializer::prep_queue() {
#ifdef _WIN32
  depth = QD;
#elif __linux__
  io_queue_init(QD, &ctx);
  // io_uring_queue_init(QD, &ring, 0);
  depth = QD;
#endif
}

void Serializer::handle_write_cqe() {
  while (pend_writes >= depth) {
    // printf("pend_writes: %d\n", pend_writes);
#ifdef _WIN32
    OVERLAPPED *ol;
    DWORD byte;
    ULONG_PTR comp_key;
    if (!GetQueuedCompletionStatus(handle_port, &byte, &comp_key,
                                   (LPOVERLAPPED *)&ol, INFINITE)) {
      printf("[ERROR]: finish_write Error %ld.\n", GetLastError());
      exit(1);
    }
    delete ol;
    pend_writes--;
#elif __linux__
    // Linux libaio implementation
    struct io_event ios[1];
    int res = io_getevents(ctx, 1, 1, ios, NULL);
    if (res <= 0) {
      fprintf(stderr, "[ERROR]: Write CQE %s.\n", strerror(-res));
      exit(1);
    }

    // // Linux liburing implementation
    // struct io_uring_cqe * cqe;
    // io_uring_wait_cqe(&ring, &cqe);
    // if (cqe->res < 0) {
    //   printf("[ERROR]: Write CQE %s\n", strerror(-cqe->res));
    //   exit(1);
    // }
    // io_uring_cqe_seen(&ring, cqe);

    pend_writes--;
#endif
  }
}

void Serializer::finish_write() {
  depth = 1;
  this->handle_write_cqe();
}

bool Serializer::write_block(int block_id, void *data, size_t size) {
  // Check mode
  if (mode_internal != MODE::WRITE) {
    fprintf(stderr, "[ERROR]: Serializer in an invalid state\n");
    return false;
  }

  size_t offset = block_id * S_BLOCK_SIZE;

#ifdef _WIN32
  OVERLAPPED *ol = new OVERLAPPED();
  ol->hEvent = NULL;
  ol->Offset = offset;
  ol->OffsetHigh = (offset >> 32);

  if (!WriteFile(handle_file, data, size, NULL, ol)) {
    auto err = GetLastError();
    if (err != ERROR_IO_PENDING) {
      fprintf(stderr, "[ERROR]: WriteNode Err: %ld\n", err);
      exit(1);
    }
  }
  pend_writes++;
  if (pend_writes >= QD) {
    this->handle_write_cqe();
  }
  return true;
#elif __linux__
  // Linux libaio implementation
  struct iocb *ios = new iocb[1];
  io_prep_pwrite(&ios[0], fd, data, size, offset);
  int res = io_submit(ctx, 1, &ios);

  while (res <= 0) {
    if (res == -EAGAIN) {
      this->handle_write_cqe();
      res = io_submit(ctx, 1, &ios);
    } else {
      fprintf(stderr, "[ERROR]: write_node %s.\n", strerror(-res));
      exit(1);
    }
  }
  pend_writes++;
  if (pend_writes >= QD) {
    this->handle_write_cqe();
  }
  delete[] ios;
  return true;

  // // Linux io_uring implementation
  // struct io_uring_sqe *sqe;
  // sqe = io_uring_get_sqe(&ring);

  // while (!sqe) {
  //   io_uring_submit(&ring);
  //   this->handle_write_cqe();
  //   sqe = io_uring_get_sqe(&ring);
  // }

  // io_uring_sqe_set_data(sqe, nullptr);
  // io_uring_prep_write(sqe, fd, data, size, 0);
  // pend_writes++;

  // // Ring is full, submit ring and wait cqes.
  // if (pend_writes >= QD) {
  //   io_uring_submit(&ring);
  //   this->handle_write_cqe();
  // }
  // return true;
#endif
  return false;
}

template <class T> std::shared_ptr<T> Serializer::read_block(int block_id) {
  size_t offset = block_id * S_BLOCK_SIZE;
  auto block_ptr = std::make_shared<T>();

  if (this->mode_internal == MODE::IN_MEMORY) {
    memcpy(block_ptr.get(), mapped_data + offset, sizeof(T));
    return block_ptr;
  }

#ifdef _WIN32
  OVERLAPPED ol;
  ol.hEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
  ol.Offset = offset;
  ol.OffsetHigh = (offset >> 32);
  DWORD bytes;
  if (!ReadFile(handle_file, block_ptr.get(), sizeof(T), &bytes, &ol)) {
    auto err = GetLastError();
    if (err != ERROR_IO_PENDING) {
      fprintf(stderr, "[ERROR] SYNC ReadFile %zu Err: %ld\n", offset, err);
      exit(1);
    } else {
      if (!GetOverlappedResult(handle_file, &ol, &bytes, TRUE)) {
        err = GetLastError();
        fprintf(stderr, "[ERROR] SYNC GetOverlappedResult Err: %ld\n", err);
        exit(1);
      }
    }
  }
  CloseHandle(ol.hEvent);
#else
  ssize_t bytes = pread(fd, (char *)block_ptr.get(), sizeof(T), offset);
#endif
  if (bytes <= 0) {
    fprintf(stderr, "[ERROR]: Could not read block: Offset %zu\n", offset);
    exit(1);
  }
  return block_ptr;
}

template std::shared_ptr<EdgeBlock> Serializer::read_block(int block_id);
template std::shared_ptr<VertexBlock> Serializer::read_block(int block_id);
template std::shared_ptr<MetaBlock> Serializer::read_block(int block_id);

template <class T>
std::vector<std::shared_ptr<T>> Serializer::read_blocks(int start_block_id,
                                                        int count) {
  size_t offset = start_block_id * S_BLOCK_SIZE;
  size_t buf_size = sizeof(T) * count;
  auto block_ptrs = std::vector<std::shared_ptr<T>>(count);

  for (int i = 0; i < count; i++) {
    block_ptrs[i] = std::make_shared<T>();
  }

  if (this->mode_internal == MODE::IN_MEMORY) {
    for (int i = 0; i < count; i++) {
      memcpy(block_ptrs[i].get(), mapped_data + offset + i * sizeof(T),
             sizeof(T));
    }
    return block_ptrs;
  }

#ifdef _WIN32
  char *buf = (char *)_aligned_malloc(buf_size, S_BLOCK_SIZE);
#elif __linux__
  char *buf = (char *)aligned_alloc(S_BLOCK_SIZE, buf_size);
#endif

#ifdef _WIN32
  OVERLAPPED ol;
  ol.hEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
  ol.Offset = offset;
  ol.OffsetHigh = (offset >> 32);
  DWORD bytes;
  if (!ReadFile(handle_file, buf, buf_size, &bytes, &ol)) {
    auto err = GetLastError();
    if (err != ERROR_IO_PENDING) {
      fprintf(stderr, "[ERROR] SYNC ReadFile %zu Err: %ld\n", offset, err);
      exit(1);
    } else {
      if (!GetOverlappedResult(handle_file, &ol, &bytes, TRUE)) {
        err = GetLastError();
        fprintf(stderr, "[ERROR] SYNC GetOverlappedResult Err: %ld\n", err);
        exit(1);
      }
    }
  }
  CloseHandle(ol.hEvent);
#else
  ssize_t bytes = pread(fd, buf, buf_size, offset);
#endif
  if (bytes <= 0) {
    fprintf(stderr,
            "[ERROR]: Could not read blocks: Bytes %zd, Offset %zu, size %zu\n",
            bytes, offset, buf_size);
    fprintf(stderr, "%s\n", strerror(errno));
    exit(1);
  }
  for (int i = 0; i < count; i++) {
    memcpy(block_ptrs[i].get(), buf + i * sizeof(T), sizeof(T));
  }

  return block_ptrs;
}
template std::vector<std::shared_ptr<EdgeBlock>>
Serializer::read_blocks(int start_block_id, int count);
template std::vector<std::shared_ptr<VertexBlock>>
Serializer::read_blocks(int start_block_id, int count);

bool Serializer::wait_all_signals(int type) {
  int max_wait_ms = 50;
#ifdef _WIN32
  DWORD byte;
  ULONG_PTR comp_key;
  while (1) {
    if (type == 0) {
      EXT_OVERLAPPED *eol;
      if (!GetQueuedCompletionStatus(handle_port, &byte, &comp_key,
                                     (LPOVERLAPPED *)&eol, max_wait_ms)) {
        if (!eol) {
          // Timeout
          // printf("[INFO]: IOCP Port Clear!\n");
          return true;
        } else {
          printf("[ERROR]: wait_all_signals: GetStatus Err %ld.\n",
                 GetLastError());
          return false;
        }
      } else {
        delete eol;
      }
    } else if (type == 1) {
      OVERLAPPED *ol;
      if (!GetQueuedCompletionStatus(handle_port, &byte, &comp_key,
                                     (LPOVERLAPPED *)&ol, max_wait_ms)) {
        if (!ol) {
          // Timeout
          // printf("[INFO]: IOCP Port Clear!\n");
          return true;
        } else {
          printf("[ERROR]: wait_all_signals: GetStatus Err %ld.\n",
                 GetLastError());
          return false;
        }
      } else {
        delete ol;
      }
    }
  }
#else
  // Linux libaio implementation
  struct timespec max_wait_ns = {.tv_sec = 0, .tv_nsec = max_wait_ms * 100000};
  // Recollect in batch 32
  struct io_event ios[32];
  while (1) {
    // Timeout
    int res = io_getevents(ctx, 1, 32, ios, &max_wait_ns);
    if (res <= 0)
      break;

    for (int i = 0; i < res; i++) {
      // Get data
      Uring_data *ud = (Uring_data *)ios[i].data;
      delete ud;
    }
  }

  // // Linux liburing implementation
  // struct io_uring_cqe *cqe;
  // struct __kernel_timespec max_wait_ns = {.tv_sec = 0,
  //                                         .tv_nsec = max_wait_ms * 100000};
  // while (1) {
  //   // Timeout
  //   int res = io_uring_wait_cqe_timeout(&ring, &cqe, &max_wait_ns);
  //   if (res <= 0)
  //     break;
  //   Uring_data *ud = (Uring_data *)io_uring_cqe_get_block(cqe);
  //   delete ud;
  //   // Flag it as seen
  //   io_uring_cqe_seen(&ring, cqe);
  // }
#endif
  return true;
}
