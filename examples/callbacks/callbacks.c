// Copyright 2020 Abaco Systems
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "takyon.h"
#include "takyon_extensions.h"
#include <pthread.h>

static bool     L_is_endpointA       = false;
static char    *L_interconnect       = NULL;
static bool     L_is_multi_threaded  = false;
static bool     L_is_polling         = false;
static int      L_nbufs              = 2;
static uint64_t L_nbytes             = 1024;
static int      L_ncycles            = 1000;

typedef struct _MyPathStruct MyPath;
struct _MyPathStruct {
  TakyonPath *takyon_path;
  int buffer_index;
  uint64_t bytes_to_send;
  int cycle;
  void (*sendDoneCallback)(MyPath *path, int buffer_index);
  void (*recvDoneCallback)(MyPath *path, int buffer_index, uint64_t bytes);
  // Used to synchronize between threads
  pthread_mutex_t mutex;
  pthread_cond_t send_cond;
  pthread_cond_t recv_cond;
  pthread_cond_t main_cond;
  bool send_ready_to_start;
  bool recv_ready_to_start;
  bool time_to_quit;
};

static void getArgValues(int argc, char **argv) {
  L_interconnect = argv[1];

  int index = 2;
  while (index < argc) {
    if (strcmp(argv[index], "-mt") == 0) {
      L_is_multi_threaded = true;
    } else if (strcmp(argv[index], "-endpointA") == 0) {
      L_is_endpointA = true;
    } else if (strcmp(argv[index], "-poll") == 0) {
      L_is_polling = true;
    } else if (strcmp(argv[index], "-nbufs") == 0) {
      index++;
      L_nbufs = atoi(argv[index]);
    } else if (strcmp(argv[index], "-nbytes") == 0) {
      index++;
      L_nbytes = atol(argv[index]);
    } else if (strcmp(argv[index], "-ncycles") == 0) {
      index++;
      L_ncycles = atoi(argv[index]);
    }
    index++;
  }
}

static void fillInTestData(uint8_t *data, uint64_t bytes, int cycle) {
  for (uint64_t i=0; i<bytes; i++) {
    data[i] = (uint8_t)((cycle+i) % 256);
  }
}

static void verifyTestData(uint8_t *data, uint64_t bytes, int cycle) {
  for (uint64_t i=0; i<bytes; i++) {
    uint8_t got = data[i];
    uint8_t expected = (uint8_t)((cycle+i) % 256);
    if (got != expected) {
      printf("ERROR at cycle %d: data[%ju] wrong, got %u but expected %u\n", cycle, i, got, expected);
      exit(0);
    }
  }
}

static void *sendThread(void *user_arg) {
  MyPath *my_path = (MyPath *)user_arg;
  while (true) {
    void (*sendDoneCallback)(MyPath *path, int buffer_index);
    int buffer_index;
    uint64_t bytes_to_send;
    // Wait for a send command
    pthread_mutex_lock(&my_path->mutex);
    while (!my_path->time_to_quit && !my_path->send_ready_to_start) {
      pthread_cond_wait(&my_path->send_cond, &my_path->mutex);
    }
    if (my_path->time_to_quit) {
      pthread_mutex_unlock(&my_path->mutex);
      break;
    }
    // Got a request, so reset the request
    sendDoneCallback = my_path->sendDoneCallback;
    buffer_index = my_path->buffer_index;
    bytes_to_send = my_path->bytes_to_send;
    my_path->sendDoneCallback = NULL;
    my_path->buffer_index = -1;
    my_path->bytes_to_send = 0;
    my_path->send_ready_to_start = false;
    pthread_mutex_unlock(&my_path->mutex);
    // Do the send
    //printf("%s: Sending %ju bytes on buffer %d, cycle %d\n", my_path->takyon_path->attrs.is_endpointA ? "A" : "B", bytes_to_send, buffer_index, my_path->cycle);
    takyonSend(my_path->takyon_path, buffer_index, TAKYON_SEND_FLAGS_NONE, bytes_to_send, 0, 0, NULL);
    // Done sending, now invoke the callback
    sendDoneCallback(my_path, buffer_index);
  }
  return NULL;
}

