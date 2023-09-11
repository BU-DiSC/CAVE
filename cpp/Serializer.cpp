#include "Serializer.hpp"
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <unistd.h>
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
  if (mode_internal == MODE::ASYNC_READ) {
    io_queue_release(ctx);
    // io_uring_queue_exit(&ring);
  }
#endif
  // delete mapped_data;
}

uint64_t Serializer::get_size_mb() {
#ifdef _WIN32
  BY_HANDLE_FILE_INFORMATION h_file_info;
  GetFileInformationByHandle(handle_file, &h_file_info);
  uint64_t size_bytes =
      h_file_info.nFileSizeLow + ((uint64_t)h_file_info.nFileSizeHigh << 32);
  return size_bytes >> 10;
#elif __linux__
  struct stat64 stat_buf;
  fstat64(fd, &stat_buf);
  uint64_t size_bytes = stat_buf.st_size;
  return size_bytes >> 10;
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
    flags |= O_RDWR | O_CREAT | O_DIRECT | O_TRUNC;
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
#endif
  }
}

void Serializer::finish_write() {
  depth = 1;
  this->handle_write_cqe();
}

bool Serializer::write_meta_block(void *data) {
  // Check mode
  if (mode_internal != MODE::WRITE) {
    fprintf(stderr, "[ERROR]: Serializer in an invalid state\n");
    return false;
  }

  size_t offset = 0;

#ifdef _WIN32
  OVERLAPPED *ol = new OVERLAPPED();
  ol->hEvent = NULL;
  ol->Offset = offset;
  ol->OffsetHigh = (offset >> 32);

  if (!WriteFile(handle_file, data, sizeof(MetaBlock), NULL, ol)) {
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
  auto res = pwrite64(fd, data, sizeof(MetaBlock), offset);
  if (res <= 0) {
    fprintf(stderr, "[ERROR]: pwrite64 %s.\n", strerror(errno));
    return false;
  }
  return true;
#endif
  return false;
}

template <class T> bool Serializer::write_block(int block_id, void *data) {
  // Check mode
  if (mode_internal != MODE::WRITE) {
    fprintf(stderr, "[ERROR]: Serializer in an invalid state\n");
    return false;
  }

  size_t offset = (size_t)block_id * sizeof(T) + sizeof(MetaBlock);

#ifdef _WIN32
  OVERLAPPED *ol = new OVERLAPPED();
  ol->hEvent = NULL;
  ol->Offset = offset;
  ol->OffsetHigh = (offset >> 32);

  if (!WriteFile(handle_file, data, sizeof(T), NULL, ol)) {
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
  auto res = pwrite64(fd, data, sizeof(T), offset);
  if (res <= 0) {
    fprintf(stderr, "[ERROR]: pwrite64 %s.\n", strerror(errno));
    return false;
  }
  return true;
#endif
  return false;
}

template <class T>
bool Serializer::write_blocks(int first_block_id, void *data, size_t count) {
  // Check mode
  if (mode_internal != MODE::WRITE) {
    fprintf(stderr, "[ERROR]: Serializer in an invalid state\n");
    return false;
  }

  size_t offset = (size_t)first_block_id * sizeof(T) + sizeof(MetaBlock);

#ifdef _WIN32
  OVERLAPPED *ol = new OVERLAPPED();
  ol->hEvent = NULL;
  ol->Offset = offset;
  ol->OffsetHigh = (offset >> 32);

  if (!WriteFile(handle_file, data, sizeof(T) * count, NULL, ol)) {
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
  auto res = pwrite64(fd, data, sizeof(T) * count, offset);
  if (res <= 0) {
    fprintf(stderr, "[ERROR]: pwrite64 %s.\n", strerror(errno));
    return false;
  }
  return true;
#endif
  return false;
}
template bool Serializer::write_blocks<VertexBlock>(int first_block_id,
                                                    void *data, size_t count);
template bool Serializer::write_blocks<EdgeBlock>(int first_block_id,
                                                  void *data, size_t count);

int Serializer::read_meta_block(MetaBlock *block_ptr) {
  size_t offset = 0;
  if (this->mode_internal == MODE::IN_MEMORY) {
    memcpy(block_ptr, mapped_data + offset, sizeof(MetaBlock));
    return 1;
  }

#ifdef _WIN32
  OVERLAPPED ol;
  ol.hEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
  ol.Offset = offset;
  ol.OffsetHigh = (offset >> 32);
  DWORD bytes;
  if (!ReadFile(handle_file, block_ptr, sizeof(MetaBlock), &bytes, &ol)) {
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
  ssize_t bytes = pread64(fd, (char *)block_ptr, sizeof(MetaBlock), offset);
#endif
  if (bytes <= 0) {
    fprintf(stderr, "[ERROR]: Could not read block: Offset %zu\n", offset);
    exit(1);
  }
  return 1;
}

template <class T> int Serializer::read_block(int block_id, T *block_ptr) {

  size_t offset = (size_t)block_id * sizeof(T) + sizeof(MetaBlock);

  if (this->mode_internal == MODE::IN_MEMORY) {
    memcpy(block_ptr, mapped_data + offset, sizeof(T));
    return 1;
  }

#ifdef _WIN32
  OVERLAPPED ol;
  ol.hEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
  ol.Offset = offset;
  ol.OffsetHigh = (offset >> 32);
  DWORD bytes;
  if (!ReadFile(handle_file, block_ptr, sizeof(T), &bytes, &ol)) {
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
  ssize_t bytes = pread64(fd, (char *)block_ptr, sizeof(T), offset);
#endif
  if (bytes <= 0) {
    fprintf(stderr, "[ERROR]: Could not read block: Offset %zu\n", offset);
    exit(1);
  }
  return 1;
}
template int Serializer::read_block(int block_id, EdgeBlock *block_ptr);
template int Serializer::read_block(int block_id, VertexBlock *block_ptr);

template <class T>
int Serializer::read_blocks(int first_block_id, size_t count,
                            std::vector<T> *block_vec) {
  size_t offset = (size_t)first_block_id * sizeof(T) + sizeof(MetaBlock);
  size_t buf_size = sizeof(T) * count;

  if (this->mode_internal == MODE::IN_MEMORY) {
    std::memcpy(block_vec->data(), mapped_data + offset, count * sizeof(T));
    return count;
  }

#ifdef _WIN32
  OVERLAPPED ol;
  ol.hEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
  ol.Offset = offset;
  ol.OffsetHigh = (offset >> 32);
  DWORD bytes;
  if (!ReadFile(handle_file, block_vec->data(), buf_size, &bytes, &ol)) {
    auto err = GetLastError();
    if (err != ERROR_IO_PENDING) {
      fprintf(stderr, "[ERROR] SYNC ReadFile %llu Err: %ld\n", offset, err);
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
  ssize_t bytes = pread64(fd, block_vec->data(), buf_size, offset);
#endif
  if (bytes <= 0) {
    fprintf(stderr,
            "[ERROR]: Could not read blocks: Bytes %zd, Offset %zu, size %zu\n",
            bytes, offset, buf_size);
    fprintf(stderr, "%s\n", strerror(errno));
    exit(1);
  }

  return count;
}
template int Serializer::read_blocks(int first_block_id, size_t count,
                                     std::vector<EdgeBlock> *block_vec);
template int Serializer::read_blocks(int first_block_id, size_t count,
                                     std::vector<VertexBlock> *block_vec);