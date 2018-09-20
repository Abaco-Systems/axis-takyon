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
//   Some helpful Windows based socket functionality to:
//     - make local socket connects
//     - make TCP socket connects
//     - set some socket parameters: no delay, non blocking
//     - send and recv
//     - check for disconnects
//     - setup pipes to help close down connections (wake up poll())
//     - socket based barrier
//     - verify remote arguments
// -----------------------------------------------------------------------------

#include <winsock2.h> // Needed for Socket
#include <ws2tcpip.h> // Need for inet_pton()
#include "takyon_private.h"

#define MAX_TEMP_FILENAME_CHARS (MAX_TAKYON_INTERCONNECT_CHARS+MAX_PATH+1)

static pthread_once_t L_once_control = PTHREAD_ONCE_INIT;

static void windows_socket_finalize(void) {
  if (WSACleanup() != 0) {
    fprintf(stderr, "Sockt finalize failed.\n");
  }
}

static void windows_socket_init_once(void) {
  // Initialize Winsock
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2,2), &wsaData) != NO_ERROR) {
    fprintf(stderr, "Could not initialize the Winsock socket interface.\n");
    abort();
  }
  // This will get called if the app calls exit() or if main does a normal return.
  if (atexit(windows_socket_finalize) == -1) {
    fprintf(stderr, "Failed to call atexit() in the mutex manager initializer\n");
    abort();
  }
}

static int windows_socket_manager_init(char *error_message) {
  // Call this to make sure Windows inits sockets: This can be called multiple times, but it's garanteed to atomically run the only the first time called.
  if (pthread_once(&L_once_control, windows_socket_init_once) != 0) {
    TAKYON_RECORD_ERROR(error_message, "Failed to start Windows sockets\n");
    return false;
  }
  return true;
}

static bool setSocketTimeout(TakyonSocket socket_fd, int timeout_name, int64_t timeout_ns, char *error_message) {
  DWORD timeout_ms;
  if (timeout_ns < 0) {
    timeout_ms = INT_MAX;
  } else if (timeout_ns == TAKYON_NO_WAIT) {
    timeout_ms = 0;
  } else {
    timeout_ms = (DWORD)(timeout_ns / 1000000);
  }
  if (setsockopt(socket_fd, SOL_SOCKET, timeout_name, (char *)&timeout_ms, sizeof(timeout_ms)) != 0) {
    TAKYON_RECORD_ERROR(error_message, "Failed to set the timeout = %lld. sock_error=%d\n", timeout_ns, WSAGetLastError());
    return false;
  }
  return true;
}

static bool socket_send_event_driven(TakyonSocket socket_fd, void *addr, size_t total_bytes_to_write, int64_t timeout_ns, bool *timed_out_ret, char *error_message) {
  if (timed_out_ret != NULL) *timed_out_ret = false;

  // Set the timeout on the socket
  if (!setSocketTimeout(socket_fd, SO_SNDTIMEO, timeout_ns, error_message)) return false;

  // Write the data
  size_t total_bytes_sent = 0;
  while (total_bytes_sent < total_bytes_to_write) {
    size_t bytes_to_write = total_bytes_to_write - total_bytes_sent;
    int bytes_to_write2 = (bytes_to_write > INT_MAX) ? INT_MAX : (int)bytes_to_write;
    int bytes_written = send(socket_fd, addr, bytes_to_write2, 0);
    if (bytes_written == bytes_to_write) {
      // Done
      break;
    }
    if (bytes_written == SOCKET_ERROR) {
      int sock_error = WSAGetLastError();
      if (sock_error == WSAEWOULDBLOCK) {
        // Timed out
        if (total_bytes_sent == 0) {
          // No problem, just return timed out
          if (timed_out_ret != NULL) {
            *timed_out_ret = true;
            return true;
          } else {
            // Can't report the timed
            TAKYON_RECORD_ERROR(error_message, "Timed out starting the transfer\n");
            return false;
          }
        } else {
          // This is bad... the connection might have gone down while in the middle of sending
          TAKYON_RECORD_ERROR(error_message, "Timed out in the middle of a send transfer: total_bytes_sent=%lld\n", (unsigned long long)total_bytes_sent);
          return false;
        }
      } else if (sock_error == WSAEINTR) {
        // Interrupted by external signal. Just try again
        bytes_written = 0;
      } else {
        TAKYON_RECORD_ERROR(error_message, "Failed to write to socket: sock_error=%d\n", sock_error);
        return false;
      }
    } else {
      // There was some progress, so keep trying
      total_bytes_sent += bytes_written;
      addr = (void *)((char *)addr + bytes_written);
    }
  }

  return true;
}

static bool socket_send_polling(TakyonSocket socket, void *addr, size_t total_bytes_to_write, int64_t timeout_ns, bool *timed_out_ret, char *error_message) {
  if (timed_out_ret != NULL) *timed_out_ret = false;
  int64_t start_time = clockTimeNanoseconds();

  size_t total_bytes_sent = 0;
  while (total_bytes_sent < total_bytes_to_write) {
    size_t bytes_to_write = total_bytes_to_write - total_bytes_sent;
    int bytes_to_write2 = (bytes_to_write > INT_MAX) ? INT_MAX : (int)bytes_to_write;
    int bytes_written = send(socket, addr, bytes_to_write2, 0);
    if (bytes_written == bytes_to_write) {
      // Done
      break;
    }
    if (bytes_written == SOCKET_ERROR) {
      int sock_error = WSAGetLastError();
      if (sock_error == WSAEWOULDBLOCK) {
        // Nothing written, but no errors
        if (timeout_ns >= 0) {
          int64_t ellapsed_time = clockTimeNanoseconds() - start_time;
          if (ellapsed_time >= timeout_ns) {
            // Timed out
            if (total_bytes_sent == 0) {
              if (timed_out_ret != NULL) {
                *timed_out_ret = true;
                return true;
              } else {
                // Can't report the timed
                TAKYON_RECORD_ERROR(error_message, "Timed out starting the transfer\n");
                return false;
              }
            } else {
              // This is bad... the connection might have gone down while in the middle of sending
              TAKYON_RECORD_ERROR(error_message, "Timed out in the middle of a send transfer: total_bytes_sent=%lld\n", (unsigned long long)total_bytes_sent);
              return false;
            }
          }
        }
        bytes_written = 0;
      } else if (sock_error == WSAEINTR) { 
        // Interrupted by external signal. Just try again
        bytes_written = 0;
      } else {
        TAKYON_RECORD_ERROR(error_message, "Failed to write to socket: sock_error=%d\n", sock_error);
        return false;
      }
    } else {
      // There was some progress, so keep trying
      total_bytes_sent += bytes_written;
      addr = (void *)((char *)addr + bytes_written);
    }
  }

  return true;
}

bool socketSend(TakyonSocket socket, void *addr, size_t bytes_to_write, bool is_polling, int64_t timeout_ns, bool *timed_out_ret, char *error_message) {
  if (is_polling) {
    return socket_send_polling(socket, addr, bytes_to_write, timeout_ns, timed_out_ret, error_message);
  } else {
    return socket_send_event_driven(socket, addr, bytes_to_write, timeout_ns, timed_out_ret, error_message);
  }
}

