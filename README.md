![Takyon Logo](docs/Takyon_Abaco-logo.jpg)

## License
  Copyright 2018,2020 Abaco Systems
  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at
      http://www.apache.org/licenses/LICENSE-2.0
  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

## About
Takyon is a high level, high speed, portable, dynamic, fully scalable, point to point, message passing, communication API. It's focused on the **Embedded HPC** industry, with no intention to compete with MPI which is focused on the **HPC** industry.

**Like MPI**, Takyon is designed to be a wrapper over many low level point to point communication APIs and look like a single high level message passing API.

**Unlike MPI**, Takyon is designed for the extra requirements of the **Embedded HPC** industry:
- Better performance via one-way, zero-copy transfers on memory blocks that are preregistered when a communication path is created
- Better determinism: once a path is created, there's no implicit extra transfers, extra synchronization, memory allocations, or memory registrations
- Fault tolerant ready at the application level: all communication paths are independent of each other, can be created or destroyed at any time, all failures are reported to the application, and timeouts are provided for all stages of communication
- Unconnected communications (unicast & multicast datagrams) where data can be dropped
- Can be used to communicate with IO devices: cameras, lidar, A2D converter, D2A converter, etc.

Takyon provides an application with a one stop shop for all point to point message passing needs no mater the interconnect or locality (inter-thread, inter-process, inter-processor, intra-application, inter-application).

## Potential to Become a Standard
The Khronos organization (khronos.org), that brought the world OpenGL, Vulkan, OpenCL, etc., has approved the need of a "heterogeneous communication standard" working group. The working group will be formed once the group has reached a quorum. Takyon is currently the only proposed API, but widely accepted by the exploratory group members. Visit the Khronos website to see more details on the need for heterogeneous communication: https://www.khronos.org/exploratory/heterogeneous-communication/ Contact Khronos if you wish to be a member of the working group to define the new standard.

## Functionality
Takyon has two groups of functionality.

### Core (only 5 functions!!!)
Takyon's core functionality (the foundation) is simple yet extremely flexible:
```
takyonCreate()         - Create a connected or unconnected communication path
takyonSend()           - Send a message
takyonIsSendFinished() - Checks if a non-blocking send is finished
takyonRecv()           - Wait for a message to arrive
takyonDestroy()        - Destroy the communication path
```
All core functionality is defined in the header file:
```
Takyon/API/inc/takyon.h
```
### Open Source Extensions
Takyon also includes open source functionality to provide references to common communication functionality. It's provided as source code to allow for easy modification/tuning as needed by the application requirements. The included functionality:
- Time: sleep, wall clock
- Path Attribute Management: makes multi-buffer list management a little easier 
- Endian: testing & swapping
- Shared Memory Management: manage named buffers for inter-process communication
- Collective Functions: barrier, scatter, gather, reduce, custom
- Graph File Management: defining data-flow via a config file; to avoid hard-coding in source

All extensions are defined in the header file:
```
Takyon/extenstions/takyon_extensions.h
```

