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
//   Some helpful local memory allocation functions specific for Unix based OSes.
//   These may be helpful when a specific alignment is needed.
// -----------------------------------------------------------------------------

#include "takyon_private.h"
#include <unistd.h>
#ifdef VXWORKS_7
#include <vmLib.h>
#endif

int memoryPageSize() {
#ifdef VXWORKS_7
  return ((int)vmPageSizeGet());
#else
  return sysconf(_SC_PAGESIZE);
#endif
}

bool memoryAlloc(size_t alignment, size_t size, void **addr_ret, char *error_message) {
  // Allocated contiguous memory
  if (addr_ret == NULL) {
    TAKYON_RECORD_ERROR(error_message, "addr_ret is NULL\n");
    return false;
  }
#ifdef VXWORKS_7
  *addr_ret = memalign(alignment, size);
  if (*addr_ret == NULL) {
    TAKYON_RECORD_ERROR(error_message, "memalign() failed to allocate memory\n");
    return false;
  }
#else
  if (posix_memalign(addr_ret, alignment, size) != 0) {
    TAKYON_RECORD_ERROR(error_message, "posix_memalign() failed to allocate memory\n");
    return false;
  }
  // To avoid valgrind complaining about uninitialize memory, best to zero it
  memset(*addr_ret, 0, size);
#endif

  return true;
}

bool memoryFree(void *addr, char *error_message) {
  if (addr == NULL) {
    TAKYON_RECORD_ERROR(error_message, "addr is NULL\n");
    return false;
  }
  free(addr);
  return true;
}