static bool socket_recv_event_driven(TakyonSocket socket_fd, void *data_ptr, size_t bytes_to_read, int64_t timeout_ns, bool *timed_out_ret, char *error_message) {
  if (timed_out_ret != NULL) *timed_out_ret = false;

  // Set the timeout on the socket
  if (!setSocketTimeout(socket_fd, SO_RCVTIMEO, timeout_ns, error_message)) return false;

  char *data = data_ptr;
  size_t total_bytes_read = 0;
  while (total_bytes_read < bytes_to_read) {
    size_t left_over = bytes_to_read - total_bytes_read;
    int bytes_to_read2 = left_over > INT_MAX ? INT_MAX : (int)left_over;
    int read_bytes = recv(socket_fd, data+total_bytes_read, bytes_to_read2, 0);
    if (read_bytes == 0) {
      // The socket was gracefully close
      TAKYON_RECORD_ERROR(error_message, "Read side of socket seems to be closed\n");
      return false;
    } else if (read_bytes == SOCKET_ERROR) {
      int sock_error = WSAGetLastError();
      if (sock_error == WSAEWOULDBLOCK) {
        // Timed out
        if (total_bytes_read == 0) {
          // No problem, just return timed out
          if (timed_out_ret != NULL) {
            *timed_out_ret = true;
            return true;
          } else {
            // Can't report the timed
            TAKYON_RECORD_ERROR(error_message, "Timed out starting the transfer\n");
            return false;
          }
        } else {
          // This is bad... the connection might have gone down while in the middle of receiving
          TAKYON_RECORD_ERROR(error_message, "Timed out in the middle of a recv transfer\n");
          return false;
        }
      } else if (sock_error == WSAEINTR) {
        // Interrupted by external signal. Just try again
        read_bytes = 0;
      } else {
        TAKYON_RECORD_ERROR(error_message, "Failed to read from socket: sock_error=%d\n", sock_error);
        return false;
      }
    }
    total_bytes_read += read_bytes;
  }

  return true;
}

static bool socket_recv_polling(TakyonSocket socket_fd, void *data_ptr, size_t bytes_to_read, int64_t timeout_ns, bool *timed_out_ret, char *error_message) {
  if (timed_out_ret != NULL) *timed_out_ret = false;
  int64_t start_time = clockTimeNanoseconds();

  char *data = data_ptr;
  size_t total_bytes_read = 0;
  while (total_bytes_read < bytes_to_read) {
    size_t left_over = bytes_to_read - total_bytes_read;
    int bytes_to_read2 = left_over > INT_MAX ? INT_MAX : (int)left_over;
    int read_bytes = recv(socket_fd, data+total_bytes_read, bytes_to_read2, 0);
    if (read_bytes == 0) {
      // The socket was gracefully close
      TAKYON_RECORD_ERROR(error_message, "Read side of socket seems to be closed\n");
      return false;
    } else if (read_bytes == SOCKET_ERROR) {
      int sock_error = WSAGetLastError();
      if (sock_error == WSAEWOULDBLOCK) {
        // Nothing read, but no errors
        if (timeout_ns >= 0) {
          int64_t ellapsed_time = clockTimeNanoseconds() - start_time;
          if (ellapsed_time >= timeout_ns) {
            // Timed out
            if (total_bytes_read == 0) {
              if (timed_out_ret != NULL) {
                *timed_out_ret = true;
                return true;
              } else {
                // Can't report the time out
                TAKYON_RECORD_ERROR(error_message, "Timed out starting the transfer\n");
                return false;
              }
            } else {
              // This is bad... the connection might have gone down while in the middle of receiving
              TAKYON_RECORD_ERROR(error_message, "Timed out in the middle of a recv transfer\n");
              return false;
            }
          }
        }
        read_bytes = 0;
      } else if (sock_error == WSAEINTR) {
        // Interrupted by external signal. Just try again
        read_bytes = 0;
      } else {
        TAKYON_RECORD_ERROR(error_message, "Failed to read from socket: sock_error=%d\n", sock_error);
        return false;
      }
    }
    total_bytes_read += read_bytes;
  }

  return true;
}

bool socketRecv(TakyonSocket socket_fd, void *data_ptr, size_t bytes_to_read, bool is_polling, int64_t timeout_ns, bool *timed_out_ret, char *error_message) {
  if (is_polling) {
    return socket_recv_polling(socket_fd, data_ptr, bytes_to_read, timeout_ns, timed_out_ret, error_message);
  } else {
    return socket_recv_event_driven(socket_fd, data_ptr, bytes_to_read, timeout_ns, timed_out_ret, error_message);
  }
}

static bool wait_for_socket_read_activity(TakyonSocket socket, int64_t timeout_ns, char *error_message) {
  int timeout_in_milliseconds;
  int num_fds_with_activity;
  struct pollfd poll_fd_list[1];
  unsigned long num_fds;

  // Will come back here if EINTR is detected while waiting
 restart_wait:

  if (timeout_ns < 0) {
    timeout_in_milliseconds = -1;
  } else {
    timeout_in_milliseconds = (int)(timeout_ns / 1000000);
  }

  poll_fd_list[0].fd = socket;
  poll_fd_list[0].events = POLLIN;
  poll_fd_list[0].revents = 0;
  num_fds = 1;

  // Wait for activity on the socket
  num_fds_with_activity = WSAPoll(poll_fd_list, num_fds, timeout_in_milliseconds);
  if (num_fds_with_activity == 0) {
    // Can't report the time out
    TAKYON_RECORD_ERROR(error_message, "Timed out waiting for socket read activity\n");
    return false;
  } else if (num_fds_with_activity != 1) {
    // Select has an error
    int sock_error = WSAGetLastError();
    if (sock_error == WSAEINTR) {
      // Interrupted by external signal. Just try again
      goto restart_wait;
    }
    TAKYON_RECORD_ERROR(error_message, "Error while waiting for socket read activity: sock_error=%d\n", sock_error);
    return false;
  } else {
    // Got activity.
    return true;
  }
}

static bool get_port_number_filename(const char *socket_name, char *port_number_filename, int max_chars, char *error_message) {
  char system_folder[MAX_PATH+1];
  snprintf(system_folder, MAX_PATH, "%s", ".\\"); // Use the current working folder
  unsigned int folder_bytes = (unsigned int)strlen(system_folder);
  if ((folder_bytes == 0) || (folder_bytes > MAX_PATH)) {
    TAKYON_RECORD_ERROR(error_message, "System folder name is bigger than %d chars\n", MAX_PATH);
    return false;
  }
  if (system_folder[folder_bytes-1] == '\\') {
    snprintf(port_number_filename, max_chars, "%stemp_%s.txt", system_folder, socket_name);
  } else {
    snprintf(port_number_filename, max_chars, "%s\\temp_%s.txt", system_folder, socket_name);
  }
  return true;
}

static bool socketSetNoDelay(TakyonSocket socket_fd, bool use_it, char *error_message) {
  // NOTE: Makes no difference on OSX. Maybe it will on Linux.
  // This will allow small messages to be set right away instead of letting the socket wait to buffer up 8k of data.
  BOOL use_no_delay = use_it ? 1 : 0;
  if (setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, (char *)&use_no_delay, sizeof(use_no_delay)) != 0) {
    TAKYON_RECORD_ERROR(error_message, "Failed to set socket to TCP_NODELAY. sock_error=%d\n", WSAGetLastError());
    return false;
  }
  return true;
}

