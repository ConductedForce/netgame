/*
	Sending and Receiving Packets Example
	From "Networking for Game Programmers" - http://www.gafferongames.com/networking-for-game-programmers
	Author: Glenn Fiedler <gaffer@gaffer.org>
*/

#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#include "Net.h"

using namespace std;
using namespace net;

#ifdef DEBUG
#define check assert
#else
#define check(n) if ( !(n) ) { printf( "check failed\n" ); exit(1); }
#endif

void test_address()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test address\n" );
	printf( "-----------------------------------------------------\n" );
	
	printf( "defaults\n" );
	{
		Address address;
		check( address.GetA() == 0 );
		check( address.GetB() == 0 );
		check( address.GetC() == 0 );
		check( address.GetD() == 0 );
		check( address.GetPort() == 0 );
		check( address.GetAddress() == 0 );
	}
	
	printf( "a,b,c,d,port\n" );
	{
		const unsigned char a = 100;
		const unsigned char b = 110;
		const unsigned char c = 50;
		const unsigned char d = 12;
		const unsigned short port = 10000;
		Address address( a, b, c, d, port );
		check( a == address.GetA() );
		check( b == address.GetB() );
		check( c == address.GetC() );
		check( d == address.GetD() );
		check( port == address.GetPort() );
	}

	printf( "equality/inequality\n");
	{
		Address x(100,110,0,1,50000);	
		Address y(101,210,6,5,50002);
		check( x != y );
		check( y == y );
		check( x == x );
	}
}

void test_socket()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test socket\n" );
	printf( "-----------------------------------------------------\n" );
	
	printf( "open/close\n" );
	{
		Socket socket;
		check( !socket.IsOpen() );
		check( socket.Open( 30000 ) );
		check( socket.IsOpen() );
		socket.Close();
		check( !socket.IsOpen() );
		check( socket.Open( 30000 ) );
		check( socket.IsOpen() );
	}
	
	printf( "fails on same port\n" );
	{
		Socket a,b;
		check( a.Open( 30000 ) );
		check( !b.Open( 30000 ) );
		check( a.IsOpen() );
		check( !b.IsOpen() );
	}
	
	printf( "send and receive packets\n" );
	{
		Socket a,b;
		check( a.Open( 30000 ) );
		check( b.Open( 30001 ) );
		const char packet[] = "packet data";
		bool a_received_packet = false;
		bool b_received_packet = false;
		while ( !a_received_packet && !b_received_packet )
		{
			check( a.Send( Address(127,0,0,1,30000), packet, sizeof(packet) ) );
			check( b.Send( Address(127,0,0,1,30000), packet, sizeof(packet) ) );
			
			while ( true )
			{
				Address sender;
				char buffer[256];
				int bytes_read = a.Receive( sender, buffer, sizeof(buffer) );
				if ( bytes_read == 0 )
					break;
				if ( bytes_read == sizeof(packet) && strcmp(buffer,packet) == 0 )
					a_received_packet = true;
			}

			while ( true )
			{
				Address sender;
				char buffer[256];
				int bytes_read = b.Receive( sender, buffer, sizeof(buffer) );
				if ( bytes_read == 0 )
					break;
				if ( bytes_read == sizeof(packet) && strcmp(buffer,packet) == 0 )
					b_received_packet = true;
			}
		}
	}
}

void tests()
{
	test_address();
	test_socket();

	printf( "-----------------------------------------------------\n" );
	printf( "passed!\n" );
}

int main( int argc, char * argv[] )
{
	if ( !InitializeSockets() )
	{
		printf( "failed to initialize sockets\n" );
		return 1;
	}
	
	tests();
	
	ShutdownSockets();

	return 0;
}
