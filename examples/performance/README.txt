Description:
------------
  This example is designed to show performance (latency and throughput) of any
  supported interconnect.

  This example can be run either as multi-threaded (where the threads are explicitly
  created) or as multi-process (here the executable must be run from two command
  shells).

  There are many supported command line arguments to tune the performance testing
  as needed. To see all the supported options, just run the executable without
  any arguments.


Build:
------
  Mac and Linux:
    Terminal:
      > make
  Windows:
    DOS Shell:
      > nmake -f windows.Makefile


Run:
----
  Mac and Linux:
    To see all the usage options:
      > ./performance

    Multi threaded (test variations that include both endpoints):
      > ./performance "Memcpy -ID 1" -mt
      > ./performance "Memcpy -ID 1" -mt -poll
      > ./performance "Memcpy -ID 1 -share" -mt
      > ./performance "Memcpy -ID 1 -share" -mt -poll

    Multi process:
      Terminal 1 (endpoint A test variations, to match with endpoint B):
        > ./performance "Mmap -ID 1" -endpointA
        > ./performance "Mmap -ID 1" -endpointA -poll
        > ./performance "Mmap -ID 1 -share" -endpointA
        > ./performance "Mmap -ID 1 -share" -endpointA -poll
        > ./performance "Socket -local -ID 1" -endpointA
        > ./performance "Socket -local -ID 1" -endpointA -poll
        > ./performance "Socket -client 127.0.0.1 -port 12345" -endpointA
        > ./performance "Socket -client 127.0.0.1 -port 12345" -endpointA -poll
      Terminal 2 (endpoint B test variations, to match with endpoint A):
        > ./performance "Mmap -ID 1"
        > ./performance "Mmap -ID 1" -poll
        > ./performance "Mmap -ID 1 -share"
        > ./performance "Mmap -ID 1 -share" -poll
        > ./performance "Socket -local -ID 1 -reuse"
        > ./performance "Socket -local -ID 1 -reuse" -poll
        > ./performance "Socket -server 127.0.0.1 -port 12345 -reuse"
        > ./performance "Socket -server Any -port 12345 -reuse" -poll

  Windows:
    Follow the same as above, but replace "./performance" with "performance"
