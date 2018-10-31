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
      > make
      > make USE_STATIC_LIB=Yes      // Use static Takyon lib to avoid dynamic Takyon libs
  Windows:
    DOS Shell:
      > nmake -f windows.Makefile


Run:
----
  Mac and Linux:
    To see all the usage options:
      > ./determinism

    Multi threaded (test variations that include both endpoints):
      > ./determinism "Memcpy -ID 1" -mt
      > ./determinism "Memcpy -ID 1" -mt -poll
      > ./determinism "Memcpy -ID 1 -share" -mt
      > ./determinism "Memcpy -ID 1 -share" -mt -poll

    Multi process:
      Terminal 1 (endpoint A test variations, to match with endpoint B):
        > ./determinism "Mmap -ID 1" -endpointA
        > ./determinism "Mmap -ID 1" -endpointA -poll
        > ./determinism "Mmap -ID 1 -share" -endpointA
        > ./determinism "Mmap -ID 1 -share" -endpointA -poll
        > ./determinism "Socket -local -ID 1" -endpointA
        > ./determinism "Socket -local -ID 1" -endpointA -poll
        > ./determinism "Socket -client 127.0.0.1 -port 12345" -endpointA
        > ./determinism "Socket -client 127.0.0.1 -port 12345" -endpointA -poll
      Terminal 2 (endpoint B test variations, to match with endpoint A):
        > ./determinism "Mmap -ID 1"
        > ./determinism "Mmap -ID 1" -poll
        > ./determinism "Mmap -ID 1 -share"
        > ./determinism "Mmap -ID 1 -share" -poll
        > ./determinism "Socket -local -ID 1"
        > ./determinism "Socket -local -ID 1" -poll
        > ./determinism "Socket -server 127.0.0.1 -port 12345"
        > ./determinism "Socket -server Any -port 12345" -poll

  Windows:
    Follow the same as above, but replace "./determinism" with "determinism"
