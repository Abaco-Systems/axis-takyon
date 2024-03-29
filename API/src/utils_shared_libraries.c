// Copyright 2018,2020 Abaco Systems
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
//   Functionality needed to load shared libraries on demand instead of at
//   application load time. This allows each Takyon supported interconnect
//   interface to only be loaded if used, removing the annoyance of haveing
//   to link in all capabilites even if not used.
//   This currently works on Unix based platforms.
//   For now, it is assumed Windows statically links all Takyon interconnect
//   interfaces.
// -----------------------------------------------------------------------------

#include "takyon_private.h"
#ifdef __linux__
  #include <dlfcn.h>
#endif
#ifdef __APPLE__
  #include <dlfcn.h>
#endif

typedef struct {
  char name[MAX_FILENAME_CHARS];
  void *library;
  int counter;
} LibraryDescription;

static pthread_once_t L_once_control = PTHREAD_ONCE_INIT;
static pthread_mutex_t L_mutex;
static LibraryDescription L_loaded_libraries[MAX_INTERCONNECT_MODULES];
static int L_loaded_library_count = 0;

static void initMutexOnce(void) {
  pthread_mutex_init(&L_mutex, NULL);
}

bool sharedLibraryLoad(const char *interconnect_module, int is_verbose, char *error_message) {
  // One time init
  pthread_once(&L_once_control, initMutexOnce);

  // Need to do this atomically
  pthread_mutex_lock(&L_mutex);

  // Check to see if already loaded
  for (int i=0; i<L_loaded_library_count; i++) {
    if (strcmp(L_loaded_libraries[i].name, interconnect_module) == 0) {
      // Already loaded, so can just return
      L_loaded_libraries[i].counter++;
      pthread_mutex_unlock(&L_mutex);
      return true;
    }
  }

  // Verify enough room to load library
  if (L_loaded_library_count+1 >= MAX_INTERCONNECT_MODULES) {
    pthread_mutex_unlock(&L_mutex);
    TAKYON_RECORD_ERROR(error_message, "This implementation can only load %d unique Takyon shared interconnect libraries.\n", MAX_INTERCONNECT_MODULES);
    return false;
  }

  // Build the shared object library name
#ifdef BUILD_STATIC_LIB
  // Nothing to do
#else
  /*
#ifdef _WIN32
  char library_name[MAX_FILENAME_CHARS];
  snprintf(library_name, MAX_FILENAME_CHARS, "Takyon%s.dll", interconnect_module);
#endif
  */
  char library_name[MAX_FILENAME_CHARS];
  snprintf(library_name, MAX_FILENAME_CHARS, "libTakyon%s.so", interconnect_module);
  char full_library_path[MAX_FILENAME_CHARS];
  const char *folder_name = getenv("TAKYON_LIBS");
  if (folder_name == NULL) {
    pthread_mutex_unlock(&L_mutex);
    TAKYON_RECORD_ERROR(error_message, "The environment variable 'TAKYON_LIBS' needs to be set to point to the Takyon shared libraries folder containing %s.\n", library_name);
    return false;
  }
  int folder_name_length = (int)strlen(folder_name);
  int library_name_length = (int)strlen(library_name);
  snprintf(full_library_path, MAX_FILENAME_CHARS, "%.*s/%.*s", folder_name_length, folder_name, library_name_length, library_name);
  if (is_verbose) {
    printf("Using Takyon interconnect library: '%s'\n", full_library_path);
  }
#endif

  // Load the shared library
#ifdef BUILD_STATIC_LIB
  void *lib_handle = (void *)1;
#else
  /* DLLs for Windows reference
#ifdef _WIN32
  HINSTANCE lib_handle = LoadLibrary(full_library_path);
  if (lib_handle == NULL) {
    pthread_mutex_unlock(&L_mutex);
    TAKYON_RECORD_ERROR(error_message, "Failed to load shared library '%s'.\n", full_library_path);
    return false;
  }
#endif
  */
  // Load options:
  //   - RTLD_LAZY: If specified, Linux is not concerned about unresolved symbols until they are referenced.
  //   - RTLD_NOW: All unresolved symbols resolved when dlopen() is called.
  //   - RTLD_GLOBAL: Make symbol libraries visible.
  void *lib_handle = dlopen(full_library_path, RTLD_LAZY);
  if (lib_handle == NULL) {
    pthread_mutex_unlock(&L_mutex);
    TAKYON_RECORD_ERROR(error_message, "Failed to load shared library '%s'.\nError: %s\n", full_library_path, dlerror());
    return false;
  }
#endif

  // Store results in table
  strncpy(L_loaded_libraries[L_loaded_library_count].name, interconnect_module, MAX_FILENAME_CHARS);
  L_loaded_libraries[L_loaded_library_count].library = lib_handle;
  L_loaded_libraries[L_loaded_library_count].counter = 1;
  L_loaded_library_count++;

  // Release atomic lock
  pthread_mutex_unlock(&L_mutex);

  return true;
}

