Description:
------------
  This example is designed to show how the application can detect a timeout or failure
  and re-establish the path in order to get back to succesful transfers.

  This example can be run either as multi-threaded (where the threads are explicitly
  created) or as multi-process (where the executable must be run from two command
  shells).

  To force a realistic disconnect, either disconnect a communication cable (e.g. Ethernet)
  or reset a network switch. You can also use control-C on either of the endpoints.
  There's also a command line option to turn on random delays based on the timeout
  period (also settable on the command line). These random delays will periodically force
  a timeout, causing the path to get re-established. If run as multi-threaded, then
  manual diconnects are not really achievable.

  There are various supported command-line arguments to tune the timeouts and error
  reporting. To see all the supported options, just run the executable without any arguments.


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
      > ./haa

    Multi threaded (test variations that include both endpoints):
      > ./haa "Memcpy -ID 1" -mt -simulateDelays
      > ./haa "Memcpy -ID 1" -mt -simulateDelays -poll
      > ./haa "Memcpy -ID 1 -share" -mt -simulateDelays
      > ./haa "Memcpy -ID 1 -share" -mt -simulateDelays -poll

    Multi process:
      Terminal 1 (endpoint A test variations, to match with endpoint B):
        > ./haa "Mmap -ID 1" -errors -endpointA
        > ./haa "Mmap -ID 1" -errors -endpointA -poll
        > ./haa "Mmap -ID 1 -share" -errors -endpointA
        > ./haa "Mmap -ID 1 -share" -errors -endpointA -poll
        > ./haa "Socket -local -ID 1" -errors -endpointA
        > ./haa "Socket -local -ID 1" -errors-endpointA -poll
        > ./haa "Socket -remoteIP 127.0.0.1 -port 12345" -errors -endpointA
        > ./haa "Socket -remoteIP 127.0.0.1 -port 12345" -errors -endpointA -poll
      Terminal 2 (endpoint B test variations, to match with endpoint A):
        > ./haa "Mmap -ID 1" -errors
        > ./haa "Mmap -ID 1" -errors -poll
        > ./haa "Mmap -ID 1 -share" -errors
        > ./haa "Mmap -ID 1 -share" -errors -poll
        > ./haa "Socket -local -ID 1" -errors
        > ./haa "Socket -local -ID 1" -errors -poll
        > ./haa "Socket -localIP 127.0.0.1 -port 12345" -errors
        > ./haa "Socket -localIP Any -port 12345" -errors -poll

  Windows:
    Follow the same as above, but replace "./haa" with "haa"
