CC = g++
CFLAGS=-g -Wall -Wextra -O -std=c++17 -pthread

all: ./bin/twmailer-server ./bin/twmailer-client

./obj/twmailer-client.o: twmailer-client.cpp
	@ mkdir -p obj
	${CC} ${CFLAGS} -o obj/twmailer-client.o twmailer-client.cpp -c

./obj/twmailer-server.o: twmailer-server.cpp
	@ mkdir -p obj
	${CC} ${CFLAGS} -o obj/twmailer-server.o twmailer-server.cpp -c 

./bin/twmailer-server: ./obj/twmailer-server.o
	@ mkdir -p bin
	${CC} ${CFLAGS} -o bin/twmailer-server obj/twmailer-server.o

./bin/twmailer-client: ./obj/twmailer-client.o
	@ mkdir -p bin
	${CC} ${CFLAGS} -o bin/twmailer-client obj/twmailer-client.o

clean:
	rm -r -f bin obj
