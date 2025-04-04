Game Server

This project is a multiplayer game server written in C. It manages player connections, message queues, and interactions using socket programming.

Features

Handles multiple players simultaneously.

Uses non-blocking sockets with select() for efficient I/O handling.

Manages a queue of waiting players.

Implements a structured message queue system.

Installation and Setup

Prerequisites

A C compiler (e.g., gcc)

make (if a Makefile is present)

A Unix-based OS (Linux/macOS) recommended

Compilation

To compile the server, run:

gcc -o gameServer gameServer.c

Running the Server

To start the server, use:

./gameServer

Connecting Clients

To play the game, open two or more terminal windows and run the game. The terminals will connect automatically to the server, allowing multiple players to join and interact.

Ensure that the appropriate port and network configurations are set within gameServer.c if needed.

Contributing

Feel free to fork this repository and submit pull requests for improvements.
