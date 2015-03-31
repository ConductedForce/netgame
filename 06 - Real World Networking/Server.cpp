/*
	Server Example using Transport Layer
	From "Networking for Game Programmers" - http://www.gafferongames.com/networking-for-game-programmers
	Author: Glenn Fiedler <gaffer@gaffer.org>
*/

#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#include "NetPlatform.h"
#include "NetTransport.h"

using namespace std;
using namespace net;

int main( int argc, char * argv[] )
{
	// initialize and create transport
	
	TransportType type = Transport_LAN;
	
	if ( !Transport::Initialize( Transport_LAN ) )
	{
		printf( "failed to initialize transport layer\n" );
		return 1;
	}
	
	Transport * transport = Transport::Create();
	
	if ( !transport )
	{
		printf( "could not create transport\n" );
		return 1;
	}
	
	// start server (transport specific)
	
	switch ( type )
	{
		case Transport_LAN:
		{
			TransportLAN * lan_transport = dynamic_cast<TransportLAN*>( transport );
			char hostname[64+1] = "hostname";
			TransportLAN::GetHostName( hostname, sizeof(hostname) );
			lan_transport->StartServer( hostname );
		}
		break;
		
		default:
			break;
	}

	// main loop

	const float DeltaTime = 1.0f / 30.0f;

	while ( true )
	{
		transport->Update( DeltaTime );
		wait_seconds( DeltaTime );
	}
	
	// shutdown
	
	Transport::Destroy( transport );
	
	Transport::Shutdown();

	return 0;
}
