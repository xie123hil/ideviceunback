CC=gcc
PRF=-O2
PROFILE=
CFLAGS= -Wall -Werror -g -I.
OBJS= sha1.o
LIBS=

all: ideviceunback

.c.o:
	$(CC) $(CFLAGS) -c $*.c

ideviceunback: $(OBJS) ideviceunback.c
	gcc $(CFLAGS) ideviceunback.c $(OBJS) -o ideviceunback 

default: ideviceunback

clean:
	rm ideviceunback *.o

install: ideviceunback
	cp ideviceunback /usr/local/bin
