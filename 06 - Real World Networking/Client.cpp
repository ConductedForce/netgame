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
	
	// connect to server (transport specific)
	
	switch ( type )
	{
		case Transport_LAN:
		{
			TransportLAN * lan_transport = dynamic_cast<TransportLAN*>( transport );
			if ( argc >= 2 )
				lan_transport->ConnectClient( argv[1] );
			else
			{
				char hostname[64+1];
				TransportLAN::GetHostName( hostname, sizeof(hostname) );
				lan_transport->ConnectClient( hostname );
			}
		}
		break;
		
		default:
			break;
	}

	// main loop

	const float DeltaTime = 1.0f / 30.0f;

	bool connected = false;

	while ( true )
	{
		if ( type == Transport_LAN )
		{
			TransportLAN * lan_transport = dynamic_cast<TransportLAN*>( transport );
			if ( !connected && lan_transport->IsConnected() )
				connected = true;
			if ( connected && !lan_transport->IsConnected() )
			{
				printf( "disconnected\n" );
				break;
			}
			if ( lan_transport->ConnectFailed() )
			{
				printf( "connect failed\n" );
				break;
			}
		}

		transport->Update( DeltaTime );
		
		wait_seconds( DeltaTime );
	}
	
	// shutdown
	
	Transport::Destroy( transport );
	
	Transport::Shutdown();

	return 0;
}
