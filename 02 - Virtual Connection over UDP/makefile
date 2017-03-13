# makefile for macosx

flags = -Wall -g -DDEBUG # -O3

% : %.cpp Net.h
	g++ $< -o $@ ${flags}

all : Client Server Test

test : Test
	./Test
	
server : Server
	./Server

client : Client
	./Client

clean:
	rm -f Client Server Test
