Local Network Chat Server (TCP Sockets in C)
--------------------------------------------

A lightweight multi-client chat application built using C and TCP sockets, designed for communication within a local network (LAN).
The project includes a server capable of handling multiple connected clients simultaneously and a simple client program for sending and receiving messages.

Features
1. Multi-client chat support
2. Real-time message broadcasting
3. Server accepts unlimited clients (system-dependent)
4. Non-blocking I/O using select() (or whichever method your implementation uses)
5. Simple and fast — suitable for LAN or simulation environments

Compilation
1. open new terminal
2. gcc server.c
3. gcc client.c
4. ./server
5. open new terminal
6. gcc -o client client.c -lpthread
7. ./client <local IP address> port number

-How It Works

1. Server creates a TCP socket and listens on a configurable port.
2. Each client connects to the server via IP + port.
3. When a client sends a message, the server broadcasts it to all other clients.
4. Communication continues until a client disconnects or server shuts down.
