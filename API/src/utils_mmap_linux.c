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

// -----------------------------------------------------------------------------
// Description:
//   Some helpful inter-process memory allocation functions specific for
//   Unix based OSes, and its memory mapping functionality.
// -----------------------------------------------------------------------------

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/file.h>
#include "takyon_private.h"

#define MEMORY_MAP_PREFIX   "/"  // Prefix required by Posix

struct _MmapHandle {
  bool is_creator;
  void *mapped_addr;
  uint64_t total_bytes;
  char map_name[MAX_MMAP_NAME_CHARS];
};

bool mmapAlloc(const char *map_name, uint64_t bytes, void **addr_ret, MmapHandle *mmap_handle_ret, char *error_message) {
  char full_map_name[MAX_MMAP_NAME_CHARS];
  int mapping_fd = 0;
  void *mapped_addr = NULL;
  unsigned int max_name_length = MAX_MMAP_NAME_CHARS - (unsigned int)strlen(MEMORY_MAP_PREFIX) - 1;
  int rc;
  struct _MmapHandle *mmap_handle = NULL;

  if (map_name == NULL) {
    TAKYON_RECORD_ERROR(error_message, "map_name is NULL\n");
    return false;
  }
  if (strlen(map_name) > max_name_length) {
    TAKYON_RECORD_ERROR(error_message, "map_name '%s' has too many characters. Limit is %d\n", map_name, max_name_length);
    return false;
  }
  if (addr_ret == NULL) {
    TAKYON_RECORD_ERROR(error_message, "addr_ret is NULL\n");
    return false;
  }

  /* Create full map name */
  snprintf(full_map_name, MAX_MMAP_NAME_CHARS, "%s%s", MEMORY_MAP_PREFIX, map_name);

  /* Verify mapping not already in use */
  mapping_fd = shm_open(full_map_name, O_RDWR, S_IRUSR | S_IWUSR);
  if (mapping_fd != -1) {
    /* This must be lingering around from a previous run */
    /* Close the handle */
    close(mapping_fd);
    /* Unlink the old */
    rc = shm_unlink(full_map_name);
    if (rc == -1) {
      TAKYON_RECORD_ERROR(error_message, "Could not unlink old memory map '%s'\n", full_map_name);
      return false;
    }
    //printf("Shared mem '%s' already exists, and was safely unlinked.\n", full_map_name);
  }

  /* Create memory map handle */
  mapping_fd = shm_open(full_map_name, O_CREAT | O_RDWR | O_EXCL, S_IRUSR | S_IWUSR);
  if (mapping_fd == -1) {
    TAKYON_RECORD_ERROR(error_message, "could not create shared memory '%s' (errno=%d)\n", full_map_name, errno);
    return false;
  }

  /* Set the size of the shared memory */
  rc = ftruncate(mapping_fd, bytes);
  if (rc == -1) {
    close(mapping_fd);
    TAKYON_RECORD_ERROR(error_message, "could not set shared memory size '%s' to %lld bytes\n", full_map_name, (unsigned long long)bytes);
    return false;
  }

  /* Get a handle to mapped memory */
  mapped_addr = mmap(NULL/*addr*/, bytes, PROT_READ | PROT_WRITE, MAP_SHARED /* | MAP_HASSEMAPHORE */, mapping_fd, 0/*offset*/);
  if (mapped_addr == (void *)-1)  {
    close(mapping_fd);
    TAKYON_RECORD_ERROR(error_message, "could not obtain mapped address for '%s'\n", full_map_name);
    return false;
  }

  /* Close the file descriptor. This will not un map the the shared memory */
  close(mapping_fd);

  *addr_ret = (void *)mapped_addr;

  /* Pin memory so it doesn't get swapped out */
  mlock(*addr_ret, bytes);

  /* IMPORTANT: Don't set any of the memory values here since there might be a race condition. The memory values needed to be coordinated properly by the processes ussing it. */

  /* Create handle and store info */
  mmap_handle = (struct _MmapHandle *)calloc(1, sizeof(struct _MmapHandle));
  if (mmap_handle == NULL) {
    TAKYON_RECORD_ERROR(error_message, "Failed to allocate the shared mem handle. Out of memory.\n");
    return false;
  }
  strncpy(mmap_handle->map_name, full_map_name, MAX_MMAP_NAME_CHARS);
  mmap_handle->is_creator = true;
  mmap_handle->mapped_addr = mapped_addr;
  mmap_handle->total_bytes = bytes;

  /* Set returned handle */
  *mmap_handle_ret = mmap_handle;

  return true;
}