## Simple Example
Here's an example of a connected communication path that could work with any two-sided interconnect:
```
#include "takyon.h"

#define INTERCONNECT_ENDPOINT_A "Socket -client -IP=192.168.33.23 -ID=45"
#define INTERCONNECT_ENDPOINT_B "Socket -server -IP=Any -ID=45"

void simpleExample(bool is_endpointA) {
  // Setup the two-sided communication path attributes
  TakyonPathAttributes attrs;
  attrs.is_endpointA                = is_endpointA;
  attrs.is_polling                  = false;
  attrs.abort_on_failure            = true;
  attrs.verbosity                   = TAKYON_VERBOSITY_ERRORS;
  if (attrs.is_endpointA) {
    strncpy(attrs.interconnect, INTERCONNECT_ENDPOINT_A, TAKYON_MAX_INTERCONNECT_CHARS);
  } else {
    strncpy(attrs.interconnect, INTERCONNECT_ENDPOINT_B, TAKYON_MAX_INTERCONNECT_CHARS);
  }
  attrs.path_create_timeout         = TAKYON_WAIT_FOREVER;
  attrs.send_start_timeout          = TAKYON_WAIT_FOREVER;
  attrs.send_finish_timeout         = TAKYON_WAIT_FOREVER;
  attrs.recv_start_timeout          = TAKYON_WAIT_FOREVER;
  attrs.recv_finish_timeout         = TAKYON_WAIT_FOREVER;
  attrs.path_destroy_timeout        = TAKYON_WAIT_FOREVER;
  attrs.send_completion_method      = TAKYON_BLOCKING;
  attrs.recv_completion_method      = TAKYON_BLOCKING;
  attrs.nbufs_AtoB                  = 1;
  attrs.nbufs_BtoA                  = 1;
  uint64_t sender_max_bytes_list[1] = { 1024 };
  attrs.sender_max_bytes_list       = sender_max_bytes_list;
  uint64_t recver_max_bytes_list[1] = { 1024 };
  attrs.recver_max_bytes_list       = recver_max_bytes_list;
  size_t sender_addr_list[1]        = { 0 };
  attrs.sender_addr_list            = sender_addr_list;
  size_t recver_addr_list[1]        = { 0 };
  attrs.recver_addr_list            = recver_addr_list;

  // Create the path
  TakyonPath *path = takyonCreate(&attrs);

  // Pass some friendly greetings around and around
  const char *message = is_endpointA ? "Hello from endpoint A" : "Hello from endpoint B";
  for (int i=0; i<5; i++) {
    int buffer_index = 0;
    uint64_t max_send_buffer_bytes = path->attrs.sender_max_bytes_list[buffer_index];
    char *send_buffer = (char *)path->attrs.sender_addr_list[buffer_index];
    char *recv_buffer = (char *)path->attrs.recver_addr_list[buffer_index];
    if (is_endpointA) {
      strncpy(send_buffer, message, max_send_buffer_bytes);
      takyonSend(path, buffer_index, strlen(message)+1, 0, 0, NULL);
      takyonRecv(path, buffer_index, NULL, NULL, NULL);
      printf("Endpoint A received message %d: %s\n", i, recv_buffer);
    } else {
      takyonRecv(path, buffer_index, NULL, NULL, NULL);
      printf("Endpoint B received message %d: %s\n", i, recv_buffer);
      strncpy(send_buffer, message, max_send_buffer_bytes);
      takyonSend(path, buffer_index, strlen(message)+1, 0, 0, NULL);
    }
  }

  // Destroy the path
  takyonDestroy(&path);
```
To change to a different locality/interconnect, just modify INTERCONNECT_ENDPOINT_A and INTERCONNECT_ENDPOINT_B with any supported interconnect.

## Documentation
For all the great details on Takyon, read the following PDFs in Takyon's `docs/` folder:
- Takyon Reference Sheet.pdf - All the details at a quick glance.
- Takyon Users Guide.pdf     - In depth details of the design and how to use the API.

## Takyon Core Build Instructions
Before running any examples, the Takyon core libraries must be built.

### Linux
*Tested on CentOS 8.2, Ubuntu 16.04, 18.04 64-bit Intel using gcc, but should work on most other Linux flavors and chip architectures.*
```
> cd Takyon/API/builds/linux
> make                  # No CUDA integration
> make WITH_CUDA=Yes    # Allows some interconnects to optionally use CUDA memory buffers
```
This creates:
```
libTakyon.a                (interconnects are dynamically loaded)
libTakyon<interconnect>.so (one per interconnect to be used with libTakyon.a)
libTakyonStatic.a          (single library with all supported inteconnects)
```
### Mac OSX
*Tested on OSX 10.13 64-bit using gcc*
```
> cd Takyon/API/builds/mac
> make
```
This creates:
```
libTakyon.a                (interconnects are dynamically loaded)
libTakyon<interconnect>.so (one per interconnect to be used with libTakyon.a)
libTakyonStatic.a          (single library with all supported inteconnects)
```
### Windows
*Tested on Window 10 64-bit using MSVC 2015, 2017, 2019*

