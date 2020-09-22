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


TARGET       = barrier.exe
C_OBJS       = main.obj barrier.obj takyon_graph.obj takyon_attributes.obj takyon_collective.obj takyon_mmap.obj
#PTHREADS_INCLUDE = -I../../API/inc/windows
PTHREADS_INCLUDE = -Ic:/pthreads4w/install/include
CFLAGS       = -O2 -MD -W3 -WX -nologo -Zm200 -Zc:wchar_t- -D_CRT_SECURE_NO_WARNINGS=1 -I../../API/inc $(PTHREADS_INCLUDE) -I../../extensions
LDFLAGS      = /NOLOGO /INCREMENTAL:NO /MANIFEST:embed /SUBSYSTEM:console
PTHREADS_LIB = c:/pthreads4w/install/lib/libpthreadVC3.lib /NODEFAULTLIB:LIBCMT.LIB
#PTHREADS_LIB = c:/pthreads4w/install/lib/libpthreadVC3d.lib /NODEFAULTLIB:LIBCMT.LIB
LIBS         = ../../API/builds/windows/TakyonStatic.lib Ws2_32.lib $(PTHREADS_LIB)

.SUFFIXES: .c

all: $(TARGET)

{.\}.c{}.obj::
	cl -c $(CFLAGS) -Fo $<

{..\..\extensions\}.c{}.obj::
	cl -c $(CFLAGS) -Fo $<

$(TARGET): $(C_OBJS)
	link $(LDFLAGS) /OUT:$(TARGET) $(C_OBJS) $(LIBS)

clean:
	-del *~ *.obj *.pdb $(TARGET)
