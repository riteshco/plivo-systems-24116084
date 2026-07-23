CC = g++
CFLAGS ?= -O2 -Wall -std=c++17 -pthread -x c++

all: sender receiver

sender: sender.c
	$(CC) $(CFLAGS) -o sender sender.c

receiver: receiver.c
	$(CC) $(CFLAGS) -o receiver receiver.c

clean:
	rm -f sender receiver
