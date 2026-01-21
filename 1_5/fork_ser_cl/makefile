CC = gcc
CFLAGS = -Wall
PTHREAD = -lpthread

all: server client_multithread

server: server.c
	$(CC) $(CFLAGS) -o server server.c $(PTHREAD)

client_multithread: client_multithread.c
	$(CC) $(CFLAGS) -o client_multithread client_multithread.c $(PTHREAD)

clean:
	rm -f server client_multithread

.PHONY: all clean