bool socketCreateLocalClient(const char *socket_name, TakyonSocket *socket_fd_ret, int64_t timeout_ns, char *error_message) {
  if (!windows_socket_manager_init(error_message)) {
    return false;
  }

  TakyonSocket socket_fd = 0;

  // Keep trying until a connection is made or timeout
  int64_t start_time = clockTimeNanoseconds();
  char *ip_addr = "127.0.0.1"; // Local socket
  while (1) {
    unsigned short port_number = 0;
    socket_fd = 0;

    // Get the published port number
    {
      char port_number_filename[MAX_TEMP_FILENAME_CHARS];
      if (!get_port_number_filename(socket_name, port_number_filename, MAX_TEMP_FILENAME_CHARS, error_message)) {
        TAKYON_RECORD_ERROR(error_message, "Could not get temp filename\n");
        return false;
      }
      FILE *fp = fopen(port_number_filename, "r");
      if (fp == NULL) {
        if (errno != ENOENT) {
          TAKYON_RECORD_ERROR(error_message, "Failed to open file '%s' to get published port number. errno=%d\n", port_number_filename, errno);
          return false;
        }
        // Remote side not ready yet, Port number is still 0, so a retry will occur
      } else {
        // Read the port number
        int tokens = fscanf(fp, "%hd", &port_number);
        if ((tokens != 1) || (port_number <= 0)) {
          // The file might be in the act of being created or destroyed.
          port_number = 0;
        }
        fclose(fp);
      }
    }

    if (port_number > 0) {
      socket_fd = socket(AF_INET, SOCK_STREAM, 0);
      if (socket_fd == INVALID_SOCKET) {
        TAKYON_RECORD_ERROR(error_message, "Could not create local TCP socket. sock_error=%d\n", WSAGetLastError());
        return false;
      }

      // Set up server connection info
      struct sockaddr_in server_addr;
      memset(&server_addr, 0, sizeof(server_addr));
      server_addr.sin_family = AF_INET;
      server_addr.sin_port = htons(port_number);
      int status = inet_pton(AF_INET, ip_addr, &server_addr.sin_addr);
      if (status == 0) {
        TAKYON_RECORD_ERROR(error_message, "Could not get the IP address from the hostname '%s'. Invalid name.\n", ip_addr);
        closesocket(socket_fd);
        return false;
      } else if (status == -1) {
        TAKYON_RECORD_ERROR(error_message, "Could not get the IP address from the hostname '%s'. sock_error=%d\n", ip_addr, WSAGetLastError());
        closesocket(socket_fd);
        return false;
      }

      if (connect(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) != SOCKET_ERROR) {
        // Connection was made
        break;
      }
      int sock_error = WSAGetLastError();
      /*+ check for EBADF? Test with reduce example: graph_mp.txt */
      if (sock_error != WSAECONNREFUSED) {
        TAKYON_RECORD_ERROR(error_message, "Could not connect unix socket. sock_error=%d\n", sock_error);
        closesocket(socket_fd);
        return false;
      }
      closesocket(socket_fd);
    }

    // Check if time to timeout
    int64_t ellapsed_time_ns = clockTimeNanoseconds() - start_time;
    if (timeout_ns >= 0) {
      if (ellapsed_time_ns >= timeout_ns) {
        // Timed out
        TAKYON_RECORD_ERROR(error_message, "Timed out waiting for connection on local client socket\n");
        return false;
      }
    }
    // Context switch out for a little time to allow remote side to get going.
    int microseconds = 10000;
    clockSleepYield(microseconds);
  }

  // TCP sockets on Linux use the Nagle algorithm, so need to turn it off to get good performance.
  if (!socketSetNoDelay(socket_fd, 1, error_message)) {
    TAKYON_RECORD_ERROR(error_message, "Failed to set socket attribute\n");
    return false;
  }

  *socket_fd_ret = socket_fd;

  return true;
}

bool socketCreateTcpClient(const char *ip_addr, uint16_t port_number, TakyonSocket *socket_fd_ret, int64_t timeout_ns, char *error_message) {
  if (!windows_socket_manager_init(error_message)) {
    return false;
  }

  TakyonSocket socket_fd = 0;

  // Keep trying until a connection is made or timeout
  int64_t start_time = clockTimeNanoseconds();
  while (1) {
    // Create a socket file descriptor
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == INVALID_SOCKET) {
      TAKYON_RECORD_ERROR(error_message, "Could not create TCP socket. sock_error=%d\n", WSAGetLastError());
      return false;
    }

    // Set up server connection info
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    int status = inet_pton(AF_INET, ip_addr, &server_addr.sin_addr);
    if (status == 0) {
      TAKYON_RECORD_ERROR(error_message, "Could not get the IP address from the hostname '%s'. Invalid name.\n", ip_addr);
      closesocket(socket_fd);
      return false;
    } else if (status == -1) {
      TAKYON_RECORD_ERROR(error_message, "Could not get the IP address from the hostname '%s'. sock_error=%d\n", ip_addr, WSAGetLastError());
      closesocket(socket_fd);
      return false;
    }

    // Wait for the connection
    if (connect(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
      int sock_error = WSAGetLastError();
      closesocket(socket_fd);
      /*+ check for EBADF? Test with reduce example: graph_mp.txt */
      if (sock_error == WSAECONNREFUSED) {
        // Server side is not ready yet
        int64_t ellapsed_time_ns = clockTimeNanoseconds() - start_time;
        if (timeout_ns >= 0) {
          if (ellapsed_time_ns >= timeout_ns) {
            // Timed out
            TAKYON_RECORD_ERROR(error_message, "Timed out waiting for connection\n");
            return false;
          }
        }
        // Context switch out for a little time to allow remote side to get going.
        int microseconds = 10000;
        clockSleepYield(microseconds);
        // Try connecting again now
      } else {
        TAKYON_RECORD_ERROR(error_message, "Could not connect unix socket. sock_error=%d\n", sock_error);
        return false;
      }
    } else {
      break;
    }
  }

  // TCP sockets on Linux use the Nagle algorithm, so need to turn it off to get good performance.
  if (!socketSetNoDelay(socket_fd, 1, error_message)) {
    TAKYON_RECORD_ERROR(error_message, "Failed to set socket attribute\n");
    return false;
  }

  *socket_fd_ret = socket_fd;

  return true;
}

