CC = g++
CFLAGS=-g -Wall -Wextra -O -std=c++17 -pthread

all: ./bin/twmailer-server ./bin/twmailer-client

./obj/twmailer-client.o: twmailer-client.cpp
	${CC} ${CFLAGS} -o obj/twmailer-client.o twmailer-client.cpp -c

./obj/twmailer-server.o: twmailer-server.cpp
	${CC} ${CFLAGS} -o obj/twmailer-server.o twmailer-server.cpp -c 

./bin/twmailer-server: ./obj/twmailer-server.o
	${CC} ${CFLAGS} -o bin/twmailer-server obj/twmailer-server.o

./bin/twmailer-client: ./obj/twmailer-client.o
	${CC} ${CFLAGS} -o bin/twmailer-client obj/twmailer-client.o

clean:
	rm -f bin/* obj/*
