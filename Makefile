CC = g++
CFLAGS=-g -Wall -Wextra -O -std=c++17 -pthread
LIBS=-lldap -llber

all: ./bin/twmailer-server ./bin/twmailer-client 

./obj/twmailer-client.o: twmailer-client.cpp
	@ mkdir -p obj
	${CC} ${CFLAGS} -o obj/twmailer-client.o twmailer-client.cpp -c

./obj/twmailer-server.o: twmailer-server.cpp
	@ mkdir -p obj
	${CC} ${CFLAGS} -o obj/twmailer-server.o twmailer-server.cpp -c

./obj/mypw.o: ./ldapAuthSrc/mypw.c
	${CC} ${CFLAGS} -o obj/mypw.o ./ldapAuthSrc/mypw.c -c

./obj/ldapAuth.o: ./ldapAuthSrc/ldapAuth.cpp
	${CC} ${CFLAGS} -o ./obj/ldapAuth.o ./ldapAuthSrc/ldapAuth.cpp -c

./bin/twmailer-server: ./obj/twmailer-server.o ./obj/ldapAuth.o 
	@ mkdir -p bin
	${CC} ${CFLAGS} -o bin/twmailer-server ./obj/ldapAuth.o obj/twmailer-server.o ${LIBS}

./bin/twmailer-client: ./obj/twmailer-client.o ./obj/mypw.o
	@ mkdir -p bin
	${CC} ${CFLAGS} -o bin/twmailer-client obj/mypw.o obj/twmailer-client.o

clean:
	rm -r -f bin obj