Takyon uses POSIX threads (i.e. pthreads), and by default MSVC does not include support for pthreads, so it must be manually downloaded and built.
1. Download the source from: https://sourceforge.net/projects/pthreads4w/
2. Unzip in the C: drive and rename the folder to `c:\pthreads4w`
3. Start an MSVC command shell (not PowerShell), make sure to run the `vcvars64.bat`
```
> cd c:\pthreads4w
> nmake VC VC-debug VC-static VC-static-debug install DESTROOT=.\install
```
This creates the header files and libraries files in:
```
c:/pthreads4w/install/include/
c:/pthreads4w/install/lib/
c:/pthreads4w/install/bin/
```
Now you can build the Takyon library:
```
> cd Takyon\API\builds\windows
> nmake                  # No CUDA integration
> nmake WITH_CUDA=Yes    # Allows some interconnects to optionally use CUDA memory buffers
```
This creates:
```
Takyon.lib                 (single library with all supported inteconnects)
```

## Preparing to Run Examples
This is a one-time step before running any of the examples.

### Linux
To know where the static and dynamic libraries are, then run the following first:
```
> export TAKYON_LIBS="<Takyon_parent_folder>/Takyon/API/builds/linux"
```

### Mac OSX
To know where the static and dynamic libraries are, then run the following first:
```
> export TAKYON_LIBS="<Takyon_parent_folder>/Takyon/API/builds/mac"
```

### Windows
To know where the static library is, then run the following first:
```
> set TAKYON_LIBS="<Takyon_parent_folder>\Takyon\API\builds\windows"
```

## Examples
The examples are found in `Takyon/examples/`

### Building and Running
Each example has a `README.txt` which explains what is does and how to build and run.

### Overview of the Examples
Getting Started:
- **hello_world_mt**: Communicate between two threads in a single process
- **hello_world_mp**: Communicate between two processes (same or different processors)

Some Key Techniques:
- **performance**: Calculates the average latency and throughput of various sized transfers
- **determinism**: Displays histogram of transfer times to understand determinism
- **fault_tolerant**: Shows how to recover from failures (via timeouts, cable disconnection, or control-C)
- **one_sided**: A connected path, but one endpoint is 3rd party; i.e. not a Takyon endpoint
- **connectionless**: Send or receive datagrams (multicast or unicast). Allows data dropping. Ideal for live streaming.
- **callbacks**: Shows how to use Takyon communication in a callback model.

Graph Based (more like MPI; data flow is defined in config file to avoid hard coding in the application):
- **hello_world_graph**: A simple example of how to use the graph file
- **hello_world_cuda**: A simple example of how to use CUDA mixed with CPU memory buffers
- **barrier**: Uses a barrier to synchronize the pipeline processing
- **reduce**: Does an app defined reduction (in this case: max) and optionally passes the result to all endpoints
- **pipeline**: Pipeline style processing where paths are connected in series
- **scatter_gather**: Scatters from a single contiguous buffer, processes, then gathers into a single contiguous buffer

## Supported Interconnects
This reference implementation supports commonly used communication interfaces for inter-thread, inter-process and inter-processor communication. Abaco's commercial implementation supports additional interconnects including RDMA with GPUDirect.

### InterThreadMemcpy
Endpoints must be in the same process but different threads. Uses Posix mutexes and conditional variables to handle synchronization of memcpy() transfers.

Required arguments:
```
-ID=<integer>                 (<integer> can be any value, must be the same on both endpoints)
```
Optional arguments:
```
-srcCudaDeviceId=<id>         (For Takyon managed source buffers, allocate on CUDA device <id>)
-destCudaDeviceId=<id>        (For Takyon managed destination buffers, allocate on CUDA device <id>)
```
### InterThreadPointer
Endpoints must be in the same process but different threads. Uses Posix mutexes and conditional variables to handle synchronization of pointers to shared blocks of memory (shared by the sender and receiver).

Required arguments:
```
-ID=<integer>                 (<integer> can be any value, must be the same on both endpoints)
```
Optional arguments:
```
-destCudaDeviceId=<id>        (For Takyon managed destination buffers, allocate on CUDA device <id>)
```
### InterProcessMemcpy
Endpoints must be in the same OS but different processes. Uses local sockets to coordinate path creation, path destruction, and synchronization of memcpy() transfers. The receive buffers are shared memory maps.

