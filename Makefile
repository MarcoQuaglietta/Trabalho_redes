CC = gcc

CFLAGS = -Wall -Wextra -g -std=c99

all: navegador servidor

navegador: cliente.c
	$(CC) $(CFLAGS) -o navegador cliente.c

server: servidor.c
	$(CC) $(CFLAGS) -o server servidor.c

clean:
	rm -f navegador server