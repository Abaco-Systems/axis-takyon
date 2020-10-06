# Copyright 2018,2020 Abaco Systems
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#     http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


vpath %.c ../../src
vpath %.h ../../inc

TAKYON_C_FILES :=
TAKYON_C_FILES += takyon.c
TAKYON_C_FILES += utils_arg_parser.c
TAKYON_C_FILES += utils_endian.c
TAKYON_C_FILES += utils_ephemeral_port_manager.c
TAKYON_C_FILES += utils_inter_thread_manager.c
TAKYON_C_FILES += utils_memory_linux.c
TAKYON_C_FILES += utils_mmap_linux.c
TAKYON_C_FILES += utils_shared_libraries.c
TAKYON_C_FILES += utils_socket_linux.c
TAKYON_C_FILES += utils_threads.c
TAKYON_C_FILES += utils_time_linux.c
ifeq ($(WITH_RDMA),Yes)
    TAKYON_C_FILES += utils_rdma_verbs.c
endif
ifeq ($(WITH_CUDA),Yes)
    TAKYON_C_FILES += utils_memory_cuda.c
endif

TAKYON_H_FILES :=
TAKYON_H_FILES += ../../inc/takyon.h
TAKYON_H_FILES += ../../inc/takyon_private.h
ifeq ($(WITH_RDMA),Yes)
    TAKYON_H_FILES += ../../inc/utils_rdma_verbs.h
endif

CFLAGS         := -std=gnu99 -Wall -Werror -pthread -I../../inc
#CFLAGS         += -g -O0
CFLAGS         += -O2
ifeq ($(WITH_RDMA),Yes)
    CFLAGS += -DWITH_RDMA
endif
ifeq ($(WITH_CUDA),Yes)
    CFLAGS += -DWITH_CUDA -I/usr/local/cuda/include
endif
SO_CFLAGS      := $(CFLAGS) -fPIC
SO_LFLAGS      := -shared
TAKYON_C_OBJS  := $(patsubst %.c,%.o,$(TAKYON_C_FILES))
TAKYON_LIBRARY := libTakyon.a

ITM_C_FILES  := takyon_InterThreadMemcpy.c
ITM_C_OBJS   := $(patsubst %.c,%.o,$(ITM_C_FILES))
ITM_LIBRARY  := libTakyonInterThreadMemcpy.so

ITP_C_FILES  := takyon_InterThreadPointer.c
ITP_C_OBJS   := $(patsubst %.c,%.o,$(ITP_C_FILES))
ITP_LIBRARY  := libTakyonInterThreadPointer.so

IPM_C_FILES  := takyon_InterProcessMemcpy.c
IPM_C_OBJS   := $(patsubst %.c,%.o,$(IPM_C_FILES))
IPM_LIBRARY  := libTakyonInterProcessMemcpy.so

IPP_C_FILES  := takyon_InterProcessPointer.c
IPP_C_OBJS   := $(patsubst %.c,%.o,$(IPP_C_FILES))
IPP_LIBRARY  := libTakyonInterProcessPointer.so

IPSOCKET_C_FILES  := takyon_InterProcessSocket.c
IPSOCKET_C_OBJS   := $(patsubst %.c,%.o,$(IPSOCKET_C_FILES))
IPSOCKET_LIBRARY  := libTakyonInterProcessSocket.so

TCP_SOCKET_C_FILES  := takyon_Socket.c
TCP_SOCKET_C_OBJS   := $(patsubst %.c,%.o,$(TCP_SOCKET_C_FILES))
TCP_SOCKET_LIBRARY  := libTakyonSocket.so

ONE_SIDED_TCP_SOCKET_C_FILES  := takyon_OneSidedSocket.c
ONE_SIDED_TCP_SOCKET_C_OBJS   := $(patsubst %.c,%.o,$(ONE_SIDED_TCP_SOCKET_C_FILES))
ONE_SIDED_TCP_SOCKET_LIBRARY  := libTakyonOneSidedSocket.so

UNICAST_SEND_SOCKET_C_FILES  := takyon_UnicastSendSocket.c
UNICAST_SEND_SOCKET_C_OBJS   := $(patsubst %.c,%.o,$(UNICAST_SEND_SOCKET_C_FILES))
UNICAST_SEND_SOCKET_LIBRARY  := libTakyonUnicastSendSocket.so

UNICAST_RECV_SOCKET_C_FILES  := takyon_UnicastRecvSocket.c
UNICAST_RECV_SOCKET_C_OBJS   := $(patsubst %.c,%.o,$(UNICAST_RECV_SOCKET_C_FILES))
UNICAST_RECV_SOCKET_LIBRARY  := libTakyonUnicastRecvSocket.so

