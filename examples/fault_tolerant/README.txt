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
      > make USE_STATIC_LIB=Yes      // Use static Takyon lib to avoid dynamic Takyon libs
  Windows:
    DOS Shell:
      > nmake -f windows.Makefile


Run:
----
  Mac and Linux:
    To see all the usage options:
      > ./fault_tolerant

    Multi threaded (test variations that include both endpoints):
      > ./fault_tolerant "InterThreadMemcpy -ID=1" -mt -simulateDelays
      > ./fault_tolerant "InterThreadMemcpy -ID=1" -mt -simulateDelays -poll
      > ./fault_tolerant "InterThreadPointer -ID=1" -mt -simulateDelays
      > ./fault_tolerant "InterThreadPointer -ID=1" -mt -simulateDelays -poll

    Multi process:
      Terminal1: > ./fault_tolerant "InterProcessMemcpy -ID=1" -errors -endpointA
      Terminal2: > ./fault_tolerant "InterProcessMemcpy -ID=1" -errors

      Terminal1: > ./fault_tolerant "InterProcessMemcpy -ID=1" -errors -endpointA -poll
      Terminal2: > ./fault_tolerant "InterProcessMemcpy -ID=1" -errors -poll

      Terminal1: > ./fault_tolerant "InterProcessPointer -ID=1" -errors -endpointA
      Terminal2: > ./fault_tolerant "InterProcessPointer -ID=1" -errors

      Terminal1: > ./fault_tolerant "InterProcessPointer -ID=1" -errors -endpointA -poll
      Terminal2: > ./fault_tolerant "InterProcessPointer -ID=1" -errors -poll

      Terminal1: > ./fault_tolerant "InterProcessSocket -ID=1" -errors -endpointA
      Terminal2: > ./fault_tolerant "InterProcessSocket -ID=1 -reuse" -errors

      Terminal1: > ./fault_tolerant "InterProcessSocket -ID=1" -errors -endpointA -poll
      Terminal2: > ./fault_tolerant "InterProcessSocket -ID=1 -reuse" -errors -poll

      Terminal1: > ./fault_tolerant "Socket -client -IP=127.0.0.1 -port=12345" -errors -endpointA
      Terminal2: > ./fault_tolerant "Socket -server -IP=127.0.0.1 -port=12345 -reuse" -errors

      Terminal1: > ./fault_tolerant "Socket -client -IP=127.0.0.1 -ID=1" -errors -endpointA -poll
      Terminal2: > ./fault_tolerant "Socket -server -IP=Any -ID=1" -errors -poll

  Windows:
    Follow the same as above, but replace "./fault_tolerant" with "fault_tolerant"