bool sharedLibraryUnload(const char *interconnect_module, char *error_message) {
  bool status = true;

  // Need to do this atomically
  pthread_mutex_lock(&L_mutex);

  // Find the module
  for (int i=0; i<L_loaded_library_count; i++) {
    if (strcmp(L_loaded_libraries[i].name, interconnect_module) == 0) {
      // Found it
      L_loaded_libraries[i].counter--;
      if (L_loaded_libraries[i].counter == 0) {
        // Un load the library
#ifdef BUILD_STATIC_LIB
        // Nothing to do
#else
        /*
#ifdef _WIN32
        if (FreeLibrary(L_loaded_libraries[i].library) == 0) {
          TAKYON_RECORD_ERROR(error_message, "ERROR: failed to close dynamic library '%s'\n", L_loaded_libraries[i].name);
          status = false;
        }
#endif
        */
        if (dlclose(L_loaded_libraries[i].library) == -1) {
          TAKYON_RECORD_ERROR(error_message, "ERROR: failed to close dynamic library '%s': %s\n", L_loaded_libraries[i].name, dlerror());
          status = false;
        }
#endif
        // Remove from list
        for (int j=i+1; j<L_loaded_library_count; j++) {
          L_loaded_libraries[j-1] = L_loaded_libraries[j];
        }
        L_loaded_library_count--;
      }
      break;
    }
  }

  pthread_mutex_unlock(&L_mutex);
  return status;
}