static void *recvThread(void *user_arg) {
  MyPath *my_path = (MyPath *)user_arg;
  while (true) {
    void (*recvDoneCallback)(MyPath *my_path, int buffer_index, uint64_t bytes);
    int buffer_index;
    // Wait for a recv command
    pthread_mutex_lock(&my_path->mutex);
    while (!my_path->time_to_quit && !my_path->recv_ready_to_start) {
      pthread_cond_wait(&my_path->recv_cond, &my_path->mutex);
    }
    if (my_path->time_to_quit) {
      pthread_mutex_unlock(&my_path->mutex);
      break;
    }
    // Got a request, so reset the request
    recvDoneCallback = my_path->recvDoneCallback;
    buffer_index = my_path->buffer_index;
    my_path->recvDoneCallback = NULL;
    my_path->buffer_index = -1;
    my_path->recv_ready_to_start = false;
    pthread_mutex_unlock(&my_path->mutex);
    // Do the recv
    uint64_t bytes_received;
    //printf("%s: Recving on buffer %d, cycle %d\n", my_path->takyon_path->attrs.is_endpointA ? "A" : "B", buffer_index, my_path->cycle);
    takyonRecv(my_path->takyon_path, buffer_index, TAKYON_RECV_FLAGS_NONE, &bytes_received, NULL, NULL);
    //printf("%s: Recved %ju bytes on buffer %d, cycle %d\n", my_path->takyon_path->attrs.is_endpointA ? "A" : "B", bytes_received, buffer_index, my_path->cycle);
    // Done recving, now invoke the callback
    recvDoneCallback(my_path, buffer_index, bytes_received);
  }
  return NULL;
}

static void mySend(MyPath *my_path, int buffer_index, uint64_t bytes, void (*sendDoneCallback)(MyPath *path, int buffer_index)) {
  // Record the details of the send, so the thread will know what to do
  pthread_mutex_lock(&my_path->mutex);
  my_path->buffer_index = buffer_index;
  my_path->bytes_to_send = bytes;
  my_path->sendDoneCallback = sendDoneCallback;
  my_path->send_ready_to_start = true;
  pthread_mutex_unlock(&my_path->mutex);
  // Wake up th send thread
  pthread_cond_signal(&my_path->send_cond);
}

static void myRecv(MyPath *my_path, int buffer_index, void (*recvDoneCallback)(MyPath *my_path, int buffer_index, uint64_t bytes)) {
  // Record the details of the send, so the thread will know what to do
  pthread_mutex_lock(&my_path->mutex);
  my_path->buffer_index = buffer_index;
  my_path->recvDoneCallback = recvDoneCallback;
  my_path->recv_ready_to_start = true;
  pthread_mutex_unlock(&my_path->mutex);
  // Wake up th recv thread
  pthread_cond_signal(&my_path->recv_cond);
}

// Prototype needed here so sendDoneCallback() knows the format
static void recvDoneCallback(MyPath *my_path, int buffer_index, uint64_t bytes);

static void sendDoneCallback(MyPath *my_path, int buffer_index) {
  // IMPORTANT: this is called in a different thread from the thread that initiated the send
  if (!my_path->takyon_path->attrs.is_endpointA) {
    // This is endpoint B, so a round trip of the data has been completed. Time to update the buffer and cycles.
    buffer_index = (buffer_index + 1) % L_nbufs;
    my_path->cycle++;
    if (my_path->cycle == L_ncycles) {
      // Read to stop processing, signal main thread
      pthread_cond_signal(&my_path->main_cond);
      return;
    }
  }
  myRecv(my_path, buffer_index, recvDoneCallback);
}