Required arguments:
```
-ID=<integer>                 (<integer> can be any value, must be the same on both endpoints)
```
Optional Arguments;
```
-recverAddrMmapNamePrefix=<name>  (If the application allocates the destination buffers, which
                                   must be named shared memory, then the names must follow the
                                   format: "<name><buffer_index>". This only needs to be
                                   specified on the endpoint allocating the memory.)
-srcCudaDeviceId=<id>         (For Takyon managed source buffers, allocate on CUDA device <id>)
-destCudaDeviceId=<id>        (For Takyon managed destination buffers, allocate on CUDA device <id>)
```

### InterProcessPointer
Endpoints must be in the same OS but different processes. Uses local sockets to coordinate path creation, path destruction, and synchronization of pointers to shared blocks of memory (shared by the sender and receiver). The receive buffers are shared memory maps.

Required arguments:
```
-ID=<integer>                 (<integer> can be any value, must be the same on both endpoints)
```
Optional Arguments;
```
-recverAddrMmapNamePrefix=<name>  (If the application allocates the destination buffers, which
                                   must be named shared memory, then the names must follow the
                                   format: "<name><buffer_index>". This only needs to be
                                   specified on the endpoint allocating the memory.)
-destCudaDeviceId=<id>        (For Takyon managed destination buffers, allocate on CUDA device <id>)
```

### InterProcessSocket
Endpoints must be in the same OS but different processes. Uses a local socket to coordinate path creation, path destruction, and the transfers.

Required arguments:
```
-ID=<integer>                 (<integer> can be any value, must be the same on both endpoints)
```

### Socket
Endpoints can be in the same OS or different OSes, but in the same IP network. Uses a TCP socket to do transfers.

If need to use a well known IP port number 'port' for connecting:
```
-client -IP=<IP> -port=<port>                 (can be use on either enpdpoint.
                                               <IP> is the address of the remote server.)
-server -IP=<IP> -port=<port> [-reuse]        (use this on the opposite endpoint as the client.
                                               <IP> is the local IP inferface of this server.
                                               -reuse is optional, and allows port number to be
                                               reused without waiting in case of a failure.)
-server -IP=Any -port=<port> [-reuse]         (Alternative, where IP address is auto detected
                                               from activity on the port number)
```
If you want to let the system determine an unused (ephemeral) IP port number:
```
-client -IP=<IP> -ID=<id>                     (can be use on either enpdpoint.
                                               <IP> is the address of the remote server.)
-server -IP=<IP> -ID=<id>                     (use this on the opposite endpoint as the client.
                                               <IP> is the local IP inferface of this server.)
-server -IP=Any -ID=<id>                      (Alternative, where IP address is auto detected
                                               from activity on the port number)
```
For the above ephemeral port number method, Takyon uses implicit multicast transfers to pass the ephemeral port from the server to the client. The following Takyon multicast defaults can be overridden by environment variables:
```
TAKYON_MULTICAST_IP    "127.0.0.1"            An IP interface that is multicast capable
TAKYON_MULTICAST_PORT   6736                  Fun note: Uses phone digits to spell "Open"
TAKYON_MULTICAST_GROUP "229.82.29.66"         Fun note: Uses phone digits to spell "Takyon"
                                                        i.e. 229.TA.KY.ON
TAKYON_MULTICAST_TTL    1                     Restricts multicasting to the same subnet
```
### OneSidedSocket
Use this when Takyon needs to connect to a 3rd party TCP socket endpoint.
```
-client -IP=<IP> -port=<port>                 (Use this to connect to a 3rd party server.
                                               <IP> is the address of the remote server.)
-server -IP=<IP> -port=<port> [-reuse]        (Use this to connect to a 3rd party client.
                                               <IP> is the local IP inferface of this server.
                                               -reuse is optional, and allows port number to be
                                               reused without waiting in case of a failure.)
-server -IP=Any -port=<port> [-reuse]         (Alternative, where IP address is auto detected
                                               from activity on the port number)
```
Restrictions:
- Client must be endpoint A, and server must be endpoint B.
- Multiple buffers can be used, but remote 3rd party side will just get data in order sent.
- For receiving, takyonRecv()'s arguments for 'bytes' and 'offset' must not be NULL and must be set before calling takyonRecv(). This lets the socket know how much data to receive, and where to put it in the Takyon buffer.