bool sharedLibraryGetInterconnectFunctionPointers(const char *interconnect_module, TakyonPrivatePath *private_path, char *error_message) {
  // Need to do this atomically
  pthread_mutex_lock(&L_mutex);

  // Check to see if already loaded
  void *lib_handle = NULL;
  for (int i=0; i<L_loaded_library_count; i++) {
    if (strcmp(L_loaded_libraries[i].name, interconnect_module) == 0) {
      lib_handle = L_loaded_libraries[i].library;
      break;
    }
  }
  if (lib_handle == NULL) {
    pthread_mutex_unlock(&L_mutex);
    TAKYON_RECORD_ERROR(error_message, "Failed to lookup the shared library handle for the interconnect '%s'.\n", interconnect_module);
    return false;
  }

#ifdef BUILD_STATIC_LIB
  if (strcmp(interconnect_module, "InterThreadMemcpy") == 0) {
    setInterThreadMemcpyFunctionPointers(private_path);
  } else if (strcmp(interconnect_module, "InterThreadPointer") == 0) {
    setInterThreadPointerFunctionPointers(private_path);
#ifndef VXWORKS_7
  } else if (strcmp(interconnect_module, "InterProcessMemcpy") == 0) {
    setInterProcessMemcpyFunctionPointers(private_path);
  } else if (strcmp(interconnect_module, "InterProcessPointer") == 0) {
    setInterProcessPointerFunctionPointers(private_path);
  } else if (strcmp(interconnect_module, "InterProcessSocket") == 0) {
    setInterProcessSocketFunctionPointers(private_path);
#endif
  } else if (strcmp(interconnect_module, "Socket") == 0) {
    setSocketFunctionPointers(private_path);
  } else if (strcmp(interconnect_module, "OneSidedSocket") == 0) {
    setOneSidedSocketFunctionPointers(private_path);
  } else if (strcmp(interconnect_module, "UnicastSendSocket") == 0) {
    setUnicastSendSocketFunctionPointers(private_path);
  } else if (strcmp(interconnect_module, "UnicastRecvSocket") == 0) {
    setUnicastRecvSocketFunctionPointers(private_path);
  } else if (strcmp(interconnect_module, "MulticastSendSocket") == 0) {
    setMulticastSendSocketFunctionPointers(private_path);
  } else if (strcmp(interconnect_module, "MulticastRecvSocket") == 0) {
    setMulticastRecvSocketFunctionPointers(private_path);
#ifdef WITH_RDMA
  } else if (strcmp(interconnect_module, "Rdma") == 0) {
    setRdmaFunctionPointers(private_path);
  } else if (strcmp(interconnect_module, "UnicastSendRdma") == 0) {
    setUnicastSendRdmaFunctionPointers(private_path);
  } else if (strcmp(interconnect_module, "UnicastRecvRdma") == 0) {
    setUnicastRecvRdmaFunctionPointers(private_path);
  } else if (strcmp(interconnect_module, "MulticastSendRdma") == 0) {
    setMulticastSendRdmaFunctionPointers(private_path);
  } else if (strcmp(interconnect_module, "MulticastRecvRdma") == 0) {
    setMulticastRecvRdmaFunctionPointers(private_path);
#endif
  } else {
    pthread_mutex_unlock(&L_mutex);
    TAKYON_RECORD_ERROR(error_message, "Failed to find the functions for the interconnect module '%s'.\n", interconnect_module);
    return false;
  }

#else
  /* reference for Windows
#ifdef _WIN32
  typedef bool tknCreate(TakyonPath *);
  typedef bool tknSend(TakyonPath *, int, uint64_t, uint64_t, uint64_t, bool *);
  typedef bool tknIsSent(TakyonPath *, int, bool *);
  typedef bool tknRecv(TakyonPath *, int, uint64_t *, uint64_t *, bool *);
  typedef bool tknDestroy(TakyonPath *);

  private_path->tknCreate = (tknCreate *)GetProcAddress(lib_handle, "tknCreate");
  if (private_path->tknCreate == NULL) {
    pthread_mutex_unlock(&L_mutex);
    TAKYON_RECORD_ERROR(error_message, "Failed to find the function 'create()' in interconnect module '%s'.\n", interconnect_module);
    return false;
  }
  private_path->tknSend = (tknSend *)GetProcAddress(lib_handle, "tknSend");
  if (private_path->tknSend == NULL) {
    pthread_mutex_unlock(&L_mutex);
    TAKYON_RECORD_ERROR(error_message, "Failed to find the function 'send()' in interconnect module '%s'.\n", interconnect_module);
    return false;
  }
  private_path->tknIsSent = (tknIsSent *)GetProcAddress(lib_handle, "tknIsSent");
  if (private_path->tknIsSent == NULL) {
    pthread_mutex_unlock(&L_mutex);
    TAKYON_RECORD_ERROR(error_message, "Failed to find the function 'tknIsSent()' in interconnect module '%s'.\n", interconnect_module);
    return false;
  }
  private_path->tknRecv = (tknRecv *)GetProcAddress(lib_handle, "tknRecv");
  if (private_path->tknRecv == NULL) {
    pthread_mutex_unlock(&L_mutex);
    TAKYON_RECORD_ERROR(error_message, "Failed to find the function 'recv()' in interconnect module '%s'.\n", interconnect_module);
    return false;
  }
  private_path->tknDestroy = (tknDestroy *)GetProcAddress(lib_handle, "tknDestroy");
  if (private_path->tknDestroy == NULL) {
    pthread_mutex_unlock(&L_mutex);
    TAKYON_RECORD_ERROR(error_message, "Failed to find the function 'destroy()' in interconnect module '%s'.\n", interconnect_module);
    return false;
  }
#endif
  */

  private_path->tknCreate = dlsym(lib_handle, "tknCreate");
  if (private_path->tknCreate == NULL) {
    pthread_mutex_unlock(&L_mutex);
    TAKYON_RECORD_ERROR(error_message, "Failed to find the function 'tknCreate()' in interconnect module '%s'.\nError: %s\n", interconnect_module, dlerror());
    return false;
  }
  private_path->tknSend = dlsym(lib_handle, "tknSend");
  /* No need to error check since it's OK if an interconnect does not support sending
  if (private_path->tknSend == NULL) {
    pthread_mutex_unlock(&L_mutex);
    TAKYON_RECORD_ERROR(error_message, "Failed to find the function 'tknSend()' in interconnect module '%s'.\nError: %s\n", interconnect_module, dlerror());
    return false;
  }
  */
  private_path->tknIsSent = dlsym(lib_handle, "tknIsSent");
  /* No need to error check since it's OK if an interconnect does not support non blocking sends
  if (private_path->tknIsSent == NULL) {
    pthread_mutex_unlock(&L_mutex);
    TAKYON_RECORD_ERROR(error_message, "Failed to find the function 'tknIsSent()' in interconnect module '%s'.\nError: %s\n", interconnect_module, dlerror());
    return false;
  }
  */
  private_path->tknPostRecv = dlsym(lib_handle, "tknPostRecv");
  /* No need to error check since it's OK if an interconnect does not support posting receives
  if (private_path->tknPostRecv == NULL) {
    pthread_mutex_unlock(&L_mutex);
    TAKYON_RECORD_ERROR(error_message, "Failed to find the function 'tknPostRecv()' in interconnect module '%s'.\nError: %s\n", interconnect_module, dlerror());
    return false;
  }
  */
  private_path->tknRecv = dlsym(lib_handle, "tknRecv");
  /* No need to error check since it's OK if an interconnect does not support receiving
  if (private_path->tknRecv == NULL) {
    pthread_mutex_unlock(&L_mutex);
    TAKYON_RECORD_ERROR(error_message, "Failed to find the function 'tknRecv()' in interconnect module '%s'.\nError: %s\n", interconnect_module, dlerror());
    return false;
  }
  */
  private_path->tknDestroy = dlsym(lib_handle, "tknDestroy");
  if (private_path->tknDestroy == NULL) {
    pthread_mutex_unlock(&L_mutex);
    TAKYON_RECORD_ERROR(error_message, "Failed to find the function 'tknDestroy()' in interconnect module '%s'.\nError: %s\n", interconnect_module, dlerror());
    return false;
  }
#endif

  // Release atomic lock
  pthread_mutex_unlock(&L_mutex);

  return true;
}
