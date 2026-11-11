CC = gcc
CFLAGS = -Wall -Wextra -g

all: server client

server: server.o packet.o lossy.o
	$(CC) $(CFLAGS) -o server server.o packet.o lossy.o

client: client.o packet.o lossy.o
	$(CC) $(CFLAGS) -o client client.o packet.o lossy.o

server.o: server.c packet.h
client.o: client.c packet.h
packet.o: packet.c packet.h
lossy.o: lossy.c lossy.h

clean:
	rm -f *.o server client