MULTICAST_SEND_SOCKET_C_FILES  := takyon_MulticastSendSocket.c
MULTICAST_SEND_SOCKET_C_OBJS   := $(patsubst %.c,%.o,$(MULTICAST_SEND_SOCKET_C_FILES))
MULTICAST_SEND_SOCKET_LIBRARY  := libTakyonMulticastSendSocket.so

MULTICAST_RECV_SOCKET_C_FILES  := takyon_MulticastRecvSocket.c
MULTICAST_RECV_SOCKET_C_OBJS   := $(patsubst %.c,%.o,$(MULTICAST_RECV_SOCKET_C_FILES))
MULTICAST_RECV_SOCKET_LIBRARY  := libTakyonMulticastRecvSocket.so

ifeq ($(WITH_RDMA),Yes)
    RDMA_C_FILES  := takyon_Rdma_verbs.c
    RDMA_C_OBJS   := $(patsubst %.c,%.o,$(RDMA_C_FILES))
    RDMA_LIBRARY  := libTakyonRdma.so

    UNICAST_SEND_RDMA_C_FILES  := takyon_UnicastSendRdma.c
    UNICAST_SEND_RDMA_C_OBJS   := $(patsubst %.c,%.o,$(UNICAST_SEND_RDMA_C_FILES))
    UNICAST_SEND_RDMA_LIBRARY  := libTakyonUnicastSendRdma.so

    UNICAST_RECV_RDMA_C_FILES  := takyon_UnicastRecvRdma.c
    UNICAST_RECV_RDMA_C_OBJS   := $(patsubst %.c,%.o,$(UNICAST_RECV_RDMA_C_FILES))
    UNICAST_RECV_RDMA_LIBRARY  := libTakyonUnicastRecvRdma.so

    MULTICAST_SEND_RDMA_C_FILES  := takyon_MulticastSendRdma.c
    MULTICAST_SEND_RDMA_C_OBJS   := $(patsubst %.c,%.o,$(MULTICAST_SEND_RDMA_C_FILES))
    MULTICAST_SEND_RDMA_LIBRARY  := libTakyonMulticastSendRdma.so

    MULTICAST_RECV_RDMA_C_FILES  := takyon_MulticastRecvRdma.c
    MULTICAST_RECV_RDMA_C_OBJS   := $(patsubst %.c,%.o,$(MULTICAST_RECV_RDMA_C_FILES))
    MULTICAST_RECV_RDMA_LIBRARY  := libTakyonMulticastRecvRdma.so
endif

STATIC_LIB_C_FILES := $(TAKYON_C_FILES) $(ITM_C_FILES) $(ITP_C_FILES) $(IPM_C_FILES) $(IPP_C_FILES) $(IPSOCKET_C_FILES) $(TCP_SOCKET_C_FILES) $(ONE_SIDED_TCP_SOCKET_C_FILES) $(ONE_SIDED__SOCKET_C_FILES) $(UNICAST_SEND_SOCKET_C_FILES) $(UNICAST_RECV_SOCKET_C_FILES) $(MULTICAST_SEND_SOCKET_C_FILES) $(MULTICAST_RECV_SOCKET_C_FILES)
ifeq ($(WITH_RDMA),Yes)
    STATIC_LIB_C_FILES += $(RDMA_C_FILES)
    STATIC_LIB_C_FILES += $(UNICAST_SEND_RDMA_C_FILES)
    STATIC_LIB_C_FILES += $(UNICAST_RECV_RDMA_C_FILES)
    STATIC_LIB_C_FILES += $(MULTICAST_SEND_RDMA_C_FILES)
    STATIC_LIB_C_FILES += $(MULTICAST_RECV_RDMA_C_FILES)
endif
STATIC_LIB_C_OBJS  := $(patsubst %.c,static_%.o,$(STATIC_LIB_C_FILES))
STATIC_LIBRARY     := libTakyonStatic.a