bool socketCreateLocalServer(const char *socket_name, bool allow_reuse, TakyonSocket *socket_fd_ret, int64_t timeout_ns, char *error_message) {
  if (!windows_socket_manager_init(error_message)) {
    return false;
  }

  TakyonSocket listening_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listening_fd == INVALID_SOCKET) {
    TAKYON_RECORD_ERROR(error_message, "Could not create TCP listening socket. sock_error=%d\n", WSAGetLastError());
    return false;
  }

  // Create server connection info
  unsigned short port_number = 0; // This will tell the OS to find an available port number
  const char *ip_addr = "127.0.0.1"; // This will force interprocess communication
  struct sockaddr_in server_addr;
  memset((char*)&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port_number); // NOTE: Use 0 to have the system find an unused port number
  // NOTE: htonl(INADDR_ANY) would allow any interface to listen
  int status = inet_pton(AF_INET, ip_addr, &server_addr.sin_addr);
  if (status == 0) {
    TAKYON_RECORD_ERROR(error_message, "Could not get the IP address from the hostname '%s'. Invalid name.\n", ip_addr);
    closesocket(listening_fd);
    return false;
  } else if (status == -1) {
    TAKYON_RECORD_ERROR(error_message, "Could not get the IP address from the hostname '%s'. sock_error=%d\n", ip_addr, WSAGetLastError());
    closesocket(listening_fd);
    return false;
  }

  if (allow_reuse) {
    // This will allow a previously closed socket, that is still in the TIME_WAIT stage, to be used.
    // This may accur if the application did not exit gracefully on a previous run.
    BOOL allow = 1;
    if (setsockopt(listening_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&allow, sizeof(allow)) != 0) {
      TAKYON_RECORD_ERROR(error_message, "Failed to set socket to SO_REUSEADDR. sock_error=%d\n", WSAGetLastError());
      closesocket(listening_fd);
      return false;
    }
  }

  // This is when the port number will be determined
  if (bind(listening_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
    int sock_error = WSAGetLastError();
    if (sock_error == WSAEADDRINUSE) {
      TAKYON_RECORD_ERROR(error_message, "Could not bind TCP socket. The address is already in use or in a time wait state. May need to use the option '-reuse'\n");
    } else {
      TAKYON_RECORD_ERROR(error_message, "Could not bind TCP socket. sock_error=%d\n", WSAGetLastError());
    }
    closesocket(listening_fd);
    return false;
  }

  // If using an ephemeral port number, then use this to know the actual port number and pass it to the rest of the system
  {
    SOCKADDR_IN socket_info;
    int len = sizeof(socket_info);
    memset(&socket_info, 0, len);
    if (getsockname(listening_fd, (struct sockaddr *)&socket_info, &len) == SOCKET_ERROR) {
      TAKYON_RECORD_ERROR(error_message, "Could not get the socket name info. sock_error=%d\n", WSAGetLastError());
      closesocket(listening_fd);
      return false;
    }
    port_number = htons(socket_info.sin_port);
  }

  // Write the port number to a temporary file
  char port_number_filename[MAX_TEMP_FILENAME_CHARS];
  {
    if (!get_port_number_filename(socket_name, port_number_filename, MAX_TEMP_FILENAME_CHARS, error_message)) {
      TAKYON_RECORD_ERROR(error_message, "Could not get temp filename\n");
      closesocket(listening_fd);
      return false;
    }
    FILE *fp = fopen(port_number_filename, "w");
    if (fp == NULL) {
      TAKYON_RECORD_ERROR(error_message, "Failed to open file '%s' to temporarily publish port number. errno=%d\n", port_number_filename, errno);
      closesocket(listening_fd);
      return false;
    }
    fprintf(fp, "%d\n", port_number);
    fclose(fp);
  }
  
  // Set the number of simultaneous connections that can be made
  if (listen(listening_fd, 1) == SOCKET_ERROR) {
    TAKYON_RECORD_ERROR(error_message, "Could not listen on TCP socket. sock_error=%d\n", WSAGetLastError());
    remove(port_number_filename);
    closesocket(listening_fd);
    return false;
  }

  // Wait for a client to ask for a connection
  if (timeout_ns >= 0) {
    if (!wait_for_socket_read_activity(listening_fd, timeout_ns, error_message)) {
      TAKYON_RECORD_ERROR(error_message, "Fail to listen for connection\n");
      remove(port_number_filename);
      closesocket(listening_fd);
      return false;
    }
  }

  // This blocks until a connection is acctual made
  TakyonSocket socket_fd = accept(listening_fd, NULL, NULL);
  if (socket_fd == INVALID_SOCKET) {
    TAKYON_RECORD_ERROR(error_message, "Could not accept TCP socket. sock_error=%d\n", WSAGetLastError());
    remove(port_number_filename);
    closesocket(listening_fd);
    return false;
  }

  // TCP sockets on Linux use the Nagle algorithm, so need to turn it off to get good performance.
  if (!socketSetNoDelay(socket_fd, 1, error_message)) {
    TAKYON_RECORD_ERROR(error_message, "Failed to set socket attribute\n");
    return false;
  }

  closesocket(listening_fd);
  remove(port_number_filename);
  *socket_fd_ret = socket_fd;

  return true;
}

bool socketCreateTcpServer(const char *ip_addr, uint16_t port_number, bool allow_reuse, TakyonSocket *socket_fd_ret, int64_t timeout_ns, char *error_message) {
  if (!windows_socket_manager_init(error_message)) {
    return false;
  }

  TakyonSocket listening_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listening_fd == INVALID_SOCKET) {
    TAKYON_RECORD_ERROR(error_message, "Could not create TCP listening socket. sock_error=%d\n", WSAGetLastError());
    return false;
  }

  // Create server connection info
  struct sockaddr_in server_addr;
  memset((char*)&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port_number); // NOTE: Use 0 to have the system find an unused port number
  if (strcmp(ip_addr, "Any") == 0) {
    // NOTE: htonl(INADDR_ANY) will allow any IP interface to listen
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  } else {
    // Listen on a specific IP interface
    int status = inet_pton(AF_INET, ip_addr, &server_addr.sin_addr);
    if (status == 0) {
      TAKYON_RECORD_ERROR(error_message, "Could not get the IP address from the hostname '%s'. Invalid name.\n", ip_addr);
      closesocket(listening_fd);
      return false;
    } else if (status == -1) {
      TAKYON_RECORD_ERROR(error_message, "Could not get the IP address from the hostname '%s'. sock_error=%d\n", ip_addr, WSAGetLastError());
      closesocket(listening_fd);
      return false;
    }
  }

  if (allow_reuse) {
    // This will allow a previously closed socket, that is still in the TIME_WAIT stage, to be used.
    // This may accur if the application did not exit gracefully on a previous run.
    BOOL allow = 1;
    if (setsockopt(listening_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&allow, sizeof(allow)) != 0) {
      TAKYON_RECORD_ERROR(error_message, "Failed to set socket to SO_REUSEADDR. sock_error=%d\n", WSAGetLastError());
      closesocket(listening_fd);
      return false;
    }
  }

  if (bind(listening_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
    int sock_error = WSAGetLastError();
    if (sock_error == WSAEADDRINUSE) {
      TAKYON_RECORD_ERROR(error_message, "Could not bind TCP socket. The address is already in use or in a time wait state. May need to use the option '-reuse'\n");
    } else {
      TAKYON_RECORD_ERROR(error_message, "Could not bind TCP socket. sock_error=%d\n", WSAGetLastError());
    }
    closesocket(listening_fd);
    return false;
  }

  // If using an ephemeral port number, then use this to know the actual port number and pass it to the rest of the system
  /*
  socklen_t len = sizeof(sin);
  if (getsockname(listen_sock, (struct sockaddr *)&sin, &len) < 0) {
  // Handle error here
  }
  // You can now get the port number with ntohs(sin.sin_port).
  */

  // Set the number of simultaneous connection that can be made
  if (listen(listening_fd, 1) == SOCKET_ERROR) {
    TAKYON_RECORD_ERROR(error_message, "Could not listen on TCP socket. sock_error=%d\n", WSAGetLastError());
    closesocket(listening_fd);
    return false;
  }

  // Wait for a client to ask for a connection
  if (timeout_ns >= 0) {
    if (!wait_for_socket_read_activity(listening_fd, timeout_ns, error_message)) {
      TAKYON_RECORD_ERROR(error_message, "Fail to listen for connection\n");
      closesocket(listening_fd);
      return false;
    }
  }

  // This blocks until a connection is acctual made
  TakyonSocket socket_fd = accept(listening_fd, NULL, NULL);
  if (socket_fd == INVALID_SOCKET) {
    TAKYON_RECORD_ERROR(error_message, "Could not accept TCP socket. sock_error=%d\n", WSAGetLastError());
    closesocket(listening_fd);
    return false;
  }

  // TCP sockets on Linux use the Nagle algorithm, so need to turn it off to get good performance.
  if (!socketSetNoDelay(socket_fd, 1, error_message)) {
    TAKYON_RECORD_ERROR(error_message, "Failed to set socket attribute\n");
    return false;
  }

  closesocket(listening_fd);
  *socket_fd_ret = socket_fd;

  return true;
}

bool pipeCreate(const char *pipe_name, int *read_pipe_fd_ret, int *write_pipe_fd_ret, char *error_message) {
  // IMPORTANT: Windows does not have pipes, so may need to poll on connection to see if it's disconnected
  *read_pipe_fd_ret = -1;
  *write_pipe_fd_ret = -1;
  return true;
}

