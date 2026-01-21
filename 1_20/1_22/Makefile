CC = gcc
CFLAGS = -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
LDFLAGS = 

# Server files
SERVER_SRCS = server_main.c run_server.c create_server_socket.c accept_client.c \
              fork_and_exec_worker.c shutdown.c signal.c log.c \
              resource_monitor.c
SERVER_OBJS = $(SERVER_SRCS:.c=.o)
SERVER_TARGET = server

# Worker files
WORKER_SRCS = worker_main.c child_process.c resource_monitor.c log.c signal.c
WORKER_OBJS = $(WORKER_SRCS:.c=.o)
WORKER_TARGET = worker

# Client files
CLIENT_SRCS = client_main.c client_connect.c client_function.c
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)
CLIENT_TARGET = client

.PHONY: all clean

all: $(SERVER_TARGET) $(WORKER_TARGET) $(CLIENT_TARGET)

$(SERVER_TARGET): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(WORKER_TARGET): $(WORKER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(CLIENT_TARGET): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c server_function.h
	$(CC) $(CFLAGS) -c $< -o $@

client_%.o: client_%.c client_function.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(SERVER_OBJS) $(WORKER_OBJS) $(CLIENT_OBJS)
	rm -f $(SERVER_TARGET) $(WORKER_TARGET) $(CLIENT_TARGET)
	rm -f server.log
