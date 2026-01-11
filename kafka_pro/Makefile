CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS = -lrdkafka

TARGETS = main_kafka producer_kafka consumer_kafka

all: $(TARGETS)

kafka_wrapper.o: kafka_wrapper.c kafka_wrapper.h
	$(CC) $(CFLAGS) -c kafka_wrapper.c

producer_kafka: producer_kafka.c kafka_wrapper.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

consumer_kafka: consumer_kafka.c kafka_wrapper.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

main_kafka: main_kafka.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f *.o $(TARGETS)

run: all
	./main_kafka
