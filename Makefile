CC = g++
CFLAGS = -Wall -g -std=c++17

all: mailer

mailer: mailer.cpp
	$(CC) $(CFLAGS) -o mailer mailer.cpp

clean:
	rm -f mailer

