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

int main( int argc, char * argv[] )
{
	// initialize socket layer

	if ( !InitializeSockets() )
	{
		printf( "failed to initialize sockets\n" );
		return 1;
	}
	
	// create socket

	int port = 30000;

	printf( "creating socket on port %d\n", port );

	Socket socket;

	if ( !socket.Open( port ) )
	{
		printf( "failed to create socket!\n" );
		return 1;
	}

	// send and receive packets to ourself until the user ctrl-breaks...

	while ( true )
	{
		const char data[] = "hello world!";

		socket.Send( Address(127,0,0,1,port), data, sizeof(data) );
			
		while ( true )
		{
			Address sender;
			unsigned char buffer[256];
			int bytes_read = socket.Receive( sender, buffer, sizeof( buffer ) );
			if ( !bytes_read )
				break;
			printf( "received packet from %d.%d.%d.%d:%d (%d bytes)\n", 
				sender.GetA(), sender.GetB(), sender.GetC(), sender.GetD(), 
				sender.GetPort(), bytes_read );
		}
		
		wait_seconds( 0.25f );
	}
	
	// shutdown socket layer
	
	ShutdownSockets();

	return 0;
}