static void recvDoneCallback(MyPath *my_path, int buffer_index, uint64_t bytes) {
  verifyTestData((uint8_t *)my_path->takyon_path->attrs.recver_addr_list[buffer_index], bytes, my_path->cycle);
  if (my_path->takyon_path->attrs.is_endpointA) {
    // This is endpoint A, so a round trip of the data has been completed. Time to update the buffer and cycles.
    buffer_index = (buffer_index + 1) % L_nbufs;
    my_path->cycle++;
    if (my_path->cycle == L_ncycles) {
      // Read to stop processing, signal main thread
      pthread_cond_signal(&my_path->main_cond);
      return;
    }
  }
  fillInTestData((uint8_t *)my_path->takyon_path->attrs.sender_addr_list[buffer_index], bytes, my_path->cycle);
  mySend(my_path, buffer_index, L_nbytes, sendDoneCallback);
}

static void endpointTask(bool is_endpointA) {
  TakyonPathAttributes attrs = takyonAllocAttributes(is_endpointA, L_is_polling, L_nbufs, L_nbufs, L_nbytes, TAKYON_WAIT_FOREVER, L_interconnect);
  TakyonPath *takyon_path = takyonCreate(&attrs);
  takyonFreeAttributes(attrs);

  // Attributes
  if (!L_is_multi_threaded || is_endpointA) {
    printf("Attributes:\n");
    if (!L_is_multi_threaded) printf("  endpoint:        \"%s\"\n", is_endpointA ? "A" : "B");
    printf("  interconnect:      \"%s\"\n", L_interconnect);
    printf("  locality:          %s\n", L_is_multi_threaded ? "inter-thread" : "inter-process");
    printf("  mode:              %s\n", L_is_polling ? "polling" : "event driven");
    printf("  nbufs:             %d\n", L_nbufs);
    printf("  nbytes:            %ju\n", L_nbytes);
    printf("  cycles:            %d\n", L_ncycles);
  }

  // Wrap the takyon path into the higher level API that allows callbacks
  // Each thread has its own MyPath structure
  MyPath my_path;
  my_path.takyon_path = takyon_path;
  my_path.buffer_index = -1;
  my_path.bytes_to_send = 0;
  my_path.send_ready_to_start = false;
  my_path.recv_ready_to_start = false;
  my_path.time_to_quit = false;
  my_path.cycle = 0;
  my_path.sendDoneCallback = NULL;
  my_path.recvDoneCallback = NULL;
  pthread_mutex_init(&my_path.mutex, NULL);
  pthread_cond_init(&my_path.send_cond, NULL);
  pthread_cond_init(&my_path.recv_cond, NULL);
  pthread_cond_init(&my_path.main_cond, NULL);

  // Create the threads to handle the send and recv transfers in the background
  pthread_t send_thread;
  pthread_t recv_thread;
  pthread_create(&send_thread, NULL, sendThread, &my_path);
  pthread_create(&recv_thread, NULL, recvThread, &my_path);

  // Do transfers in a callback way
  int buffer_index = 0;
  if (is_endpointA) {
    // Endpoint A kicks off the first send
    fillInTestData((uint8_t *)my_path.takyon_path->attrs.sender_addr_list[buffer_index], L_nbytes, my_path.cycle);
    // IMPORTANT: this call to mySend() does not actually do the sending, instead it pushes the send request to the send thread where the sending is actually done
    mySend(&my_path, buffer_index, L_nbytes, sendDoneCallback);
  } else {
    // Endpoint B kicks off the first recv
    // IMPORTANT: this call to myRecv() does not actually do the receive, instead it pushes the recv request to the recv thread where the recving is actually done
    myRecv(&my_path, buffer_index, recvDoneCallback);
  }

  // Wait for threads to complete
  pthread_mutex_lock(&my_path.mutex);
  while (my_path.cycle < L_ncycles) pthread_cond_wait(&my_path.main_cond, &my_path.mutex);
  my_path.time_to_quit = true;
  pthread_mutex_unlock(&my_path.mutex);

  // Signal threads to quit
  pthread_cond_signal(&my_path.send_cond);
  pthread_cond_signal(&my_path.recv_cond);

  // Clean up
  pthread_join(send_thread, NULL);
  pthread_join(recv_thread, NULL);
  pthread_mutex_destroy(&my_path.mutex);
  pthread_cond_destroy(&my_path.send_cond);
  pthread_cond_destroy(&my_path.recv_cond);
  pthread_cond_destroy(&my_path.main_cond);
  takyonDestroy(&takyon_path);
  printf("%s: Done running %d cycles!!!\n", is_endpointA ? "A" : "B", L_ncycles);
}