bool socketWaitForDisconnectActivity(TakyonSocket socket_fd, int read_pipe_fd, bool *got_socket_activity_ret, char *error_message) {
  // NOTE: This function is used to detect when a socket close gracefull or not.
  //       A pipe is not use here since on Window, the thread listening for disconnect will
  //       wake up after the socket if gracefully closed, and then the thread can quit.

  int timeout_in_milliseconds = -1; // Wait forever
  int num_fds_with_activity;
  struct pollfd poll_fd_list[1];
  unsigned long num_fds = 1;

  // Will come back here if EINTR is detected while waiting
 restart_wait:

  poll_fd_list[0].fd = socket_fd;
  poll_fd_list[0].events = POLLIN;
  poll_fd_list[0].revents = 0;

  // Wait for activity on the socket
  num_fds_with_activity = WSAPoll(poll_fd_list, num_fds, timeout_in_milliseconds);
  if (num_fds_with_activity == 0) {
    // Can't report the time out
    TAKYON_RECORD_ERROR(error_message, "Timed out waiting for socket activity\n");
    return false;
  } else if (num_fds_with_activity < 0) {
    // Select has an error */
    int sock_error = WSAGetLastError();
    if (sock_error == WSAEINTR) {
      // Interrupted by external signal. Just try again
      goto restart_wait;
    }
    TAKYON_RECORD_ERROR(error_message, "Error while waiting for socket activity: sock_error=%d\n", sock_error);
    return false;
  } else {
    // Got activity.
    *got_socket_activity_ret = 1;
    return true;
  }
}

bool pipeWakeUpSelect(int read_pipe_fd, int write_pipe_fd, char *error_message) {
  // IMPORTANT: Nothing to do since WSAPoll will be set to wake up periodically to check if the thread should shut down */
  return true;
}

void pipeDestroy(const char *pipe_name, int read_pipe_fd, int write_pipe_fd) {
  // Nothing to do
}

bool socketSetBlocking(TakyonSocket socket_fd, bool is_blocking, char *error_message) {
  unsigned long blocking_mode = is_blocking ? 0 : 1;
  if (ioctlsocket(socket_fd, FIONBIO, &blocking_mode) == SOCKET_ERROR) {
    TAKYON_RECORD_ERROR(error_message, "Failed to set socket to %s. sock_error=%d\n", is_blocking ? "blocking" : "non-blocking", WSAGetLastError());
    return false;
  }
  return true;
}

void socketClose(TakyonSocket socket_fd) {
  closesocket(socket_fd);
}

bool socketBarrier(bool is_client, TakyonSocket socket_fd, int barrier_id, int64_t timeout_ns, char *error_message) {
  bool is_polling = false;
  bool timed_out;
  int recved_barrier_id;

  if (is_client) {
    // Wait for client to complete
    if (!socketSend(socket_fd, &barrier_id, sizeof(barrier_id), is_polling, timeout_ns, &timed_out, error_message)) {
      TAKYON_RECORD_ERROR(error_message, "Client failed to send barrier value\n");
      return false;
    }
    if ((timeout_ns >= 0) && (timed_out)) {
      TAKYON_RECORD_ERROR(error_message, "Client timed out sending barrier value\n");
      return false;
    }
    recved_barrier_id = 0;
    if (!socketRecv(socket_fd, &recved_barrier_id, sizeof(recved_barrier_id), is_polling, timeout_ns, &timed_out, error_message)) {
      TAKYON_RECORD_ERROR(error_message, "Client failed to get barrier value\n");
      return false;
    } else if ((timeout_ns >= 0) && (timed_out)) {
      TAKYON_RECORD_ERROR(error_message, "Client timed out waiting for barrier value\n");
      return false;
    } else if (recved_barrier_id != barrier_id) {
      TAKYON_RECORD_ERROR(error_message, "Client failed to get proper barrier value. Got %d but expected %d\n", recved_barrier_id, barrier_id);
      return false;
    }

  } else {
    // Wait for server to complete
    recved_barrier_id = 0;
    if (!socketRecv(socket_fd, &recved_barrier_id, sizeof(recved_barrier_id), is_polling, timeout_ns, &timed_out, error_message)) {
      TAKYON_RECORD_ERROR(error_message, "Server failed to get barrier value\n");
      return false;
    } else if ((timeout_ns >= 0) && (timed_out)) {
      TAKYON_RECORD_ERROR(error_message, "Server timed out waiting for barrier value\n");
      return false;
    } else if (recved_barrier_id != barrier_id) {
      TAKYON_RECORD_ERROR(error_message, "Server failed to get proper barrier value. Got %d but expected %d\n", recved_barrier_id, barrier_id);
      return false;
    }
    if (!socketSend(socket_fd, &barrier_id, sizeof(barrier_id), is_polling, timeout_ns, &timed_out, error_message)) {
      TAKYON_RECORD_ERROR(error_message, "Server failed to send barrier value\n");
      return false;
    }
    if ((timeout_ns >= 0) && (timed_out)) {
      TAKYON_RECORD_ERROR(error_message, "Server timed out sending barrier value\n");
      return false;
    }
  }

  return true;
}

bool socketSwapAndVerifyInt(TakyonSocket socket_fd, int value, int64_t timeout_ns, char *error_message) {
  bool is_polling = false;
  bool timed_out;

  // Send value to remote side
  if (!socketSend(socket_fd, &value, sizeof(value), is_polling, timeout_ns, &timed_out, error_message)) {
    TAKYON_RECORD_ERROR(error_message, "Client failed to send comparison value\n");
    return false;
  }
  if ((timeout_ns >= 0) && (timed_out)) {
    TAKYON_RECORD_ERROR(error_message, "Client timed out sending comparison value\n");
    return false;
  }

  // Get remote value
  int recved_value = 0;
  if (!socketRecv(socket_fd, &recved_value, sizeof(recved_value), is_polling, timeout_ns, &timed_out, error_message)) {
    TAKYON_RECORD_ERROR(error_message, "Client failed to get comparison value\n");
    return false;
  } else if ((timeout_ns >= 0) && (timed_out)) {
    TAKYON_RECORD_ERROR(error_message, "Client timed out waiting for comparison value\n");
    return false;
  } else if (recved_value != value) {
    TAKYON_RECORD_ERROR(error_message, "Client failed to get proper comparison value. Got %d but expected %d\n", recved_value, value);
    return false;
  }

  return true;
}

bool socketSwapAndVerifyUInt64(TakyonSocket socket_fd, uint64_t value, int64_t timeout_ns, char *error_message) {
  bool is_polling = false;
  bool timed_out;

  // Send value to remote side
  if (!socketSend(socket_fd, &value, sizeof(value), is_polling, timeout_ns, &timed_out, error_message)) {
    TAKYON_RECORD_ERROR(error_message, "Client failed to send comparison value\n");
    return false;
  }
  if ((timeout_ns >= 0) && (timed_out)) {
    TAKYON_RECORD_ERROR(error_message, "Client timed out sending comparison value\n");
    return false;
  }

  // Get remote value
  uint64_t recved_value = 0;
  if (!socketRecv(socket_fd, &recved_value, sizeof(recved_value), is_polling, timeout_ns, &timed_out, error_message)) {
    TAKYON_RECORD_ERROR(error_message, "Client failed to get comparison value\n");
    return false;
  } else if ((timeout_ns >= 0) && (timed_out)) {
    TAKYON_RECORD_ERROR(error_message, "Client timed out waiting for comparison value\n");
    return false;
  } else if (recved_value != value) {
    TAKYON_RECORD_ERROR(error_message, "Client failed to get proper comparison value. Got %d but expected %d\n", recved_value, value);
    return false;
  }

  return true;
}

