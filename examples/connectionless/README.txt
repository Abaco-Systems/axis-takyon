Description:
------------
  Shows how to use Takyon with an unconneted communication path. This allows for
  unreliable transfers; i.e. a message that is sent may never reach the destination.
  The receiver needs to allow for 'dropped' data. This is good with various types
  of data transfers where data integrity is not critical, for example with with
  audio or video streams where missing blocks of data do not really affect the
  application.

  Each endpoint is independent of each other; i.e. you can use control to stop one
  endpoint and then restart it, and the other endpoint will be uneffected.

  This application will support both unconnected point to point and multicast
  where one sender simultaneously sends to multiple destinations.


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
    To see the usage options:
      > ./connectionless

    Unicast experiments:
      Terminal 1 (endpoint A test variations, to match with endpoint B):
        > ./connectionless "SocketDatagram -unicastSend -remoteIP 127.0.0.1 -port 12345" -endpointA
      Terminal 2 (endpoint B test variations, to match with endpoint A):
        > ./connectionless "SocketDatagram -unicastRecv -localIP 127.0.0.1 -port 12345"

    Multicast experiments:
      Terminal 1 (endpoint A test variations, to match with endpoint B):
        > ./connectionless "SocketDatagram -multicastSend -localIP 127.0.0.1 -group 239.1.2.3 -port 12345" -endpointA
      Terminal 2 (endpoint B test variations, to match with endpoint A):
        > ./connectionless "SocketDatagram -multicastRecv -localIP 127.0.0.1 -group 239.1.2.3 -port 12345 -reuse"

  Windows:
    Follow the same as above, but replace "./connectionless" with "connectionless"
