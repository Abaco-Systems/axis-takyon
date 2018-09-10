// Copyright 2018 Abaco Systems
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "takyon_extensions.h"

#ifdef _WIN32

#define MEMORY_MAP_PREFIX "_"    // This follows the Windows way: no prefix characters really needed, but matches the internals of Takyon memory maps

struct _TakyonMmapHandle {
  HANDLE mapping_handle; // Does double duty as 'is_creator'
  LPCTSTR mapped_addr;
  uint64_t total_bytes;
  char map_name[TAKYON_MAX_MMAP_NAME_CHARS];
};

void takyonMmapAlloc(const char *map_name, uint64_t bytes, void **addr_ret, TakyonMmapHandle *mmap_handle_ret) {
  char full_map_name[TAKYON_MAX_MMAP_NAME_CHARS];
  HANDLE mapping_handle = NULL;
  LPCTSTR mapped_addr = NULL;
  size_t max_name_length = TAKYON_MAX_MMAP_NAME_CHARS - strlen(MEMORY_MAP_PREFIX) - 1;
  struct _TakyonMmapHandle *mmap_handle = NULL;

  if (map_name == NULL) {
    fprintf(stderr, "map_name is NULL\n");
    exit(EXIT_FAILURE);
  }
  if (strlen(map_name) > max_name_length) {
    fprintf(stderr, "map_name '%s' has too many characters. Limit is %lld\n", map_name, max_name_length);
    exit(EXIT_FAILURE);
  }
  if (addr_ret == NULL) {
    fprintf(stderr, "addr_ret is NULL\n");
    exit(EXIT_FAILURE);
  }

  // Create full map name
  snprintf(full_map_name, TAKYON_MAX_MMAP_NAME_CHARS, "%s%s", MEMORY_MAP_PREFIX, map_name);

  // Verify mapping not already in use
  mapping_handle = OpenFileMapping(FILE_MAP_ALL_ACCESS,   // read/write access
                                   FALSE,                 // do not inherit the name
                                   (TCHAR *)full_map_name);
  if (mapping_handle != NULL) {
    // This was around from a previous run, need to remove it
    // Close the handle. This should hopefully remove the underlying file
    CloseHandle(mapping_handle);
    //printf("Shared mem '%s' already exists, and was safely unlinked.\n", full_map_name);
  }

  // Create memory map handle
  mapping_handle = CreateFileMapping(INVALID_HANDLE_VALUE,    // use paging file
                                     NULL,                    // default security
                                     PAGE_READWRITE,          // read/write access
                                     0,                       // max. object size
                                     (DWORD)bytes,            // buffer size
                                     (TCHAR *)full_map_name);
  if (mapping_handle == NULL) {
    LPVOID lpMsgBuf;
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                  NULL,
                  GetLastError(),
                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
                  (LPTSTR) &lpMsgBuf,
                  0,
                  NULL);
    fprintf(stderr, "Could not create shared memory '%s'. Error: %s\n", map_name, (char *)lpMsgBuf);
    LocalFree(lpMsgBuf);
    exit(EXIT_FAILURE);
  }

  // Get a handle to mapped memory
  mapped_addr = (LPTSTR)MapViewOfFile(mapping_handle, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
  if (mapped_addr == NULL) {
    fprintf(stderr, "Could not obtain mapped address for '%s'\n", map_name);
    exit(EXIT_FAILURE);
  }

  *addr_ret = (void *)mapped_addr;

  // IMPORTANT: Don't set any of the memory values here since there might be a race condition. The memory values needed to be coordinated properly by the processes ussing it.

  // Create handle and store info
  mmap_handle = (struct _TakyonMmapHandle *)calloc(1, sizeof(struct _TakyonMmapHandle));
  if (mmap_handle == NULL) {
    fprintf(stderr, "Failed to allocate the mmap handle. Out of memory.\n");
    exit(EXIT_FAILURE);
  }
  strncpy(mmap_handle->map_name, full_map_name, TAKYON_MAX_MMAP_NAME_CHARS);
  mmap_handle->mapping_handle = mapping_handle;
  mmap_handle->mapped_addr = mapped_addr;
  mmap_handle->total_bytes = bytes;

#ifdef DEBUG_MESSAGE
  printf("CREATED: '%s' %d bytes\n", full_map_name, bytes);
#endif

  // Set returned handle
  *mmap_handle_ret = mmap_handle;
}

void takyonMmapFree(TakyonMmapHandle mmap_handle) {
  if (mmap_handle == NULL) {
    fprintf(stderr, "The mmap is NULL\n");
    exit(EXIT_FAILURE);
  }

  // Unmap memory address
  UnmapViewOfFile(mmap_handle->mapped_addr);

  // Delete memory map
  if (mmap_handle->mapping_handle != NULL) {
    // Remove map
    CloseHandle(mmap_handle->mapping_handle);
  }

  // Free the handle
  free(mmap_handle);
}

#else // Posix mmap (Linux, Mac)

#include <errno.h>
#include <sys/mman.h>
#include <sys/file.h>

#define MEMORY_MAP_PREFIX   "/"  // Prefix required by Posix, and matches the internals of Takyon memory maps

