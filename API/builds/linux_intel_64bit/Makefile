# Copyright 2018 Abaco Systems
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
TAKYON_C_FILES += utils_arg_parser.c
TAKYON_C_FILES += utils_endian.c
TAKYON_C_FILES += utils_memory_linux.c
TAKYON_C_FILES += utils_mmap_linux.c
TAKYON_C_FILES += utils_shared_libraries.c
TAKYON_C_FILES += utils_socket_linux.c
TAKYON_C_FILES += utils_threads.c
TAKYON_C_FILES += utils_time_linux.c
TAKYON_C_FILES += takyon.c

TAKYON_H_FILES :=
TAKYON_H_FILES += ../../inc/takyon.h
TAKYON_H_FILES += ../../inc/takyon_private.h

CFLAGS         := -std=gnu99 -Wall -Werror -pthread -I../../inc
#CFLAGS         += -g -O0
CFLAGS         += -O2
SO_CFLAGS      := $(CFLAGS) -fPIC
SO_LFLAGS      := -shared
TAKYON_C_OBJS  := $(patsubst %.c,%.o,$(TAKYON_C_FILES))
TAKYON_LIBRARY := libTakyon.a

MEMCPY_C_FILES  := takyon_memcpy.c
MEMCPY_C_OBJS   := $(patsubst %.c,%.o,$(MEMCPY_C_FILES))
MEMCPY_LIBRARY  := libTakyonMemcpy.so

MMAP_C_FILES  := takyon_mmap.c
MMAP_C_OBJS   := $(patsubst %.c,%.o,$(MMAP_C_FILES))
MMAP_LIBRARY  := libTakyonMmap.so

SOCKET_C_FILES  := takyon_socket.c
SOCKET_C_OBJS   := $(patsubst %.c,%.o,$(SOCKET_C_FILES))
SOCKET_LIBRARY  := libTakyonSocket.so

SOCKET_DATAGRAM_C_FILES  := takyon_socket_datagram.c
SOCKET_DATAGRAM_C_OBJS   := $(patsubst %.c,%.o,$(SOCKET_DATAGRAM_C_FILES))
SOCKET_DATAGRAM_LIBRARY  := libTakyonSocketDatagram.so

STATIC_LIB_C_FILES := $(TAKYON_C_FILES) $(MEMCPY_C_FILES) $(MMAP_C_FILES) $(SOCKET_C_FILES) $(SOCKET_DATAGRAM_C_FILES)
STATIC_LIB_C_OBJS  := $(patsubst %.c,static_%.o,$(STATIC_LIB_C_FILES))
STATIC_LIBRARY     := libTakyonStatic.a

all: static_lib dynamic_lib
static_lib: $(STATIC_LIBRARY)
dynamic_lib: $(TAKYON_LIBRARY) $(MEMCPY_LIBRARY) $(MMAP_LIBRARY) $(SOCKET_LIBRARY) $(SOCKET_DATAGRAM_LIBRARY)

clean:
	rm -f *.o
	rm -f *~
	rm -f $(STATIC_LIBRARY)
	rm -f $(TAKYON_LIBRARY)
	rm -f $(MEMCPY_LIBRARY)
	rm -f $(MMAP_LIBRARY)
	rm -f $(SOCKET_LIBRARY)
	rm -f $(SOCKET_DATAGRAM_LIBRARY)

$(STATIC_LIB_C_OBJS): static_%.o: %.c $(TAKYON_H_FILES)
	gcc -DBUILD_STATIC_LIB $(CFLAGS) -c $< -o $@

$(TAKYON_C_OBJS): %.o: %.c $(TAKYON_H_FILES)
	gcc $(CFLAGS) -c $< -o $@

$(MEMCPY_C_OBJS) $(MMAP_C_OBJS) $(SOCKET_C_OBJS) $(SOCKET_DATAGRAM_C_OBJS): %.o: %.c $(TAKYON_H_FILES)
	gcc $(SO_CFLAGS) -c $< -o $@

$(TAKYON_LIBRARY): $(TAKYON_C_OBJS)
	ar rcs $@ $^

$(STATIC_LIBRARY): $(STATIC_LIB_C_OBJS)
	ar rcs $@ $^

# NOTE: Just in case shared object versioning is needed:
#MEMCPY_HIGH_VER := 1
#MEMCPY_LOW_VER  := 0
#$(MEMCPY_LIBRARY).$(MEMCPY_HIGH_VER).$(MEMCPY_LOW_VER): $(MEMCPY_C_OBJS)
#	gcc $(SO_LFLAGS) -Wl,-soname,$(MEMCPY_LIBRARY).$(MEMCPY_HIGH_VER) -o $(MEMCPY_LIBRARY).$(MEMCPY_HIGH_VER).$(MEMCPY_LOW_VER) $(MEMCPY_C_OBJS)
#	ln -sf $(MEMCPY_LIBRARY).$(MEMCPY_HIGH_VER).$(MEMCPY_LOW_VER) $(MEMCPY_LIBRARY).$(MEMCPY_HIGH_VER)
#	ln -sf $(MEMCPY_LIBRARY).$(MEMCPY_HIGH_VER) $(MEMCPY_LIBRARY)

$(MEMCPY_LIBRARY): $(MEMCPY_C_OBJS)
	gcc $(SO_LFLAGS) -Wl,-soname,$(MEMCPY_LIBRARY) -o $(MEMCPY_LIBRARY) $(MEMCPY_C_OBJS)

$(MMAP_LIBRARY): $(MMAP_C_OBJS)
	gcc $(SO_LFLAGS) -Wl,-soname,$(MMAP_LIBRARY) -o $(MMAP_LIBRARY) $(MMAP_C_OBJS)

$(SOCKET_LIBRARY): $(SOCKET_C_OBJS)
	gcc $(SO_LFLAGS) -Wl,-soname,$(SOCKET_LIBRARY) -o $(SOCKET_LIBRARY) $(SOCKET_C_OBJS)

$(SOCKET_DATAGRAM_LIBRARY): $(SOCKET_DATAGRAM_C_OBJS)
	gcc $(SO_LFLAGS) -Wl,-soname,$(SOCKET_DATAGRAM_LIBRARY) -o $(SOCKET_DATAGRAM_LIBRARY) $(SOCKET_DATAGRAM_C_OBJS)