bool socketSendUInt64(TakyonSocket socket_fd, uint64_t value, int64_t timeout_ns, char *error_message) {
  bool is_polling = false;
  bool timed_out;

  // Send value to remote side
  if (!socketSend(socket_fd, &value, sizeof(value), is_polling, timeout_ns, &timed_out, error_message)) {
    TAKYON_RECORD_ERROR(error_message, "Client failed to send comparison value\n");
    return false;
  }
  if ((timeout_ns >= 0) && (timed_out)) {
    TAKYON_RECORD_ERROR(error_message, "Client timed out sending comparison value\n");
    return false;
  }
  return true;
}

bool socketRecvUInt64(TakyonSocket socket_fd, uint64_t *value_ret, int64_t timeout_ns, char *error_message) {
  bool is_polling = false;
  bool timed_out;

  // Get remote value
  if (!socketRecv(socket_fd, value_ret, sizeof(uint64_t), is_polling, timeout_ns, &timed_out, error_message)) {
    TAKYON_RECORD_ERROR(error_message, "Client failed to get comparison value\n");
    return false;
  } else if ((timeout_ns >= 0) && (timed_out)) {
    TAKYON_RECORD_ERROR(error_message, "Client timed out waiting for comparison value\n");
    return false;
  }

  return true;
}

static bool datagram_send_polling(TakyonSocket socket_fd, void *sock_in_addr, void *addr, size_t bytes_to_write, int64_t timeout_ns, bool *timed_out_ret, char *error_message) {
  if (timed_out_ret != NULL) *timed_out_ret = false;
  int64_t start_time = clockTimeNanoseconds();

  if (bytes_to_write > INT_MAX) {
    TAKYON_RECORD_ERROR(error_message, "Data size is greater than what an 'int' can hold.\n");
    return false;
  }

  while (1) {
    int flags = 0;
    int bytes_written = sendto(socket_fd, addr, (int)bytes_to_write, flags, (struct sockaddr *)sock_in_addr, sizeof(struct sockaddr_in));
    if (bytes_written == bytes_to_write) {
      // Transfer completed
      break;
    }
    if (bytes_written == SOCKET_ERROR) {
      int sock_error = WSAGetLastError();
      if (sock_error == WSAEWOULDBLOCK) {
        // Nothing written, but no errors
        if (timeout_ns >= 0) {
          int64_t ellapsed_time = clockTimeNanoseconds() - start_time;
          if (ellapsed_time >= timeout_ns) {
            // Timed out
            if (timed_out_ret != NULL) {
              *timed_out_ret = true;
              return true;
            } else {
              // Can't report the time out
              TAKYON_RECORD_ERROR(error_message, "Timed out starting the transfer\n");
              return false;
            }
          }
        }
        bytes_written = 0;
      } else if (sock_error == WSAEMSGSIZE) {
        TAKYON_RECORD_ERROR(error_message, "Failed to send datagram message. %lld bytes exceeds the datagram size\n", (long long)bytes_to_write);
        return false;
      } else if (sock_error == WSAEINTR) {
        // Interrupted by external signal. Just try again
        bytes_written = 0;
      } else {
        TAKYON_RECORD_ERROR(error_message, "Failed to write to socket: error=%d\n", sock_error);
        return false;
      }
    } else if (bytes_written == 0) {
      // Keep trying
    } else {
      TAKYON_RECORD_ERROR(error_message, "Wrote a partial datagram: bytes_sent=%lld, bytes_to_send=%lld\n", bytes_written, bytes_to_write);
      return false;
    }
  }

  return true;
}

static bool datagram_send_event_driven(TakyonSocket socket_fd, void *sock_in_addr, void *addr, size_t bytes_to_write, int64_t timeout_ns, bool *timed_out_ret, char *error_message) {
  if (timed_out_ret != NULL) *timed_out_ret = false;

  if (bytes_to_write > INT_MAX) {
    TAKYON_RECORD_ERROR(error_message, "Data size is greater than what an 'int' can hold.\n");
    return false;
  }

  // Set the timeout on the socket
  if (!setSocketTimeout(socket_fd, SO_SNDTIMEO, timeout_ns, error_message)) return false;

  while (1) {
    // Write the data
    int flags = 0;
    int bytes_written = sendto(socket_fd, addr, (int)bytes_to_write, flags, (struct sockaddr *)sock_in_addr, sizeof(struct sockaddr_in));
    if (bytes_written == bytes_to_write) {
      // Transfer completed
      return true;
    }

    // Something went wrong
    if (bytes_written == SOCKET_ERROR) {
      int sock_error = WSAGetLastError();
      if (sock_error == WSAEMSGSIZE) {
        TAKYON_RECORD_ERROR(error_message, "Failed to send datagram message. %lld bytes exceeds the allowable datagram size\n", (long long)bytes_to_write);
        return false;
      } else if (sock_error == WSAEWOULDBLOCK) {
        // Timed out
        if (timed_out_ret != NULL) {
          *timed_out_ret = true;
          return true;
        } else {
          // Can't report the timed
          TAKYON_RECORD_ERROR(error_message, "Timed out starting the transfer\n");
          return false;
        }
      } else if (sock_error == WSAEINTR) {
        // Interrupted by external signal. Just try again
      } else {
        TAKYON_RECORD_ERROR(error_message, "Failed to write to socket: error=%d\n", sock_error);
        return false;
      }
    } else if (bytes_written == 0) {
      // Keep trying
    } else {
      TAKYON_RECORD_ERROR(error_message, "Wrote a partial datagram: bytes_sent=%lld, bytes_to_send=%lld\n", bytes_written, bytes_to_write);
      return false;
    }
  }

  return true;
}

static bool datagram_recv_event_driven(TakyonSocket socket_fd, void *data_ptr, size_t buffer_bytes, uint64_t *bytes_read_ret, int64_t timeout_ns, bool *timed_out_ret, char *error_message) {
  if (timed_out_ret != NULL) *timed_out_ret = false;

  if (buffer_bytes > INT_MAX) {
    TAKYON_RECORD_ERROR(error_message, "Buffer bytes is greater than what an 'int' can hold.\n");
    return false;
  }

  // Set the timeout on the socket
  if (!setSocketTimeout(socket_fd, SO_RCVTIMEO, timeout_ns, error_message)) return false;

  while (1) {
    int flags = 0;
    int bytes_received = recvfrom(socket_fd, data_ptr, (int)buffer_bytes, flags, NULL, NULL);
    if (bytes_received > 0) {
      // Got a datagram
      *bytes_read_ret = (uint64_t)bytes_received;
      return true;
    }
    if (bytes_received == 0) {
      // Socket was probably closed by the remote side
      TAKYON_RECORD_ERROR(error_message, "Read side of socket seems to be closed\n");
      return false;
    }
    if (bytes_received == SOCKET_ERROR) {
      int sock_error = WSAGetLastError();
      if (sock_error == WSAEWOULDBLOCK) {
        // Timed out
        if (timed_out_ret != NULL) {
          *timed_out_ret = true;
          return true;
        } else {
          // Can't report the timed
          TAKYON_RECORD_ERROR(error_message, "Timed out starting the transfer\n");
          return false;
        }
      } else if (sock_error == WSAEINTR) {
        // Interrupted by external signal. Just try again
      } else {
        TAKYON_RECORD_ERROR(error_message, "Failed to read from socket: error=%d\n", sock_error);
        return false;
      }
    }
  }
}