struct _TakyonMmapHandle {
  bool is_creator;
  void *mapped_addr;
  uint64_t total_bytes;
  char map_name[TAKYON_MAX_MMAP_NAME_CHARS];
};

void takyonMmapAlloc(const char *map_name, uint64_t bytes, void **addr_ret, TakyonMmapHandle *mmap_handle_ret) {
  char full_map_name[TAKYON_MAX_MMAP_NAME_CHARS];
  int mapping_fd = 0;
  void *mapped_addr = NULL;
  unsigned int max_name_length = TAKYON_MAX_MMAP_NAME_CHARS - (unsigned int)strlen(MEMORY_MAP_PREFIX) - 1;
  int rc;
  struct _TakyonMmapHandle *mmap_handle = NULL;

  if (map_name == NULL) {
    fprintf(stderr, "map_name is NULL\n");
    exit(EXIT_FAILURE);
  }
  if (strlen(map_name) > max_name_length) {
    fprintf(stderr, "map_name '%s' has too many characters. Limit is %d\n", map_name, max_name_length);
    exit(EXIT_FAILURE);
  }
  if (addr_ret == NULL) {
    fprintf(stderr, "addr_ret is NULL\n");
    exit(EXIT_FAILURE);
  }

  // Create full map name
  snprintf(full_map_name, TAKYON_MAX_MMAP_NAME_CHARS, "%s%s", MEMORY_MAP_PREFIX, map_name);

  // Verify mapping not already in use
  mapping_fd = shm_open(full_map_name, O_RDWR, S_IRUSR | S_IWUSR);
  if (mapping_fd != -1) {
    // This must be lingering around from a previous run
    // Close the handle
    close(mapping_fd);
    // Unlink the old
    rc = shm_unlink(full_map_name);
    if (rc == -1) {
      fprintf(stderr, "Could not unlink old memory map '%s'\n", full_map_name);
      exit(EXIT_FAILURE);
    }
    //printf("Shared mem '%s' already exists, and was safely unlinked.\n", full_map_name);
  }

  // Create memory map handle
  mapping_fd = shm_open(full_map_name, O_CREAT | O_RDWR | O_EXCL, S_IRUSR | S_IWUSR);
  if (mapping_fd == -1) {
    fprintf(stderr, "Could not create shared memory '%s' (errno=%d)\n", full_map_name, errno);
    exit(EXIT_FAILURE);
  }

  // Set the size of the shared memory
  rc = ftruncate(mapping_fd, bytes);
  if (rc == -1) {
    close(mapping_fd);
    fprintf(stderr, "Could not set shared memory size '%s' to %lld bytes\n", full_map_name, (unsigned long long)bytes);
    exit(EXIT_FAILURE);
  }

  // Get a handle to mapped memory
  mapped_addr = mmap(NULL/*addr*/, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, mapping_fd, 0/*offset*/);
  if (mapped_addr == (void *)-1)  {
    close(mapping_fd);
    fprintf(stderr, "Could not obtain mapped address for '%s'\n", full_map_name);
    exit(EXIT_FAILURE);
  }

  // Close the file descriptor. This will not un map the the shared memory
  close(mapping_fd);

  *addr_ret = (void *)mapped_addr;

  // Pin memory so it doesn't get swapped out
  mlock(*addr_ret, bytes);

  // IMPORTANT: Don't set any of the memory values here since there might be a race condition. The memory values needed to be coordinated properly by the processes ussing it.

  // Create handle and store info
  mmap_handle = (struct _TakyonMmapHandle *)calloc(1, sizeof(struct _TakyonMmapHandle));
  if (mmap_handle == NULL) {
    fprintf(stderr, "Failed to allocate the shared mem handle. Out of memory.\n");
    exit(EXIT_FAILURE);
  }
  strncpy(mmap_handle->map_name, full_map_name, TAKYON_MAX_MMAP_NAME_CHARS);
  mmap_handle->is_creator = true;
  mmap_handle->mapped_addr = mapped_addr;
  mmap_handle->total_bytes = bytes;

  // Set returned handle
  *mmap_handle_ret = mmap_handle;
}

void takyonMmapFree(TakyonMmapHandle mmap_handle) {
  if (mmap_handle == NULL) {
    fprintf(stderr, "The shared mem is NULL\n");
    exit(EXIT_FAILURE);
  }

  // Unmap memory address
  int rc = munmap(mmap_handle->mapped_addr, mmap_handle->total_bytes);
  if (rc == -1) {
    fprintf(stderr, "Could not un map memory map '%s'\n", mmap_handle->map_name);
    exit(EXIT_FAILURE);
  }

  // Delete memory map
  if (mmap_handle->is_creator) {
    // Unpin memory
    munlock(mmap_handle->mapped_addr, mmap_handle->total_bytes);
    // Unlink
    rc = shm_unlink(mmap_handle->map_name);
    if (rc == -1) {
      fprintf(stderr, "Could not unlink memory map '%s', errno=%d\n", mmap_handle->map_name, errno);
      exit(EXIT_FAILURE);
    }
  }

  // Free the handle
  free(mmap_handle);
}

#endif
