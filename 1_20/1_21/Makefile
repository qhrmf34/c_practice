CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
LDFLAGS = 

SERVER_SRCS = server_main.c server_run.c server_accept.c server_worker.c \
              server_shutdown.c server_child.c server_socket.c server_log.c \
              server_monitor.c server_signal.c
WORKER_SRCS = worker_main.c server_child.c server_log.c server_monitor.c \
              server_signal.c
CLIENT_SRCS = client_main.c client_connect.c client_run.c

SERVER_OBJS = $(SERVER_SRCS:.c=.o)
WORKER_OBJS = $(WORKER_SRCS:.c=.o)
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)

all: server worker client

server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

worker: $(WORKER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c server_function.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c client_function.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o server worker client server.log

.PHONY: all clean
