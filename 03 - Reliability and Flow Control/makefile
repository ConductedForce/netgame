# makefile for macosx

flags = -Wall -g -DDEBUG # -O3

% : %.cpp Net.h
	g++ $< -o $@ ${flags}

all : Example Test

test : Test
	./Test
	
server : Example
	./Example

client : Example
	./Example 127.0.0.1
	
clean:
	rm -f Test Example
	
