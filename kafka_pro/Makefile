CC = gcc
CFLAGS = -Wall -Wextra -pthread -g
LDFLAGS = -lrdkafka -lpthread
TARGETS = main_kafka producer_kafka consumer_kafka

all: $(TARGETS)

queue.o: queue.c queue.h
	$(CC) $(CFLAGS) -c queue.c

kafka_wrapper.o: kafka_wrapper.c kafka_wrapper.h
	$(CC) $(CFLAGS) -c kafka_wrapper.c

main_kafka: main_kafka.c queue.o
	$(CC) $(CFLAGS) -o main_kafka main_kafka.c queue.o $(LDFLAGS)

producer_kafka: producer_kafka.c queue.o kafka_wrapper.o
	$(CC) $(CFLAGS) -o producer_kafka producer_kafka.c queue.o kafka_wrapper.o $(LDFLAGS)

consumer_kafka: consumer_kafka.c queue.o kafka_wrapper.o
	$(CC) $(CFLAGS) -o consumer_kafka consumer_kafka.c queue.o kafka_wrapper.o $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.o
	ipcrm -a

run: all
	./main_kafka

.PHONY: all clean run