static bool datagram_recv_polling(TakyonSocket socket_fd, void *data_ptr, size_t buffer_bytes, uint64_t *bytes_read_ret, int64_t timeout_ns, bool *timed_out_ret, char *error_message) {
  if (timed_out_ret != NULL) *timed_out_ret = false;
  int64_t start_time = clockTimeNanoseconds();

  if (buffer_bytes > INT_MAX) {
    TAKYON_RECORD_ERROR(error_message, "Buffer bytes is greater than what an 'int' can hold.\n");
    return false;
  }

  while (1) {
    int flags = 0;
    int bytes_received = recvfrom(socket_fd, data_ptr, (int)buffer_bytes, flags, NULL, NULL);
    if (bytes_received > 0) {
      // Got a datagram
      *bytes_read_ret = (uint64_t)bytes_received;
      return true;
    }
    if (bytes_received == 0) {
      // Socket was probably closed by the remote side
      TAKYON_RECORD_ERROR(error_message, "Read side of socket seems to be closed\n");
      return false;
    }
    if (bytes_received == SOCKET_ERROR) {
      int sock_error = WSAGetLastError();
      if (sock_error == WSAEWOULDBLOCK) {
        // Nothing read, but no errors
        if (timeout_ns >= 0) {
          int64_t ellapsed_time = clockTimeNanoseconds() - start_time;
          if (ellapsed_time >= timeout_ns) {
            // Timed out
            if (timed_out_ret != NULL) {
              *timed_out_ret = true;
              return true;
            } else {
              // Can't report the time out
              TAKYON_RECORD_ERROR(error_message, "Timed out starting the transfer\n");
              return false;
            }
          }
        }
      } else if (sock_error == WSAEINTR) {
        // Interrupted by external signal. Just try again
      } else {
        TAKYON_RECORD_ERROR(error_message, "Failed to read from socket: error=%d\n", sock_error);
        return false;
      }
    }
  }
}

bool socketCreateUnicastSender(const char *ip_addr, uint16_t port_number, TakyonSocket *socket_fd_ret, void **sock_in_addr_ret, char *error_message) {
  if (!windows_socket_manager_init(error_message)) {
    return false;
  }

  // Create a datagram socket on which to send.
  TakyonSocket socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_fd < 0) {
    TAKYON_RECORD_ERROR(error_message, "Failed to create datagram sender socket: error=%d\n", WSAGetLastError());
    return false;
  }

  struct sockaddr_in *sock_in_addr = malloc(sizeof(struct sockaddr_in));
  if (sock_in_addr == NULL) {
    TAKYON_RECORD_ERROR(error_message, "Out of memory\n");
    closesocket(socket_fd);
    return false;
  }
  memset((char *)sock_in_addr, 0, sizeof(struct sockaddr_in));
#ifdef __APPLE__
  sock_in_addr->sin_len = sizeof(struct sockaddr_in);
#endif
  sock_in_addr->sin_family = AF_INET;
  int status = inet_pton(AF_INET, ip_addr, &sock_in_addr->sin_addr);
  if (status == 0) {
    TAKYON_RECORD_ERROR(error_message, "Could not get the IP address from the hostname '%s'. Invalid name.\n", ip_addr);
    closesocket(socket_fd);
    return false;
  } else if (status == -1) {
    TAKYON_RECORD_ERROR(error_message, "Could not get the IP address from the hostname '%s'. sock_error=%d\n", ip_addr, WSAGetLastError());
    closesocket(socket_fd);
    return false;
  }
  sock_in_addr->sin_port = htons(port_number);
  *sock_in_addr_ret = sock_in_addr;

  *socket_fd_ret = socket_fd;
  return true;
}

bool socketDatagramSend(TakyonSocket socket_fd, void *sock_in_addr, void *addr, size_t bytes_to_write, bool is_polling, int64_t timeout_ns, bool *timed_out_ret, char *error_message) {
  if (is_polling) {
    return datagram_send_polling(socket_fd, sock_in_addr, addr, bytes_to_write, timeout_ns, timed_out_ret, error_message);
  } else {
    return datagram_send_event_driven(socket_fd, sock_in_addr, addr, bytes_to_write, timeout_ns, timed_out_ret, error_message);
  }
}

bool socketCreateUnicastReceiver(const char *ip_addr, uint16_t port_number, bool allow_reuse, TakyonSocket *socket_fd_ret, char *error_message) {
  if (!windows_socket_manager_init(error_message)) {
    return false;
  }

  // Create a datagram socket on which to receive.
  TakyonSocket socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_fd < 0) {
    TAKYON_RECORD_ERROR(error_message, "Failed to create datagram socket: error=%d\n", WSAGetLastError());
    return false;
  }

  // Bind to the proper port number with the IP address specified as INADDR_ANY.
  // I.e. only allow packets received on the specified port number
  struct sockaddr_in localSock;
  memset((char *) &localSock, 0, sizeof(localSock));
#ifdef __APPLE__
  localSock.sin_len = sizeof(localSock);
#endif
  localSock.sin_family = AF_INET;
  localSock.sin_port = htons(port_number);
  if (strcmp(ip_addr, "Any") == 0) {
    // NOTE: htonl(INADDR_ANY) will allow any IP interface to listen
    localSock.sin_addr.s_addr = htonl(INADDR_ANY);
  } else {
    // Listen on a specific IP interface
    int status = inet_pton(AF_INET, ip_addr, &localSock.sin_addr);
    if (status == 0) {
      TAKYON_RECORD_ERROR(error_message, "Could not get the IP address from the hostname '%s'. Invalid name.\n", ip_addr);
      closesocket(socket_fd);
      return false;
    } else if (status == -1) {
      TAKYON_RECORD_ERROR(error_message, "Could not get the IP address from the hostname '%s'. sock_error=%d\n", ip_addr, WSAGetLastError());
      closesocket(socket_fd);
      return false;
    }
  }

  if (allow_reuse) {
    // This will allow a previously closed socket, that is still in the TIME_WAIT stage, to be used.
    // This may accur if the application did not exit gracefully on a previous run.
    BOOL allow = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&allow, sizeof(allow)) != 0) {
      TAKYON_RECORD_ERROR(error_message, "Failed to set datagram socket to SO_REUSEADDR. error=%d\n", WSAGetLastError());
      closesocket(socket_fd);
      return false;
    }
  }

  // Bind the port number and socket
  if (bind(socket_fd, (struct sockaddr*)&localSock, sizeof(localSock))) {
    int sock_error = WSAGetLastError();
    if (sock_error == WSAEADDRINUSE) {
      TAKYON_RECORD_ERROR(error_message, "Could not bind datagram socket. The address is already in use or in a time wait state. May need to use the option '-reuse'\n");
    } else {
      TAKYON_RECORD_ERROR(error_message, "Failed to bind datagram socket: error=%d\n", sock_error);
    }
    closesocket(socket_fd);
    return false;
  }

  *socket_fd_ret = socket_fd;
  return true;
}

bool socketDatagramRecv(TakyonSocket socket_fd, void *data_ptr, size_t buffer_bytes, uint64_t *bytes_read_ret, bool is_polling, int64_t timeout_ns, bool *timed_out_ret, char *error_message) {
  *bytes_read_ret = 0;
  if (is_polling) {
    return datagram_recv_polling(socket_fd, data_ptr, buffer_bytes, bytes_read_ret, timeout_ns, timed_out_ret, error_message);
  } else {
    return datagram_recv_event_driven(socket_fd, data_ptr, buffer_bytes, bytes_read_ret, timeout_ns, timed_out_ret, error_message);
  }
}