static void *endpointThread(void *user_data) {
  bool is_endpointA = (user_data != NULL);
  endpointTask(is_endpointA);
  return NULL;
}

#ifdef VXWORKS_7
static int callbacks_run(int argc, char **argv) {
  if (argc == 1) {
    printf("Usage: callbacks(\"<interconnect_spec>\",<number_of_parameters>,\"[options]\")\n");
    printf("  Options:\n");
    printf("    -mt                Enable inter-thread communication (default is inter-process)\n");
    printf("    -endpointA         If not multi threaded, then this process is marked as endpoint A (default is endpoint B)\n");
    printf("    -poll              Enable polling communication (default is event driven)\n");
    printf("    -nbufs <N>         Number of buffers. Default is %d\n", L_nbufs);
    printf("    -nbytes <N>         Message size in bytes. Default is %ju\n", L_nbytes);
    printf("    -ncycles <N>       Number of cycles at each byte size to time. Default is %d\n", L_ncycles);
    printf("  Example:\n");
    printf("    callbacks(\"InterThreadMemcpy -ID=1\",3,\"-endpointA\",\"-ncycles\",\"2000\")\n");
    return 1;
  }
#else
int main(int argc, char **argv) {
  if (argc == 1) {
    printf("Usage: callbacks <interconnect_spec> [options]\n");
    printf("  Options:\n");
    printf("    -mt                 Enable inter-thread communication (default is inter-process)\n");
    printf("    -endpointA          If not multi threaded, then this process is marked as endpoint A (default is endpoint B)\n");
    printf("    -poll               Enable polling communication (default is event driven)\n");
    printf("    -nbufs <N>          Number of buffers. Default is %d\n", L_nbufs);
    printf("    -nbytes <N>         Message size in bytes. Default is %ju\n", L_nbytes);
    printf("    -ncycles <N>        Number of cycles at each byte size to time. Default is %d\n", L_ncycles);
    return 1;
  }
#endif

  // Parse command line arguments
  getArgValues(argc, argv);

  if (L_is_multi_threaded) {
    pthread_t endpointA_thread_id;
    pthread_t endpointB_thread_id;
    pthread_create(&endpointA_thread_id, NULL, endpointThread, (void *)1LL);
    pthread_create(&endpointB_thread_id, NULL, endpointThread, NULL);
    pthread_join(endpointA_thread_id, NULL);
    pthread_join(endpointB_thread_id, NULL);
  } else {
    endpointTask(L_is_endpointA);
  }

  return 0;
}

#ifdef VXWORKS_7
#define ARGV_LIST_MAX 12
int callbacks(char *interconnect_spec_arg, int count, ...) {
  char *argv_list[ARGV_LIST_MAX];
  int arg_count = 0;
  argv_list[0] = "callbacks";
  arg_count++;
  if (NULL != interconnect_spec_arg) {
    argv_list[arg_count] = interconnect_spec_arg;
    arg_count++;
  }
  va_list valist;
  va_start(valist, count);
  
  if (count > (ARGV_LIST_MAX-arg_count)) {
    printf("ERROR: exceeded <number_of_parameters>\n");
    return (callbacks_run(1,NULL));
  }
  for (int i=0; i<count ; i++) {
    argv_list[arg_count] = va_arg(valist, char*);
    arg_count++;
  }
  va_end(valist);
  return (callbacks_run(arg_count,argv_list));
}
#endif