DYNAMIC_LIBRARIES := $(ITM_LIBRARY) $(ITP_LIBRARY) $(IPM_LIBRARY) $(IPP_LIBRARY) $(IPSOCKET_LIBRARY) $(TCP_SOCKET_LIBRARY) $(ONE_SIDED_TCP_SOCKET_LIBRARY) $(UNICAST_SEND_SOCKET_LIBRARY) $(UNICAST_RECV_SOCKET_LIBRARY) $(MULTICAST_SEND_SOCKET_LIBRARY) $(MULTICAST_RECV_SOCKET_LIBRARY)
ifeq ($(WITH_RDMA),Yes)
    DYNAMIC_LIBRARIES += $(RDMA_LIBRARY)
    DYNAMIC_LIBRARIES += $(UNICAST_SEND_RDMA_LIBRARY)
    DYNAMIC_LIBRARIES += $(UNICAST_RECV_RDMA_LIBRARY)
    DYNAMIC_LIBRARIES += $(MULTICAST_SEND_RDMA_LIBRARY)
    DYNAMIC_LIBRARIES += $(MULTICAST_RECV_RDMA_LIBRARY)
endif
DYNAMIC_LIB_C_OBJS := $(ITM_C_OBJS) $(ITP_C_OBJS) $(IPM_C_OBJS) $(IPP_C_OBJS) $(IPSOCKET_C_OBJS) $(TCP_SOCKET_C_OBJS) $(ONE_SIDED_TCP_SOCKET_C_OBJS) $(UNICAST_SEND_SOCKET_C_OBJS) $(UNICAST_RECV_SOCKET_C_OBJS) $(MULTICAST_SEND_SOCKET_C_OBJS) $(MULTICAST_RECV_SOCKET_C_OBJS)
ifeq ($(WITH_RDMA),Yes)
    DYNAMIC_LIB_C_OBJS += $(RDMA_C_OBJS)
    DYNAMIC_LIB_C_OBJS += $(UNICAST_SEND_RDMA_C_OBJS)
    DYNAMIC_LIB_C_OBJS += $(UNICAST_RECV_RDMA_C_OBJS)
    DYNAMIC_LIB_C_OBJS += $(MULTICAST_SEND_RDMA_C_OBJS)
    DYNAMIC_LIB_C_OBJS += $(MULTICAST_RECV_RDMA_C_OBJS)
endif

all: static_lib dynamic_lib
static_lib: $(STATIC_LIBRARY)
dynamic_lib: $(TAKYON_LIBRARY) $(DYNAMIC_LIBRARIES)

clean:
	rm -f *.o
	rm -f *~
	rm -f $(STATIC_LIBRARY)
	rm -f $(TAKYON_LIBRARY)
	rm -f *.so

$(STATIC_LIB_C_OBJS): static_%.o: %.c $(TAKYON_H_FILES)
	gcc -DBUILD_STATIC_LIB $(CFLAGS) -c $< -o $@

$(TAKYON_C_OBJS): %.o: %.c $(TAKYON_H_FILES)
	gcc $(CFLAGS) -c $< -o $@

$(DYNAMIC_LIB_C_OBJS): %.o: %.c $(TAKYON_H_FILES)
	gcc $(SO_CFLAGS) -c $< -o $@

$(TAKYON_LIBRARY): $(TAKYON_C_OBJS)
	ar rcs $@ $^

$(STATIC_LIBRARY): $(STATIC_LIB_C_OBJS)
	ar rcs $@ $^

# NOTE: Just in case shared object versioning is needed:
#ITM_HIGH_VER := 1
#ITM_LOW_VER  := 0
#$(ITM_LIBRARY).$(ITM_HIGH_VER).$(ITM_LOW_VER): $(ITM_C_OBJS)
#	gcc $(SO_LFLAGS) -Wl,-soname,$(ITM_LIBRARY).$(ITM_HIGH_VER) -o $(ITM_LIBRARY).$(ITM_HIGH_VER).$(ITM_LOW_VER) $(ITM_C_OBJS)
#	ln -sf $(ITM_LIBRARY).$(ITM_HIGH_VER).$(ITM_LOW_VER) $(ITM_LIBRARY).$(ITM_HIGH_VER)
#	ln -sf $(ITM_LIBRARY).$(ITM_HIGH_VER) $(ITM_LIBRARY)

$(ITM_LIBRARY): $(ITM_C_OBJS)
	gcc $(SO_LFLAGS) -Wl,-soname,$(ITM_LIBRARY) -o $(ITM_LIBRARY) $(ITM_C_OBJS)

$(ITP_LIBRARY): $(ITP_C_OBJS)
	gcc $(SO_LFLAGS) -Wl,-soname,$(ITP_LIBRARY) -o $(ITP_LIBRARY) $(ITP_C_OBJS)

$(IPM_LIBRARY): $(IPM_C_OBJS)
	gcc $(SO_LFLAGS) -Wl,-soname,$(IPM_LIBRARY) -o $(IPM_LIBRARY) $(IPM_C_OBJS)