### UnicastSendSocket
Send UDP datagrams to a single endpoint:
```
-IP=<IP> -port=<port>                         (<IP> is IP address of the remote destination)
```
Restrictions:
- Must be endpoint A.
- Multiple buffers can be used, but remote side will not have any concept of the order or buffer indices.
- The destination offset of takyonSend() must always be zero.

### UnicastRecvSocket
Receive UDP datagrams to a single endpoint:
```
-IP=<IP> -port=<port> [-reuse]                (<IP> is the local IP inferface of this receiver.
                                               -reuse is optional, and allows port number to be
                                               reused without waiting in case of a failure.)
-IP=Any -port=<port> [-reuse]                 (Alternative, where IP address is auto detected
                                               from activity on the port number)
```
Restrictions:
- Must be endpoint B.
- Data can be dropped, and order of data is not guaranteed.
- Multiple buffers can be used, but this is not a coordination with the remote sender.
- If takyonRecv() gets the offset, the offset will always be set to zero.

### MulticastSendSocket
Send UDP datagrams to a multicast group:
```
-IP=<IP> -group=<gIP> -port=<port>            (<IP> is address of a local IP interface.
                                               <gIP> is the IP multicast address between
                                               224.0.0.0 and 239.255.255.255; some of these
                                               adresses are reserved)
```
Options:
```
-noLoopback                                   (By default, the multicast datagrams will be
                                               looped back to this IP interface. Use this
                                               flag to stop this.)
-TTL=<n>                                      (Defines how far down the network the multicast
                                               datagram will be sent:
                                               0:   Same host
                                               1:   Same subnet (default)
                                               32:  Same site
                                               64:  Same region
                                               128: Same continent
                                               255: Everywhere
```
Restrictions:
- Must be endpoint A.
- Multiple buffers can be used, but remote endpoints will not have any concept of the order or buffer indices.
- The destination offset of takyonSend() must always be zero.

### MulticastRecvSocket
Receive UDP datagrams from a multicast group:
```
-IP=<IP> -group=<gIP> -port=<port> [-reuse]   (<IP> is the local IP inferface of this receiver.
                                               <gIP> is the IP multicast address between
                                               224.0.0.0 and 239.255.255.255; some of these
                                               adresses are reserved.
                                               -reuse is optional, and allows port number to be
                                               reused without waiting in case of a failure.)

```
Restrictions:
- Must be endpoint B.
- Data can be dropped, and order of data is not guaranteed.
- Multiple buffers can be used, but this is not a coordination with the multicast sender.
- If takyonRecv() gets the offset, the offset will always be set to zero.

## The Evolution of Takyon
The Takyon API was formulated by Michael Both after about 20 years of challenging experiences with communication APIs for heterogeneous compute architectures in the embedded HPC industry. He implemented applications using many standard communication APIs (Socket, MPI, Verbs, Network Direct, named memory map, message queue, semaphore, mutex, cond var, memcpy, corba), many company proprietary APIs (Abaco, Mercury, Ixthos, Texas Instruments, Sparc, Sky Computers, Radstone Technologies, Google, Apple), and on many different architectures (Sparc, PPC, Sharc, TI, Intel, Arm, iOS, Android). In addition to using all these communication APIs, he also implemented one high level open standard (Abaco's MPI 1.x) and two high level proprietary APIs (Lockheed Martin's GEDAE and Abaco's AXIS Flow). This vast experience gave a great insight into the strengths and weaknesses of each communication API. One API did not fit all the needs of the common embedded HPC application, and it became clear that a better standard was needed for this audience. Michael first approached Khronos in 2017 to see if Takyon should become an open standard. In 2018, Khronos decided to create an exploratory group to determine industry interest. In 2019, Khronos approved the formation of a working group to define the specification.
