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
//   Windows OS, and its memory mapping functionality.
// -----------------------------------------------------------------------------

#include "takyon_private.h"

#define MEMORY_MAP_PREFIX "_" /* This follows the Windows way: no prefix characters really needed */

struct _MmapHandle {
  HANDLE mapping_handle; // Does double duty as 'is_creator'
  LPCTSTR mapped_addr;
  uint64_t total_bytes;
  char map_name[MAX_MMAP_NAME_CHARS];
};

bool mmapAlloc(const char *map_name, uint64_t bytes, void **addr_ret, MmapHandle *mmap_handle_ret, char *error_message) {
  char full_map_name[MAX_MMAP_NAME_CHARS];
  HANDLE mapping_handle = NULL;
  LPCTSTR mapped_addr = NULL;
  size_t max_name_length = MAX_MMAP_NAME_CHARS - strlen(MEMORY_MAP_PREFIX) - 1;
  struct _MmapHandle *mmap_handle = NULL;

  if (map_name == NULL) {
    TAKYON_RECORD_ERROR(error_message, "map_name is NULL\n");
    return false;
  }
  if (strlen(map_name) > max_name_length) {
    TAKYON_RECORD_ERROR(error_message, "map_name '%s' has too many characters. Limit is %lld\n", map_name, max_name_length);
    return false;
  }
  if (addr_ret == NULL) {
    TAKYON_RECORD_ERROR(error_message, "addr_ret is NULL\n");
    return false;
  }

  // Create full map name
  snprintf(full_map_name, MAX_MMAP_NAME_CHARS, "%s%s", MEMORY_MAP_PREFIX, map_name);

  /* Verify mapping not already in use */
  mapping_handle = OpenFileMapping(FILE_MAP_ALL_ACCESS,   /* read/write access */
                                   FALSE,                 /* do not inherit the name */
                                   (TCHAR *)full_map_name);
  if (mapping_handle != NULL) {
    // This was around from a previous run, need to remove it
    // Close the handle. This should hopefully remove the underlying file
    CloseHandle(mapping_handle);
    //printf("Shared mem '%s' already exists, and was safely unlinked.\n", full_map_name);
  }

  /* Create memory map handle */
  mapping_handle = CreateFileMapping(INVALID_HANDLE_VALUE,    /* use paging file */
                                     NULL,                    /* default security */
                                     PAGE_READWRITE,          /* read/write access */
                                     0,                       /* max. object size */
                                     (DWORD)bytes,            /* buffer size */
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
    TAKYON_RECORD_ERROR(error_message, "could not create shared memory '%s'. Error: %s\n", map_name, (char *)lpMsgBuf);
    LocalFree(lpMsgBuf);
    return false;
  }

  /* Get a handle to mapped memory */
  mapped_addr = (LPTSTR)MapViewOfFile(mapping_handle, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
  if (mapped_addr == NULL) {
    CloseHandle(mapping_handle);
    TAKYON_RECORD_ERROR(error_message, "could not obtain mapped address for '%s'\n", map_name);
    return false;
  }

  /* IMPORTANT: Don't set any of the memory values here since there might be a race condition. The memory values needed to be coordinated properly by the processes ussing it. */
  *addr_ret = (void *)mapped_addr;
  /* NOTE: Not pinning memory like with Linux, but may not be an issue */

  /* Create handle and store info */
  mmap_handle = (struct _MmapHandle *)calloc(1, sizeof(struct _MmapHandle));
  if (mmap_handle == NULL) {
    UnmapViewOfFile(mapped_addr);
    CloseHandle(mapping_handle);
    TAKYON_RECORD_ERROR(error_message, "Failed to allocate the mmap handle. Out of memory.\n");
    return false;
  }

  /* Store the map info */
  strncpy(mmap_handle->map_name, full_map_name, MAX_MMAP_NAME_CHARS);
  mmap_handle->mapping_handle = mapping_handle;
  mmap_handle->mapped_addr = mapped_addr;
  mmap_handle->total_bytes = bytes;

#ifdef DEBUG_MESSAGE
  printf("CREATED: '%s' %d bytes\n", full_map_name, bytes);
#endif

  /* Set returned handle */
  *mmap_handle_ret = mmap_handle;

  return true;
}

bool mmapGet(const char *map_name, uint64_t bytes, void **addr_ret, bool *got_it_ret, MmapHandle *mmap_handle_ret, char *error_message) {
  HANDLE mapping_handle = NULL;
  LPCTSTR mapped_addr = NULL;
  size_t max_name_length = MAX_MMAP_NAME_CHARS - strlen(MEMORY_MAP_PREFIX) - 1;
  char full_map_name[MAX_MMAP_NAME_CHARS];
  struct _MmapHandle *mmap_handle = NULL;

  if (map_name == NULL) {
    TAKYON_RECORD_ERROR(error_message, "map_name is NULL\n");
    return false;
  }
  if (strlen(map_name) > max_name_length) {
    TAKYON_RECORD_ERROR(error_message, "map_name '%s' has too many characters. Limit is %lld\n", map_name, max_name_length);
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

  /* Verify mapping not already in use */
  mapping_handle = OpenFileMapping(FILE_MAP_ALL_ACCESS,   /* read/write access */
                                   FALSE,                 /* do not inherit the name */
                                   (TCHAR *)full_map_name);
  if (mapping_handle == NULL) {
    /* The memory does not exists, but this is not an error. The caller can just see if the return address is null to know it doesn't exist. */
    *addr_ret = NULL;
    *got_it_ret = 0;
    return true;
  }

  /* Get a handle to remote mapped memory */
  mapped_addr = (LPTSTR)MapViewOfFile(mapping_handle, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
  if (mapped_addr == NULL) {
    CloseHandle(mapping_handle);
    TAKYON_RECORD_ERROR(error_message, "Could not obtain address mapping for '%s'\n", map_name);
    return false;
  }
  *addr_ret = (void *)mapped_addr;

  /* Close the file descriptor. This will not un map the the shared memory */
  CloseHandle(mapping_handle);

  /* Create handle and store info */
  mmap_handle = (struct _MmapHandle *)calloc(1, sizeof(struct _MmapHandle));
  if (mmap_handle == NULL) {
    TAKYON_RECORD_ERROR(error_message, "Failed to allocate the mmap handle. Out of memory.\n");
    return false;
  }
  strncpy(mmap_handle->map_name, full_map_name, MAX_MMAP_NAME_CHARS);
  mmap_handle->mapping_handle = NULL;
  mmap_handle->mapped_addr = mapped_addr;
  mmap_handle->total_bytes = bytes;

#ifdef DEBUG_MESSAGE
  printf("FOUND: '%s' %d bytes\n", full_map_name, bytes);
#endif

  *got_it_ret = 1;
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
      TAKYON_RECORD_ERROR(error_message, "uni_mmap_get() failed");
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
    TAKYON_RECORD_ERROR(error_message, "The mmap is NULL\n");
    return false;
  }

  /* Unmap memory address */
  UnmapViewOfFile(mmap_handle->mapped_addr);

  /* Delete memory map */
  if (mmap_handle->mapping_handle != NULL) {
    /* Remove map */
    CloseHandle(mmap_handle->mapping_handle);
  }

  /* Free the handle */
  mmap_handle->map_name[0] = '\0';
  mmap_handle->mapped_addr = NULL;
  mmap_handle->mapping_handle = NULL;
  mmap_handle->total_bytes = 0;
  free(mmap_handle);

  return true;
}
