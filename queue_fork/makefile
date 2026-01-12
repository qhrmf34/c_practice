# ═══════════════════════════════════════════════════════════
# Makefile
# ═══════════════════════════════════════════════════════════

CC = gcc
CFLAGS = -Wall -Wextra -pthread -g
TARGETS = main producer consumer

all: $(TARGETS)

# queue 라이브러리
queue.o: queue.c queue.h
	$(CC) $(CFLAGS) -c queue.c

# 메인 프로그램
main: main.c queue.o
	$(CC) $(CFLAGS) -o main main.c queue.o

# 생산자
producer: producer.c queue.o
	$(CC) $(CFLAGS) -o producer producer.c queue.o

# 소비자
consumer: consumer.c queue.o
	$(CC) $(CFLAGS) -o consumer consumer.c queue.o

# 정리
clean:
	rm -f $(TARGETS) *.o
	ipcrm -a  # 공유 메모리 정리

# 실행
run: all
	./main

.PHONY: all clean run