bool mmapGet(const char *map_name, uint64_t bytes, void **addr_ret, bool *got_it_ret, MmapHandle *mmap_handle_ret, char *error_message) {
  int mapping_fd = 0;
  void *mapped_addr = NULL;
  unsigned int max_name_length = MAX_MMAP_NAME_CHARS - (unsigned int)strlen(MEMORY_MAP_PREFIX) - 1;
  char full_map_name[MAX_MMAP_NAME_CHARS];
  struct _MmapHandle *mmap_handle = NULL;

  if (map_name == NULL) {
    TAKYON_RECORD_ERROR(error_message, "map_name is NULL\n");
    return false;
  }
  if (strlen(map_name) > max_name_length) {
    TAKYON_RECORD_ERROR(error_message, "map_name '%s' has too many characters. Limit is %d\n", map_name, max_name_length);
    return false;
  }
  if (addr_ret == NULL) {
    TAKYON_RECORD_ERROR(error_message, "addr_ret is NULL\n");
    return false;
  }
  if (got_it_ret == NULL) {
    TAKYON_RECORD_ERROR(error_message, "got_it_ret is NULL\n");
    return false;
  }

  /* Create full map name */
  snprintf(full_map_name, MAX_MMAP_NAME_CHARS, "%s%s", MEMORY_MAP_PREFIX, map_name);

  /* Obtain the existing mapped file */
  mapping_fd = shm_open(full_map_name, O_RDWR, S_IRUSR | S_IWUSR);
  if (mapping_fd == -1) {
    /* The memory does not exists, but this is not an error. The caller can just see if the return address is null to know it doesn't exist. */
    *addr_ret = NULL;
    *got_it_ret = false;
    return true;
  }

  /* Get a handle to remote mapped memory */
  mapped_addr = mmap(NULL/*addr*/, bytes, PROT_READ | PROT_WRITE, MAP_SHARED /* | MAP_HASSEMAPHORE */, mapping_fd, 0/*offset*/);
  if (mapped_addr == (void *)-1) {
    close(mapping_fd);
    TAKYON_RECORD_ERROR(error_message, "Could not obtain address mapping for '%s'\n", map_name);
    return false;
  }
  *addr_ret = (void *)mapped_addr;

  /* Close the file descriptor. This will not un map the the shared memory */
  close(mapping_fd);

  /* Create handle and store info */
  mmap_handle = (struct _MmapHandle *)calloc(1, sizeof(struct _MmapHandle));
  if (mmap_handle == NULL) {
    TAKYON_RECORD_ERROR(error_message, "Failed to allocate the shared mem handle. Out of memory.\n");
    return false;
  }
  strncpy(mmap_handle->map_name, full_map_name, MAX_MMAP_NAME_CHARS);
  mmap_handle->is_creator = false;
  mmap_handle->mapped_addr = mapped_addr;
  mmap_handle->total_bytes = bytes;

  /* Set returned handle */
  *got_it_ret = true;
  *mmap_handle_ret = mmap_handle;

  return true;
}

bool mmapGetTimed(const char *mmap_name, uint64_t bytes, void **addr_ret, MmapHandle *mmap_handle_ret, int64_t timeout_ns, bool *timed_out_ret, char *error_message) {
  int attempts = 0;

  /* Get the current time so it's known when to timeout */
  if (timed_out_ret != NULL) *timed_out_ret = false;
  int64_t start_time_ns = clockTimeNanoseconds();

  /* Wait until mmap is available since remote side might not have created it yet */
  while (1) {
    bool got_it;
    if (mmapGet(mmap_name, bytes, addr_ret, &got_it, mmap_handle_ret, error_message) != 1) {
      TAKYON_RECORD_ERROR(error_message, "Failed to get shared memory");
      return false;
    }

    if (got_it) {
      /* Got a handle to the remote memory, so break out of loop */
      return true;
    }

    /* Check if timed out */
    int64_t ellapsed_time_ns = clockTimeNanoseconds() - start_time_ns;
    if (timeout_ns == TAKYON_NO_WAIT) {
      if (timed_out_ret != NULL) *timed_out_ret = true;
      return true;
    } else if (timeout_ns >= 0) {
      if (ellapsed_time_ns >= timeout_ns) {
        if (timed_out_ret != NULL) *timed_out_ret = true;
        return true;
      }
    }

    /* Delay a little */
    /* Let 1000 microseconds pass and give it another try */
    int64_t micro_seconds_sleep = 1000;
    clockSleepYield(micro_seconds_sleep);
    attempts++;
  }
}

bool mmapFree(MmapHandle mmap_handle, char *error_message) {
  if (mmap_handle == NULL) {
    TAKYON_RECORD_ERROR(error_message, "The shared mem is NULL\n");
    return false;
  }

  /* Unmap memory address */
  int rc = munmap(mmap_handle->mapped_addr, mmap_handle->total_bytes);
  if (rc == -1) {
    TAKYON_RECORD_ERROR(error_message, "Could not un map memory map '%s'\n", mmap_handle->map_name);
    return false;
  }

  /* Delete memory map */
  if (mmap_handle->is_creator) {
    /* Unpin memory */
    munlock(mmap_handle->mapped_addr, mmap_handle->total_bytes);
    /* Unlink */
    rc = shm_unlink(mmap_handle->map_name);
    if (rc == -1) {
      TAKYON_RECORD_ERROR(error_message, "Could not unlink memory map '%s'\n", mmap_handle->map_name);
      return false;
    }
  }

  /* Free the handle */
  mmap_handle->map_name[0] = '\0';
  mmap_handle->mapped_addr = NULL;
  mmap_handle->is_creator = false;
  mmap_handle->total_bytes = 0;
  free(mmap_handle);

  return true;
}
