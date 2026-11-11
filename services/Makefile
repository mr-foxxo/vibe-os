# Makefile – user-space services
CC      = gcc
CFLAGS  = -O2 -Wall -pipe -I../headers/include
LDFLAGS = -static   # makes booting simpler

all: audio-service net-service xserver deviced

audio-service: audio-service.c mailbox.c
	${CC} ${CFLAGS} -o $@ $^ ${LDFLAGS}

net-service: net-service.c mailbox.c
	${CC} ${CFLAGS} -o $@ $^ ${LDFLAGS}

xserver: xserver.c
	${CC} ${CFLAGS} -o $@ $^ ${LDFLAGS}

deviced: deviced.c mailbox.c
	${CC} ${CFLAGS} -o $@ $^ ${LDFLAGS}

clean:
	rm -f audio-service net-service xserver deviced

install: all
	install -m 755 audio-service /sbin/audio-service
	install -m 755 net-service   /sbin/net-service
	install -m 755 xserver       /sbin/xserver
	install -m 755 deviced       /sbin/deviced