bool socketCreateMulticastSender(const char *ip_addr, const char *multicast_group, uint16_t port_number, bool disable_loopback, int ttl_level, TakyonSocket *socket_fd_ret, void **sock_in_addr_ret, char *error_message) {
  if (!windows_socket_manager_init(error_message)) {
    return false;
  }

  // Create a datagram socket on which to send.
  TakyonSocket socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_fd < 0) {
    TAKYON_RECORD_ERROR(error_message, "Failed to create datagram sender socket: error=%d\n", WSAGetLastError());
    return false;
  }

  // Disable loopback so you do not receive your own datagrams.
  {
    char loopch = 0;
    int loopch_length = sizeof(loopch);
    if (getsockopt(socket_fd, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&loopch, &loopch_length) < 0) {
      TAKYON_RECORD_ERROR(error_message, "Getting IP_MULTICAST_LOOP error when trying to determine loopback value");
      closesocket(socket_fd);
      return false;
    }
    char expected_loopch = disable_loopback ? 0 : 1;
    if (loopch != expected_loopch) {
      loopch = expected_loopch;
      if (setsockopt(socket_fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loopch, sizeof(loopch)) < 0) {
        TAKYON_RECORD_ERROR(error_message, "Getting IP_MULTICAST_LOOP error when trying to %s loopback", expected_loopch ? "enable" : "disable");
        closesocket(socket_fd);
        return false;
      }
    }
  }

  // Set local interface for outbound multicast datagrams.
  // The IP address specified must be associated with a local, multicast capable interface.
  {
    struct in_addr localInterface;
    int status = inet_pton(AF_INET, ip_addr, &localInterface.s_addr);
    if (status == 0) {
      TAKYON_RECORD_ERROR(error_message, "Could not get the IP address from the hostname '%s'. Invalid name.\n", ip_addr);
      closesocket(socket_fd);
      return false;
    } else if (status == -1) {
      TAKYON_RECORD_ERROR(error_message, "Could not get the IP address from the hostname '%s'. sock_error=%d\n", ip_addr, WSAGetLastError());
      closesocket(socket_fd);
      return false;
    }
    if (setsockopt(socket_fd, IPPROTO_IP, IP_MULTICAST_IF, (char *)&localInterface, sizeof(localInterface)) < 0) {
      TAKYON_RECORD_ERROR(error_message, "Failed to set local interface for multicast");
      closesocket(socket_fd);
      return false;
    }
  }

  // Set the Time-to-Live of the multicast to 1 so it's only on the local subnet.
  {
    // Supported levels:
    // 0:   Are restricted to the same host
    // 1:   Are restricted to the same subnet
    // 32:  Are restricted to the same site
    // 64:  Are restricted to the same region
    // 128: Are restricted to the same continent
    // 255: Are unrestricted in scope
    int router_depth = ttl_level;
    if (setsockopt(socket_fd, IPPROTO_IP, IP_MULTICAST_TTL, (char *)&router_depth, sizeof(router_depth)) < 0) {
      TAKYON_RECORD_ERROR(error_message, "Failed to set IP_MULTICAST_TTL for multicast");
      closesocket(socket_fd);
      return false;
    }
  }

  // Set the multicast address
  struct sockaddr_in *sock_in_addr = malloc(sizeof(struct sockaddr_in));
  if (sock_in_addr == NULL) {
    TAKYON_RECORD_ERROR(error_message, "Out of memory\n");
    closesocket(socket_fd);
    return false;
  }
  memset((char *)sock_in_addr, 0, sizeof(struct sockaddr_in));
#ifdef __APPLE__
  sock_in_addr->sin_len = sizeof(struct sockaddr_in);
#endif
  sock_in_addr->sin_family = AF_INET;
  int status = inet_pton(AF_INET, multicast_group, &sock_in_addr->sin_addr);
  if (status == 0) {
    TAKYON_RECORD_ERROR(error_message, "Could not get the IP address from the hostname '%s'. Invalid name.\n", ip_addr);
    closesocket(socket_fd);
    return false;
  } else if (status == -1) {
    TAKYON_RECORD_ERROR(error_message, "Could not get the IP address from the hostname '%s'. sock_error=%d\n", ip_addr, WSAGetLastError());
    closesocket(socket_fd);
    return false;
  }
  sock_in_addr->sin_port = htons(port_number);
  *sock_in_addr_ret = sock_in_addr;

  *socket_fd_ret = socket_fd;
  return true;
}

bool socketCreateMulticastReceiver(const char *ip_addr, const char *multicast_group, uint16_t port_number, bool allow_reuse, TakyonSocket *socket_fd_ret, char *error_message) {
  if (!windows_socket_manager_init(error_message)) {
    return false;
  }

  // Create a datagram socket on which to receive.
  TakyonSocket socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_fd < 0) {
    TAKYON_RECORD_ERROR(error_message, "Failed to create multicast socket: error=%d\n", WSAGetLastError());
    return false;
  }

  // Bind to the proper port number with the IP address specified as INADDR_ANY.
  // I.e. only allow packets received on the specified port number
  struct sockaddr_in localSock;
  memset((char *) &localSock, 0, sizeof(localSock));
#ifdef __APPLE__
  localSock.sin_len = sizeof(localSock);
#endif
  localSock.sin_family = AF_INET;
  localSock.sin_port = htons(port_number);
  // NOTE: htonl(INADDR_ANY) will allow any IP interface to listen
  // IMPORTANT: can't use an actual IP address here or else won't work. The interface address is set when the group is set (below).
  localSock.sin_addr.s_addr = htonl(INADDR_ANY);

  if (allow_reuse) {
    // This will allow a previously closed socket, that is still in the TIME_WAIT stage, to be used.
    // This may accur if the application did not exit gracefully on a previous run.
    BOOL allow = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&allow, sizeof(allow)) != 0) {
      TAKYON_RECORD_ERROR(error_message, "Failed to set multicast socket to SO_REUSEADDR. error=%d\n", WSAGetLastError());
      closesocket(socket_fd);
      return false;
    }
  }

  // Bind the port number and socket
  if (bind(socket_fd, (struct sockaddr*)&localSock, sizeof(localSock))) {
    int sock_error = WSAGetLastError();
    if (sock_error == WSAEADDRINUSE) {
      TAKYON_RECORD_ERROR(error_message, "Could not bind multicast socket. The address is already in use or in a time wait state. May need to use the option '-reuse'\n");
    } else {
      TAKYON_RECORD_ERROR(error_message, "Failed to bind multicast socket: error=%d\n", sock_error);
    }
    closesocket(socket_fd);
    return false;
  }

  // Join the multicast group <multicast_group_addr> on the local network interface <reader_network_interface_addr> interface.
  // Note that this IP_ADD_MEMBERSHIP option must be called for each local interface over which the multicast
  // datagrams are to be received.
  {
    struct ip_mreq group;
    int status = inet_pton(AF_INET, multicast_group, &group.imr_multiaddr);
    if (status == 0) {
      TAKYON_RECORD_ERROR(error_message, "Could not get the IP address from the hostname '%s'. Invalid name.\n", ip_addr);
      closesocket(socket_fd);
      return false;
    } else if (status == -1) {
      TAKYON_RECORD_ERROR(error_message, "Could not get the IP address from the hostname '%s'. sock_error=%d\n", ip_addr, WSAGetLastError());
      closesocket(socket_fd);
      return false;
    }
    status = inet_pton(AF_INET, ip_addr, &group.imr_interface);
    if (status == 0) {
      TAKYON_RECORD_ERROR(error_message, "Could not get the IP address from the hostname '%s'. Invalid name.\n", ip_addr);
      closesocket(socket_fd);
      return false;
    } else if (status == -1) {
      TAKYON_RECORD_ERROR(error_message, "Could not get the IP address from the hostname '%s'. sock_error=%d\n", ip_addr, WSAGetLastError());
      closesocket(socket_fd);
      return false;
    }
    if (setsockopt(socket_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&group, sizeof(group)) < 0) {
      TAKYON_RECORD_ERROR(error_message, "Failed to add socket to the multicast group: error=%d\n", WSAGetLastError());
      closesocket(socket_fd);
      return false;
    }
  }

  *socket_fd_ret = socket_fd;
  return true;
}
