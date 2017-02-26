# makefile for macosx

flags = -Wall -g -DDEBUG # -O3

% : %.cpp Net.h
	g++ $< -o $@ ${flags}

all : Test Server Client Node

test : Test
	./Test
	
server : Server
	./Server

client : Client
	./Client
	
node : Node
	./Node

clean:
	rm -f Test Server Client Node
