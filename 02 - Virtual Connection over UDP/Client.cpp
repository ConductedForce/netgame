/*
	Client using Virtual Connection over UDP
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

const int ServerPort = 30000;
const int ClientPort = 30001;
const int ProtocolId = 0x99887766;
const float DeltaTime = 0.25f;
const float SendRate = 0.25f;
const float TimeOut = 10.0f;

int main( int argc, char * argv[] )
{
	if ( !InitializeSockets() )
	{
		printf( "failed to initialize sockets\n" );
		return 1;
	}

	Connection connection( ProtocolId, TimeOut );
	
	if ( !connection.Start( ClientPort ) )
	{
		printf( "could not start connection on port %d\n", ClientPort );
		return 1;
	}
	
	connection.Connect( Address(127,0,0,1,ServerPort ) );
	
	bool connected = false;
	
	while ( true )
	{
		if ( !connected && connection.IsConnected() )
		{
			printf( "client connected to server\n" );
			connected = true;
		}
		
		if ( !connected && connection.ConnectFailed() )
		{
			printf( "connection failed\n" );
			break;
		}
		
		unsigned char packet[] = "client to server";
		connection.SendPacket( packet, sizeof( packet ) );
		
		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = connection.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
			printf( "received packet from server\n" );
		}
		
		connection.Update( DeltaTime );

		wait_seconds( DeltaTime );
	}
	
	ShutdownSockets();

	return 0;
}
