Description:
------------
  This example is designed to show how deterministic a one way transfer (half
  of a round trip) is by reporting a histogram of results.

  This example can be run either as multi-threaded (where the threads are explicitly
  created) or as multi-process (here the executable must be run from two command
  shells).

  There are many supported command line arguments to tune the testing. To see
  all the supported options, just run the executable without any arguments.


Build:
------
  Mac and Linux:
    Terminal:
      > export TAKYON_LIBS=<folder>  // e.g. $HOME/Takyon/API/builds/linux
      > make
      > make USE_STATIC_LIB=Yes      // Use static Takyon lib to avoid dynamic Takyon libs
  Windows:
    DOS Shell:
      > set TAKYON_LIBS=<folder>     // e.g. c:\takyon\API\build\windows
      > nmake -f windows.Makefile


Run:
----
  Mac and Linux:
    To see all the usage options:
      > ./determinism

    Multi threaded (test variations that include both endpoints):
      > ./determinism "InterThreadMemcpy -ID=1" -mt
      > ./determinism "InterThreadMemcpy -ID=1" -mt -poll
      > ./determinism "InterThreadPointer -ID=1" -mt
      > ./determinism "InterThreadPointer -ID=1" -mt -poll

    Multi process:
      
      Terminal 1: > ./determinism "InterProcessMemcpy -ID=1" -endpointA
      Terminal 2: > ./determinism "InterProcessMemcpy -ID=1"

      Terminal 1: > ./determinism "InterProcessMemcpy -ID=1" -endpointA -poll
      Terminal 2: > ./determinism "InterProcessMemcpy -ID=1" -poll

      Terminal 1: > ./determinism "InterProcessPointer -ID=1" -endpointA
      Terminal 2: > ./determinism "InterProcessPointer -ID=1"

      Terminal 1: > ./determinism "InterProcessPointer -ID=1" -endpointA -poll
      Terminal 2: > ./determinism "InterProcessPointer -ID=1" -poll

      Terminal 1: > ./determinism "InterProcessSocket -ID=1" -endpointA
      Terminal 2: > ./determinism "InterProcessSocket -ID=1"

      Terminal 1: > ./determinism "InterProcessSocket -ID=1" -endpointA -poll
      Terminal 2: > ./determinism "InterProcessSocket -ID=1" -poll

      Terminal 1: > ./determinism "Socket -client -IP=127.0.0.1 -port=12345" -endpointA
      Terminal 2: > ./determinism "Socket -server -IP=127.0.0.1 -port=12345 -reuse"

      Terminal 1: > ./determinism "Socket -client -IP=127.0.0.1 -ID=1" -endpointA -poll
      Terminal 2: > ./determinism "Socket -server -IP=Any -ID=1" -poll

  Windows:
    Follow the same as above, but replace "./determinism" with "determinism"