$(IPP_LIBRARY): $(IPP_C_OBJS)
	gcc $(SO_LFLAGS) -Wl,-soname,$(IPP_LIBRARY) -o $(IPP_LIBRARY) $(IPP_C_OBJS)

$(IPSOCKET_LIBRARY): $(IPSOCKET_C_OBJS)
	gcc $(SO_LFLAGS) -Wl,-soname,$(IPSOCKET_LIBRARY) -o $(IPSOCKET_LIBRARY) $(IPSOCKET_C_OBJS)

$(TCP_SOCKET_LIBRARY): $(TCP_SOCKET_C_OBJS)
	gcc $(SO_LFLAGS) -Wl,-soname,$(TCP_SOCKET_LIBRARY) -o $(TCP_SOCKET_LIBRARY) $(TCP_SOCKET_C_OBJS)

$(ONE_SIDED_TCP_SOCKET_LIBRARY): $(ONE_SIDED_TCP_SOCKET_C_OBJS)
	gcc $(SO_LFLAGS) -Wl,-soname,$(ONE_SIDED_TCP_SOCKET_LIBRARY) -o $(ONE_SIDED_TCP_SOCKET_LIBRARY) $(ONE_SIDED_TCP_SOCKET_C_OBJS)

$(UNICAST_SEND_SOCKET_LIBRARY): $(UNICAST_SEND_SOCKET_C_OBJS)
	gcc $(SO_LFLAGS) -Wl,-soname,$(UNICAST_SEND_SOCKET_LIBRARY) -o $(UNICAST_SEND_SOCKET_LIBRARY) $(UNICAST_SEND_SOCKET_C_OBJS)

$(UNICAST_RECV_SOCKET_LIBRARY): $(UNICAST_RECV_SOCKET_C_OBJS)
	gcc $(SO_LFLAGS) -Wl,-soname,$(UNICAST_RECV_SOCKET_LIBRARY) -o $(UNICAST_RECV_SOCKET_LIBRARY) $(UNICAST_RECV_SOCKET_C_OBJS)

$(MULTICAST_SEND_SOCKET_LIBRARY): $(MULTICAST_SEND_SOCKET_C_OBJS)
	gcc $(SO_LFLAGS) -Wl,-soname,$(MULTICAST_SEND_SOCKET_LIBRARY) -o $(MULTICAST_SEND_SOCKET_LIBRARY) $(MULTICAST_SEND_SOCKET_C_OBJS)

$(MULTICAST_RECV_SOCKET_LIBRARY): $(MULTICAST_RECV_SOCKET_C_OBJS)
	gcc $(SO_LFLAGS) -Wl,-soname,$(MULTICAST_RECV_SOCKET_LIBRARY) -o $(MULTICAST_RECV_SOCKET_LIBRARY) $(MULTICAST_RECV_SOCKET_C_OBJS)

ifeq ($(WITH_RDMA),Yes)
$(RDMA_LIBRARY): $(RDMA_C_OBJS)
	gcc $(SO_LFLAGS) -Wl,-soname,$(RDMA_LIBRARY) -o $(RDMA_LIBRARY) $(RDMA_C_OBJS)

$(UNICAST_SEND_RDMA_LIBRARY): $(UNICAST_SEND_RDMA_C_OBJS)
	gcc $(SO_LFLAGS) -Wl,-soname,$(UNICAST_SEND_RDMA_LIBRARY) -o $(UNICAST_SEND_RDMA_LIBRARY) $(UNICAST_SEND_RDMA_C_OBJS)

$(UNICAST_RECV_RDMA_LIBRARY): $(UNICAST_RECV_RDMA_C_OBJS)
	gcc $(SO_LFLAGS) -Wl,-soname,$(UNICAST_RECV_RDMA_LIBRARY) -o $(UNICAST_RECV_RDMA_LIBRARY) $(UNICAST_RECV_RDMA_C_OBJS)

$(MULTICAST_SEND_RDMA_LIBRARY): $(MULTICAST_SEND_RDMA_C_OBJS)
	gcc $(SO_LFLAGS) -Wl,-soname,$(MULTICAST_SEND_RDMA_LIBRARY) -o $(MULTICAST_SEND_RDMA_LIBRARY) $(MULTICAST_SEND_RDMA_C_OBJS)

$(MULTICAST_RECV_RDMA_LIBRARY): $(MULTICAST_RECV_RDMA_C_OBJS)
	gcc $(SO_LFLAGS) -Wl,-soname,$(MULTICAST_RECV_RDMA_LIBRARY) -o $(MULTICAST_RECV_RDMA_LIBRARY) $(MULTICAST_RECV_RDMA_C_OBJS)
endif
