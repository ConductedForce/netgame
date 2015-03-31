/*
	Simple Network Library from "Networking for Game Programmers"
	http://www.gafferongames.com/networking-for-game-programmers
	Author: Glenn Fiedler <gaffer@gaffer.org>
*/

#include "NetTransport.h"

#include <assert.h>

static net::TransportType transportType = net::Transport_None;
static int transportCount = 0;

bool net::Transport::Initialize( TransportType type )
{
	assert( type != Transport_None );
	bool result = false;
	switch ( type )
	{
		case Transport_LAN: result = TransportLAN::Initialize(); break;
		default: break;
	}
	transportType = type;
	return result;
}

void net::Transport::Shutdown()
{
	switch ( transportType )
	{
		case Transport_LAN: TransportLAN::Shutdown();
		default: break;
	}
}

net::Transport * net::Transport::Create()
{
	Transport * transport = nullptr;
	
	assert( transportCount >= 0 );
	
	switch ( transportType )
	{
		case Transport_LAN: 	transport = new TransportLAN(); 		break;
//		case Transport_RakNet:	transport = new TransportRakNet(); 		break;
//		case Transport_OpenTNL:	transport = new TransportOpenTNL();		break;
//		case Transport_eNet:	transport = new TransportENet();		break;
		default: break;
	}

	assert( transport );
	assert( transport->GetType() == transportType );
	
	transportCount++;
	
	return transport;
}

void net::Transport::Destroy( Transport * transport )
{
	assert( transport );
	assert( transport->GetType() == transportType );
	assert( transportCount > 0 );
	delete transport;
	transportCount--;
}

// ---------------------------------------------------------------------------

// lan transport implementation

#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#ifdef DEBUG
#define NET_UNIT_TEST
#endif

#include "lan/NetAddress.h"
#include "lan/NetSockets.h"
#include "lan/NetBeacon.h"
#include "lan/NetConnection.h"
#include "lan/NetNodeMesh.h"
#include "NetTransport.h"

// static interface (note: unit tests are at bottom...)

bool net::TransportLAN::Initialize()
{
	return InitializeSockets();
}

void net::TransportLAN::Shutdown()
{
	return ShutdownSockets();
}

bool net::TransportLAN::GetHostName( char hostname[], int size )
{
	return net::GetHostName( hostname, size );
}

// lan specific interface

net::TransportLAN::TransportLAN()
{
	mesh = nullptr;
	node = nullptr;
	beacon = nullptr;
	listener = nullptr;
	beaconAccumulator = 1.0f;
	connectingByName = false;
	connectFailed = false;
}

net::TransportLAN::~TransportLAN()
{
	Stop();
}

void net::TransportLAN::Configure( Config & config )
{
	// todo: assert not already running
	this->config = config;
}

const net::TransportLAN::Config & net::TransportLAN::GetConfig() const
{
	return config;
}

bool net::TransportLAN::StartServer( const char name[] )
{
	assert( !node );
	assert( !mesh );
	assert( !beacon );
	assert( !listener );
	printf( "lan transport: start server\n" );
	beacon = new Beacon( name, config.protocolId, config.listenerPort, config.meshPort );
	if ( !beacon->Start( config.beaconPort ) )
	{
		printf( "failed to start beacon on port %d\n", config.beaconPort );
		Stop();
		return false;
	}
	mesh = new Mesh( config.protocolId, config.maxNodes, config.meshSendRate, config.timeout );
	if ( !mesh->Start( config.meshPort ) )
	{
		printf( "failed to start mesh on port %d\n", config.meshPort );
		Stop();
		return 1;
	}
	node = new Node( config.protocolId, config.meshSendRate, config.timeout );
 	if ( !node->Start( config.serverPort ) )
	{
		printf( "failed to start node on port %d\n", config.serverPort );
		Stop();
		return 1;
	}
	mesh->Reserve( 0, Address(127,0,0,1,config.serverPort) );
	node->Join( Address(127,0,0,1,config.meshPort) );
	return true;
}

bool net::TransportLAN::ConnectClient( const char server[] )
{
	assert( !node );
	assert( !mesh );
	assert( !beacon );
	assert( !listener );
	// connect by address?
	unsigned int a = 0;
	unsigned int b = 0;
	unsigned int c = 0;
	unsigned int d = 0;
	unsigned int port = 0;
	bool isAddress = false;
	if ( sscanf( server, "%d.%d.%d.%d:%d", &a, &b, &c, &d, &port ) )
	{
		isAddress = true;
	}
	else
	{
		port = config.meshPort;
		if ( sscanf( server, "%d.%d.%d.%d", &a, &b, &c, &d ) )
			isAddress = true;
	}
	// yes, connect by address
	if ( isAddress )
	{
		printf( "lan transport: client connect to address: %d.%d.%d.%d:%d\n", a, b, c, d, port );
		node = new Node( config.protocolId, config.meshSendRate, config.timeout );
	 	if ( !node->Start( config.clientPort ) )
		{
			printf( "failed to start node on port %d\n", config.serverPort );
			Stop();
			return 1;
		}
		node->Join( Address( (unsigned char) a, (unsigned char) b, (unsigned char) c, (unsigned char) d, (unsigned short) port ) );
		return true;
	}
	// no, connect by hostname
	else
	{
		printf( "lan transport: client connect by name \"%s\"\n", server );
		listener = new Listener( config.protocolId, config.timeout );
		if ( !listener->Start( config.listenerPort ) )
		{
			printf( "failed to start listener on port %d\n", config.listenerPort );
			Stop();
			return false;
		}
		connectingByName = true;
		strncpy( connectName, server, sizeof(connectName) - 1 );
		connectName[ sizeof(connectName) - 1 ] = '\0';
		connectAccumulator = 0.0f;
		connectFailed = false;
	}
	return true;
}

bool net::TransportLAN::IsConnected() const
{
	return node && node->IsConnected();
}

bool net::TransportLAN::ConnectFailed() const
{
	return node && node->JoinFailed() || connectingByName && connectFailed;
}

bool net::TransportLAN::EnterLobby()
{
	printf( "lan transport: enter lobby\n" );
	listener = new Listener( config.protocolId, config.timeout );
	if ( !listener->Start( config.listenerPort ) )
	{
		printf( "failed to start listener on port %d\n", config.listenerPort );
		Stop();
		return false;
	}
	return true;
}

int net::TransportLAN::GetLobbyEntryCount()
{
	if ( listener )
		return listener->GetEntryCount();
	else
		return 0;
}

bool net::TransportLAN::GetLobbyEntryAtIndex( int index, LobbyEntry & entry )
{
	if ( !listener || index < 0 || index >= listener->GetEntryCount() )
		return false;
	const ListenerEntry & e = listener->GetEntry( index );
	sprintf( entry.address, "%d.%d.%d.%d:%d", e.address.GetA(), e.address.GetB(), e.address.GetC(), e.address.GetD(), e.address.GetPort() );
	strncpy( entry.name, e.name, sizeof( entry.name ) );
	entry.name[ sizeof(entry.name) - 1 ] = '\0';
	return true;
}

void net::TransportLAN::Stop()
{
	printf( "lan transport: stop\n" );
	if ( mesh )
	{
		delete mesh;
		mesh = NULL;
	}
	if ( node )
	{
		delete node;
		node = NULL;
	}
	if ( beacon )
	{
		delete beacon;
		beacon = NULL;
	}
	if ( listener )
	{
		delete listener;
		listener = NULL;
	}
	connectingByName = false;
	connectFailed = false;
}

// implement transport interface

bool net::TransportLAN::IsNodeConnected( int nodeId )
{
	assert( node );
	return node->IsNodeConnected( nodeId );
}

int net::TransportLAN::GetLocalNodeId() const
{
	assert( node );
	return node->GetLocalNodeId();
}

int net::TransportLAN::GetMaxNodes() const
{
	assert( node );
	return node->GetMaxNodes();
}

bool net::TransportLAN::SendPacket( int nodeId, const unsigned char data[], int size )
{
	assert( node );
	return node->SendPacket( nodeId, data, size );
}

int net::TransportLAN::ReceivePacket( int & nodeId, unsigned char data[], int size )
{
	assert( node );
	return node->ReceivePacket( nodeId, data, size );
}

class net::ReliabilitySystem & net::TransportLAN::GetReliability( int nodeId )
{
	// todo: implement!
	static ReliabilitySystem reliabilitySystem;
	return reliabilitySystem;
}

void net::TransportLAN::Update( float deltaTime )
{
	if ( connectingByName && !connectFailed )
	{
		assert( listener );
		const int entryCount = listener->GetEntryCount();
		for ( int i = 0; i < entryCount; ++i )
		{
			const ListenerEntry & entry = listener->GetEntry( i );
			if ( strcmp( entry.name, connectName ) == 0 )
			{
				printf( "lan transport: found server %d.%d.%d.%d:%d\n", 
					entry.address.GetA(),
					entry.address.GetB(),
					entry.address.GetC(),
					entry.address.GetD(),
					entry.address.GetPort() );
				node = new Node( config.protocolId, config.meshSendRate, config.timeout );
			 	if ( !node->Start( config.clientPort ) )
				{
					printf( "failed to start node on port %d\n", config.serverPort );
					Stop();
					connectFailed = true;
					return;
				}
				node->Join( entry.address );
				delete listener;
				listener = NULL;
				connectingByName = false;
			}
		}
		if ( connectingByName )
		{
			connectAccumulator += deltaTime;
			if ( connectAccumulator > config.timeout )
				connectFailed = true;
		}
	}
	if ( mesh )
		mesh->Update( deltaTime );
	if ( node )
		node->Update( deltaTime );
	if ( beacon )
	{
		beaconAccumulator += deltaTime;
		while ( beaconAccumulator >= 1.0f )
		{
			beacon->Update( 1.0f );
			beaconAccumulator -= 1.0f;
		}
	}
	if ( listener )
		listener->Update( deltaTime );
	if ( mesh )
		mesh->Update( deltaTime );
	if ( node )
		node->Update( deltaTime );
}

net::TransportType net::TransportLAN::GetType() const
{
	return Transport_LAN;
}

// -------------------------------------------------------------------------------
// unit tests for transport layer
// -------------------------------------------------------------------------------

#ifdef DEBUG

using namespace std;
using namespace net;

#ifdef DEBUG
#define check assert
#else
#define check(n) if ( !n ) { printf( "check failed\n" ); exit(1); }
#endif

void test_connection_join()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test connection join\n" );
	printf( "-----------------------------------------------------\n" );
	
	const int ServerPort = 30000;
	const int ClientPort = 30001;
	const int ProtocolId = 0x11112222;
	const float DeltaTime = 0.001f;
	const float TimeOut = 0.1f;
	
	Connection client( ProtocolId, TimeOut );
	Connection server( ProtocolId, TimeOut );
	
	check( client.Start( ClientPort ) );
	check( server.Start( ServerPort ) );
	
	client.Connect( Address(127,0,0,1,ServerPort ) );
	server.Listen();
	
	while ( true )
	{
		if ( client.IsConnected() && server.IsConnected() )
			break;
			
		if ( !client.IsConnecting() && client.ConnectFailed() )
			break;
		
		unsigned char client_packet[] = "client to server";
		client.SendPacket( client_packet, sizeof( client_packet ) );

		unsigned char server_packet[] = "server to client";
		server.SendPacket( server_packet, sizeof( server_packet ) );
		
		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = client.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}

		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = server.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}
		
		client.Update( DeltaTime );
		server.Update( DeltaTime );
	}
	
	check( client.IsConnected() );
	check( server.IsConnected() );
}

void test_connection_join_timeout()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test connection join timeout\n" );
	printf( "-----------------------------------------------------\n" );
	
	const int ServerPort = 30000;
	const int ClientPort = 30001;
	const int ProtocolId = 0x11112222;
	const float DeltaTime = 0.001f;
	const float TimeOut = 0.1f;
	
	Connection client( ProtocolId, TimeOut );
	
	check( client.Start( ClientPort ) );
	
	client.Connect( Address(127,0,0,1,ServerPort ) );
	
	while ( true )
	{
		if ( !client.IsConnecting() )
			break;
		
		unsigned char client_packet[] = "client to server";
		client.SendPacket( client_packet, sizeof( client_packet ) );

		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = client.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}
		
		client.Update( DeltaTime );
	}
	
	check( !client.IsConnected() );
	check( client.ConnectFailed() );
}

void test_connection_join_busy()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test connection join busy\n" );
	printf( "-----------------------------------------------------\n" );
	
	const int ServerPort = 30000;
	const int ClientPort = 30001;
	const int ProtocolId = 0x11112222;
	const float DeltaTime = 0.001f;
	const float TimeOut = 0.1f;
	
	// connect client to server
	
	Connection client( ProtocolId, TimeOut );
	Connection server( ProtocolId, TimeOut );
	
	check( client.Start( ClientPort ) );
	check( server.Start( ServerPort ) );
	
	client.Connect( Address(127,0,0,1,ServerPort ) );
	server.Listen();
	
	while ( true )
	{
		if ( client.IsConnected() && server.IsConnected() )
			break;
			
		if ( !client.IsConnecting() && client.ConnectFailed() )
			break;
		
		unsigned char client_packet[] = "client to server";
		client.SendPacket( client_packet, sizeof( client_packet ) );

		unsigned char server_packet[] = "server to client";
		server.SendPacket( server_packet, sizeof( server_packet ) );
		
		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = client.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}

		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = server.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}
		
		client.Update( DeltaTime );
		server.Update( DeltaTime );
	}
	
	check( client.IsConnected() );
	check( server.IsConnected() );

	// attempt another connection, verify connect fails (busy)
	
	Connection busy( ProtocolId, TimeOut );
	check( busy.Start( ClientPort + 1 ) );
	busy.Connect( Address(127,0,0,1,ServerPort ) );

	while ( true )
	{
		if ( !busy.IsConnecting() || busy.IsConnected() )
			break;
		
		unsigned char client_packet[] = "client to server";
		client.SendPacket( client_packet, sizeof( client_packet ) );

		unsigned char server_packet[] = "server to client";
		server.SendPacket( server_packet, sizeof( server_packet ) );
		
		unsigned char busy_packet[] = "i'm so busy!";
		busy.SendPacket( busy_packet, sizeof( busy_packet ) );
		
		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = client.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}

		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = server.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}
		
		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = busy.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}

		client.Update( DeltaTime );
		server.Update( DeltaTime );
		busy.Update( DeltaTime );
	}

	check( client.IsConnected() );
	check( server.IsConnected() );
	check( !busy.IsConnected() );
	check( busy.ConnectFailed() );
}

void test_connection_rejoin()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test connection rejoin\n" );
	printf( "-----------------------------------------------------\n" );
	
	const int ServerPort = 30000;
	const int ClientPort = 30001;
	const int ProtocolId = 0x11112222;
	const float DeltaTime = 0.001f;
	const float TimeOut = 0.1f;
	
	Connection client( ProtocolId, TimeOut );
	Connection server( ProtocolId, TimeOut );
	
	check( client.Start( ClientPort ) );
	check( server.Start( ServerPort ) );

	// connect client and server
	
	client.Connect( Address(127,0,0,1,ServerPort ) );
	server.Listen();
	
	while ( true )
	{
		if ( client.IsConnected() && server.IsConnected() )
			break;
			
		if ( !client.IsConnecting() && client.ConnectFailed() )
			break;
		
		unsigned char client_packet[] = "client to server";
		client.SendPacket( client_packet, sizeof( client_packet ) );

		unsigned char server_packet[] = "server to client";
		server.SendPacket( server_packet, sizeof( server_packet ) );
		
		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = client.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}

		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = server.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}
		
		client.Update( DeltaTime );
		server.Update( DeltaTime );
	}
	
	check( client.IsConnected() );
	check( server.IsConnected() );

	// let connection timeout

	while ( client.IsConnected() || server.IsConnected() )
	{
		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = client.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}

		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = server.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}

		client.Update( DeltaTime );
		server.Update( DeltaTime );
	}
	
	check( !client.IsConnected() );
	check( !server.IsConnected() );

	// reconnect client

	client.Connect( Address(127,0,0,1,ServerPort ) );
	
	while ( true )
	{
		if ( client.IsConnected() && server.IsConnected() )
			break;
			
		if ( !client.IsConnecting() && client.ConnectFailed() )
			break;
		
		unsigned char client_packet[] = "client to server";
		client.SendPacket( client_packet, sizeof( client_packet ) );

		unsigned char server_packet[] = "server to client";
		server.SendPacket( server_packet, sizeof( server_packet ) );
		
		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = client.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}

		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = server.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}
		
		client.Update( DeltaTime );
		server.Update( DeltaTime );
	}
	
	check( client.IsConnected() );
	check( server.IsConnected() );
}

void test_connection_payload()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test connection payload\n" );
	printf( "-----------------------------------------------------\n" );
	
	const int ServerPort = 30000;
	const int ClientPort = 30001;
	const int ProtocolId = 0x11112222;
	const float DeltaTime = 0.001f;
	const float TimeOut = 0.1f;
	
	Connection client( ProtocolId, TimeOut );
	Connection server( ProtocolId, TimeOut );
	
	check( client.Start( ClientPort ) );
	check( server.Start( ServerPort ) );
	
	client.Connect( Address(127,0,0,1,ServerPort ) );
	server.Listen();
	
	while ( true )
	{
		if ( client.IsConnected() && server.IsConnected() )
			break;
			
		if ( !client.IsConnecting() && client.ConnectFailed() )
			break;
		
		unsigned char client_packet[] = "client to server";
		client.SendPacket( client_packet, sizeof( client_packet ) );

		unsigned char server_packet[] = "server to client";
		server.SendPacket( server_packet, sizeof( server_packet ) );
		
		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = client.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
			check( strcmp( (const char*) packet, "server to client" ) == 0 );
		}

		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = server.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
			check( strcmp( (const char*) packet, "client to server" ) == 0 );
		}
		
		client.Update( DeltaTime );
		server.Update( DeltaTime );
	}
	
	check( client.IsConnected() );
	check( server.IsConnected() );
}

// --------------------------------------------------------

void test_packet_queue()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test packet queue\n" );
	printf( "-----------------------------------------------------\n" );

	const unsigned int MaximumSequence = 255;

	PacketQueue packetQueue;
	
	printf( "check insert back\n" );
	for ( int i = 0; i < 100; ++i )
	{
		PacketData data;
		data.sequence = i;
		packetQueue.insert_sorted( data, MaximumSequence );
		packetQueue.verify_sorted( MaximumSequence );
	}
		
	printf( "check insert front\n" );
	packetQueue.clear();
	for ( int i = 100; i < 0; ++i )
	{
		PacketData data;
		data.sequence = i;
		packetQueue.insert_sorted( data, MaximumSequence );
		packetQueue.verify_sorted( MaximumSequence );
	}
	
	printf( "check insert random\n" );
	packetQueue.clear();
	for ( int i = 100; i < 0; ++i )
	{
		PacketData data;
		data.sequence = rand() & 0xFF;
		packetQueue.insert_sorted( data, MaximumSequence );
		packetQueue.verify_sorted( MaximumSequence );
	}

	printf( "check insert wrap around\n" );
	packetQueue.clear();
	for ( int i = 200; i <= 255; ++i )
	{
		PacketData data;
		data.sequence = i;
		packetQueue.insert_sorted( data, MaximumSequence );
		packetQueue.verify_sorted( MaximumSequence );
	}
	for ( int i = 0; i <= 50; ++i )
	{
		PacketData data;
		data.sequence = i;
		packetQueue.insert_sorted( data, MaximumSequence );
		packetQueue.verify_sorted( MaximumSequence );
	}
}

void test_reliability_system()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test reliability system\n" );
	printf( "-----------------------------------------------------\n" );
	
	const int MaximumSequence = 255;
	
	printf( "check bit index for sequence\n" );
	check( ReliabilitySystem::bit_index_for_sequence( 99, 100, MaximumSequence ) == 0 );
	check( ReliabilitySystem::bit_index_for_sequence( 90, 100, MaximumSequence ) == 9 );
	check( ReliabilitySystem::bit_index_for_sequence( 0, 1, MaximumSequence ) == 0 );
	check( ReliabilitySystem::bit_index_for_sequence( 255, 0, MaximumSequence ) == 0 );
	check( ReliabilitySystem::bit_index_for_sequence( 255, 1, MaximumSequence ) == 1 );
	check( ReliabilitySystem::bit_index_for_sequence( 254, 1, MaximumSequence ) == 2 );
	check( ReliabilitySystem::bit_index_for_sequence( 254, 2, MaximumSequence ) == 3 );
	
	printf( "check generate ack bits\n");
	PacketQueue packetQueue;
	for ( int i = 0; i < 32; ++i )
	{
		PacketData data;
		data.sequence = i;
		packetQueue.insert_sorted( data, MaximumSequence );
		packetQueue.verify_sorted( MaximumSequence );
	}
	check( ReliabilitySystem::generate_ack_bits( 32, packetQueue, MaximumSequence ) == 0xFFFFFFFF );
	check( ReliabilitySystem::generate_ack_bits( 31, packetQueue, MaximumSequence ) == 0x7FFFFFFF );
	check( ReliabilitySystem::generate_ack_bits( 33, packetQueue, MaximumSequence ) == 0xFFFFFFFE );
	check( ReliabilitySystem::generate_ack_bits( 16, packetQueue, MaximumSequence ) == 0x0000FFFF );
	check( ReliabilitySystem::generate_ack_bits( 48, packetQueue, MaximumSequence ) == 0xFFFF0000 );

	printf( "check generate ack bits with wrap\n");
	packetQueue.clear();
	for ( int i = 255 - 31; i <= 255; ++i )
	{
		PacketData data;
		data.sequence = i;
		packetQueue.insert_sorted( data, MaximumSequence );
		packetQueue.verify_sorted( MaximumSequence );
	}
	check( packetQueue.size() == 32 );
	check( ReliabilitySystem::generate_ack_bits( 0, packetQueue, MaximumSequence ) == 0xFFFFFFFF );
	check( ReliabilitySystem::generate_ack_bits( 255, packetQueue, MaximumSequence ) == 0x7FFFFFFF );
	check( ReliabilitySystem::generate_ack_bits( 1, packetQueue, MaximumSequence ) == 0xFFFFFFFE );
	check( ReliabilitySystem::generate_ack_bits( 240, packetQueue, MaximumSequence ) == 0x0000FFFF );
	check( ReliabilitySystem::generate_ack_bits( 16, packetQueue, MaximumSequence ) == 0xFFFF0000 );
	
	printf( "check process ack (1)\n" );
	{
		PacketQueue pendingAckQueue;
		for ( int i = 0; i < 33; ++i )
		{
			PacketData data;
			data.sequence = i;
			data.time = 0.0f;
			pendingAckQueue.insert_sorted( data, MaximumSequence );
			pendingAckQueue.verify_sorted( MaximumSequence );
		}
		PacketQueue ackedQueue;
		std::vector<unsigned int> acks;
		float rtt = 0.0f;
		unsigned int acked_packets = 0;
		ReliabilitySystem::process_ack( 32, 0xFFFFFFFF, pendingAckQueue, ackedQueue, acks, acked_packets, rtt, MaximumSequence );
		check( acks.size() == 33 );
		check( acked_packets == 33 );
		check( ackedQueue.size() == 33 );
		check( pendingAckQueue.size() == 0 );
		ackedQueue.verify_sorted( MaximumSequence );
		for ( unsigned int i = 0; i < acks.size(); ++i )
			check( acks[i] == i );
		unsigned int i = 0;
		for ( PacketQueue::iterator itor = ackedQueue.begin(); itor != ackedQueue.end(); ++itor, ++i )
			check( itor->sequence == i );
	}

	printf( "check process ack (2)\n" );
	{
		PacketQueue pendingAckQueue;
		for ( int i = 0; i < 33; ++i )
		{
			PacketData data;
			data.sequence = i;
			data.time = 0.0f;
			pendingAckQueue.insert_sorted( data, MaximumSequence );
			pendingAckQueue.verify_sorted( MaximumSequence );
		}
		PacketQueue ackedQueue;
		std::vector<unsigned int> acks;
		float rtt = 0.0f;
		unsigned int acked_packets = 0;
		ReliabilitySystem::process_ack( 32, 0x0000FFFF, pendingAckQueue, ackedQueue, acks, acked_packets, rtt, MaximumSequence );
		check( acks.size() == 17 );
		check( acked_packets == 17 );
		check( ackedQueue.size() == 17 );
		check( pendingAckQueue.size() == 33 - 17 );
		ackedQueue.verify_sorted( MaximumSequence );
		unsigned int i = 0;
		for ( PacketQueue::iterator itor = pendingAckQueue.begin(); itor != pendingAckQueue.end(); ++itor, ++i )
			check( itor->sequence == i );
		i = 0;
		for ( PacketQueue::iterator itor = ackedQueue.begin(); itor != ackedQueue.end(); ++itor, ++i )
			check( itor->sequence == i + 16 );
		for ( unsigned int i = 0; i < acks.size(); ++i )
			check( acks[i] == i + 16 );
	}

	printf( "check process ack (3)\n" );
	{
		PacketQueue pendingAckQueue;
		for ( int i = 0; i < 32; ++i )
		{
			PacketData data;
			data.sequence = i;
			data.time = 0.0f;
			pendingAckQueue.insert_sorted( data, MaximumSequence );
			pendingAckQueue.verify_sorted( MaximumSequence );
		}
		PacketQueue ackedQueue;
		std::vector<unsigned int> acks;
		float rtt = 0.0f;
		unsigned int acked_packets = 0;
		ReliabilitySystem::process_ack( 48, 0xFFFF0000, pendingAckQueue, ackedQueue, acks, acked_packets, rtt, MaximumSequence );
		check( acks.size() == 16 );
		check( acked_packets == 16 );
		check( ackedQueue.size() == 16 );
		check( pendingAckQueue.size() == 16 );
		ackedQueue.verify_sorted( MaximumSequence );
		unsigned int i = 0;
		for ( PacketQueue::iterator itor = pendingAckQueue.begin(); itor != pendingAckQueue.end(); ++itor, ++i )
			check( itor->sequence == i );
		i = 0;
		for ( PacketQueue::iterator itor = ackedQueue.begin(); itor != ackedQueue.end(); ++itor, ++i )
			check( itor->sequence == i + 16 );
		for ( unsigned int i = 0; i < acks.size(); ++i )
			check( acks[i] == i + 16 );
	}
	
	printf( "check process ack wrap around (1)\n" );
	{
		PacketQueue pendingAckQueue;
		for ( int i = 255 - 31; i <= 256; ++i )
		{
			PacketData data;
			data.sequence = i & 0xFF;
			data.time = 0.0f;
			pendingAckQueue.insert_sorted( data, MaximumSequence );
			pendingAckQueue.verify_sorted( MaximumSequence );
		}
		check( pendingAckQueue.size() == 33 );
		PacketQueue ackedQueue;
		std::vector<unsigned int> acks;
		float rtt = 0.0f;
		unsigned int acked_packets = 0;
		ReliabilitySystem::process_ack( 0, 0xFFFFFFFF, pendingAckQueue, ackedQueue, acks, acked_packets, rtt, MaximumSequence );
		check( acks.size() == 33 );
		check( acked_packets == 33 );
		check( ackedQueue.size() == 33 );
		check( pendingAckQueue.size() == 0 );
		ackedQueue.verify_sorted( MaximumSequence );
		for ( unsigned int i = 0; i < acks.size(); ++i )
			check( acks[i] == ( (i+255-31) & 0xFF ) );
		unsigned int i = 0;
		for ( PacketQueue::iterator itor = ackedQueue.begin(); itor != ackedQueue.end(); ++itor, ++i )
			check( itor->sequence == ( (i+255-31) & 0xFF ) );
	}

	printf( "check process ack wrap around (2)\n" );
	{
		PacketQueue pendingAckQueue;
		for ( int i = 255 - 31; i <= 256; ++i )
		{
			PacketData data;
			data.sequence = i & 0xFF;
			data.time = 0.0f;
			pendingAckQueue.insert_sorted( data, MaximumSequence );
			pendingAckQueue.verify_sorted( MaximumSequence );
		}
		check( pendingAckQueue.size() == 33 );
		PacketQueue ackedQueue;
		std::vector<unsigned int> acks;
		float rtt = 0.0f;
		unsigned int acked_packets = 0;
		ReliabilitySystem::process_ack( 0, 0x0000FFFF, pendingAckQueue, ackedQueue, acks, acked_packets, rtt, MaximumSequence );
		check( acks.size() == 17 );
		check( acked_packets == 17 );
		check( ackedQueue.size() == 17 );
		check( pendingAckQueue.size() == 33 - 17 );
		ackedQueue.verify_sorted( MaximumSequence );
		for ( unsigned int i = 0; i < acks.size(); ++i )
			check( acks[i] == ( (i+255-15) & 0xFF ) );
		unsigned int i = 0;
		for ( PacketQueue::iterator itor = pendingAckQueue.begin(); itor != pendingAckQueue.end(); ++itor, ++i )
			check( itor->sequence == i + 255 - 31 );
		i = 0;
		for ( PacketQueue::iterator itor = ackedQueue.begin(); itor != ackedQueue.end(); ++itor, ++i )
			check( itor->sequence == ( (i+255-15) & 0xFF ) );
	}

	printf( "check process ack wrap around (3)\n" );
	{
		PacketQueue pendingAckQueue;
		for ( int i = 255 - 31; i <= 255; ++i )
		{
			PacketData data;
			data.sequence = i & 0xFF;
			data.time = 0.0f;
			pendingAckQueue.insert_sorted( data, MaximumSequence );
			pendingAckQueue.verify_sorted( MaximumSequence );
		}
		check( pendingAckQueue.size() == 32 );
		PacketQueue ackedQueue;
		std::vector<unsigned int> acks;
		float rtt = 0.0f;
		unsigned int acked_packets = 0;
		ReliabilitySystem::process_ack( 16, 0xFFFF0000, pendingAckQueue, ackedQueue, acks, acked_packets, rtt, MaximumSequence );
		check( acks.size() == 16 );
		check( acked_packets == 16 );
		check( ackedQueue.size() == 16 );
		check( pendingAckQueue.size() == 16 );
		ackedQueue.verify_sorted( MaximumSequence );
		for ( unsigned int i = 0; i < acks.size(); ++i )
			check( acks[i] == ( (i+255-15) & 0xFF ) );
		unsigned int i = 0;
		for ( PacketQueue::iterator itor = pendingAckQueue.begin(); itor != pendingAckQueue.end(); ++itor, ++i )
			check( itor->sequence == i + 255 - 31 );
		i = 0;
		for ( PacketQueue::iterator itor = ackedQueue.begin(); itor != ackedQueue.end(); ++itor, ++i )
			check( itor->sequence == ( (i+255-15) & 0xFF ) );
	}
}

// --------------------------------------------------------

void test_reliable_connection_join()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test reliable connection join\n" );
	printf( "-----------------------------------------------------\n" );
	
	const int ServerPort = 30000;
	const int ClientPort = 30001;
	const int ProtocolId = 0x11112222;
	const float DeltaTime = 0.001f;
	const float TimeOut = 1.0f;
	
	ReliableConnection client( ProtocolId, TimeOut );
	ReliableConnection server( ProtocolId, TimeOut );
	
	check( client.Start( ClientPort ) );
	check( server.Start( ServerPort ) );
	
	client.Connect( Address(127,0,0,1,ServerPort ) );
	server.Listen();
	
	while ( true )
	{
		if ( client.IsConnected() && server.IsConnected() )
			break;
			
		if ( !client.IsConnecting() && client.ConnectFailed() )
			break;
		
		unsigned char client_packet[] = "client to server";
		client.SendPacket( client_packet, sizeof( client_packet ) );

		unsigned char server_packet[] = "server to client";
		server.SendPacket( server_packet, sizeof( server_packet ) );
		
		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = client.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}

		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = server.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}
		
		client.Update( DeltaTime );
		server.Update( DeltaTime );
	}
	
	check( client.IsConnected() );
	check( server.IsConnected() );
}

void test_reliable_connection_join_timeout()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test reliable connection join timeout\n" );
	printf( "-----------------------------------------------------\n" );
	
	const int ServerPort = 30000;
	const int ClientPort = 30001;
	const int ProtocolId = 0x11112222;
	const float DeltaTime = 0.001f;
	const float TimeOut = 0.1f;
	
	ReliableConnection client( ProtocolId, TimeOut );
	
	check( client.Start( ClientPort ) );
	
	client.Connect( Address(127,0,0,1,ServerPort ) );
	
	while ( true )
	{
		if ( !client.IsConnecting() )
			break;
		
		unsigned char client_packet[] = "client to server";
		client.SendPacket( client_packet, sizeof( client_packet ) );

		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = client.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}
		
		client.Update( DeltaTime );
	}
	
	check( !client.IsConnected() );
	check( client.ConnectFailed() );
}

void test_reliable_connection_join_busy()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test reliable connection join busy\n" );
	printf( "-----------------------------------------------------\n" );
	
	const int ServerPort = 30000;
	const int ClientPort = 30001;
	const int ProtocolId = 0x11112222;
	const float DeltaTime = 0.001f;
	const float TimeOut = 0.1f;
	
	// connect client to server
	
	ReliableConnection client( ProtocolId, TimeOut );
	ReliableConnection server( ProtocolId, TimeOut );
	
	check( client.Start( ClientPort ) );
	check( server.Start( ServerPort ) );
	
	client.Connect( Address(127,0,0,1,ServerPort ) );
	server.Listen();
	
	while ( true )
	{
		if ( client.IsConnected() && server.IsConnected() )
			break;
			
		if ( !client.IsConnecting() && client.ConnectFailed() )
			break;
		
		unsigned char client_packet[] = "client to server";
		client.SendPacket( client_packet, sizeof( client_packet ) );

		unsigned char server_packet[] = "server to client";
		server.SendPacket( server_packet, sizeof( server_packet ) );
		
		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = client.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}

		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = server.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}
		
		client.Update( DeltaTime );
		server.Update( DeltaTime );
	}
	
	check( client.IsConnected() );
	check( server.IsConnected() );

	// attempt another connection, verify connect fails (busy)
	
	ReliableConnection busy( ProtocolId, TimeOut );
	check( busy.Start( ClientPort + 1 ) );
	busy.Connect( Address(127,0,0,1,ServerPort ) );

	while ( true )
	{
		if ( !busy.IsConnecting() || busy.IsConnected() )
			break;
		
		unsigned char client_packet[] = "client to server";
		client.SendPacket( client_packet, sizeof( client_packet ) );

		unsigned char server_packet[] = "server to client";
		server.SendPacket( server_packet, sizeof( server_packet ) );
		
		unsigned char busy_packet[] = "i'm so busy!";
		busy.SendPacket( busy_packet, sizeof( busy_packet ) );
		
		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = client.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}

		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = server.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}
		
		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = busy.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}

		client.Update( DeltaTime );
		server.Update( DeltaTime );
		busy.Update( DeltaTime );
	}

	check( client.IsConnected() );
	check( server.IsConnected() );
	check( !busy.IsConnected() );
	check( busy.ConnectFailed() );
}

void test_reliable_connection_rejoin()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test reliable connection rejoin\n" );
	printf( "-----------------------------------------------------\n" );
	
	const int ServerPort = 30000;
	const int ClientPort = 30001;
	const int ProtocolId = 0x11112222;
	const float DeltaTime = 0.001f;
	const float TimeOut = 0.1f;
	
	ReliableConnection client( ProtocolId, TimeOut );
	ReliableConnection server( ProtocolId, TimeOut );
	
	check( client.Start( ClientPort ) );
	check( server.Start( ServerPort ) );

	// connect client and server
	
	client.Connect( Address(127,0,0,1,ServerPort ) );
	server.Listen();
	
	while ( true )
	{
		if ( client.IsConnected() && server.IsConnected() )
			break;
			
		if ( !client.IsConnecting() && client.ConnectFailed() )
			break;
		
		unsigned char client_packet[] = "client to server";
		client.SendPacket( client_packet, sizeof( client_packet ) );

		unsigned char server_packet[] = "server to client";
		server.SendPacket( server_packet, sizeof( server_packet ) );
		
		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = client.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}

		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = server.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}
		
		client.Update( DeltaTime );
		server.Update( DeltaTime );
	}
	
	check( client.IsConnected() );
	check( server.IsConnected() );

	// let connection timeout

	while ( client.IsConnected() || server.IsConnected() )
	{
		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = client.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}

		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = server.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}

		client.Update( DeltaTime );
		server.Update( DeltaTime );
	}
	
	check( !client.IsConnected() );
	check( !server.IsConnected() );

	// reconnect client

	client.Connect( Address(127,0,0,1,ServerPort ) );
	
	while ( true )
	{
		if ( client.IsConnected() && server.IsConnected() )
			break;
			
		if ( !client.IsConnecting() && client.ConnectFailed() )
			break;
		
		unsigned char client_packet[] = "client to server";
		client.SendPacket( client_packet, sizeof( client_packet ) );

		unsigned char server_packet[] = "server to client";
		server.SendPacket( server_packet, sizeof( server_packet ) );
		
		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = client.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}

		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = server.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
		}
		
		client.Update( DeltaTime );
		server.Update( DeltaTime );
	}
	
	check( client.IsConnected() );
	check( server.IsConnected() );
}

void test_reliable_connection_payload()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test reliable connection payload\n" );
	printf( "-----------------------------------------------------\n" );
	
	const int ServerPort = 30000;
	const int ClientPort = 30001;
	const int ProtocolId = 0x11112222;
	const float DeltaTime = 0.001f;
	const float TimeOut = 0.1f;
	
	ReliableConnection client( ProtocolId, TimeOut );
	ReliableConnection server( ProtocolId, TimeOut );
	
	check( client.Start( ClientPort ) );
	check( server.Start( ServerPort ) );
	
	client.Connect( Address(127,0,0,1,ServerPort ) );
	server.Listen();
	
	while ( true )
	{
		if ( client.IsConnected() && server.IsConnected() )
			break;
			
		if ( !client.IsConnecting() && client.ConnectFailed() )
			break;
		
		unsigned char client_packet[] = "client to server";
		client.SendPacket( client_packet, sizeof( client_packet ) );

		unsigned char server_packet[] = "server to client";
		server.SendPacket( server_packet, sizeof( server_packet ) );
		
		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = client.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
			check( strcmp( (const char*) packet, "server to client" ) == 0 );
		}

		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = server.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
			check( strcmp( (const char*) packet, "client to server" ) == 0 );
		}
		
		client.Update( DeltaTime );
		server.Update( DeltaTime );
	}
	
	check( client.IsConnected() );
	check( server.IsConnected() );
}

void test_reliable_connection_acks()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test reliable connection acks\n" );
	printf( "-----------------------------------------------------\n" );

	const int ServerPort = 30000;
	const int ClientPort = 30001;
	const int ProtocolId = 0x11112222;
	const float DeltaTime = 0.001f;
	const float TimeOut = 0.1f;
	const unsigned int PacketCount = 100;
	
	ReliableConnection client( ProtocolId, TimeOut );
	ReliableConnection server( ProtocolId, TimeOut );
	
	check( client.Start( ClientPort ) );
	check( server.Start( ServerPort ) );
	
	client.Connect( Address(127,0,0,1,ServerPort ) );
	server.Listen();
		
	bool clientAckedPackets[PacketCount];
 	bool serverAckedPackets[PacketCount];
	for ( unsigned int i = 0; i < PacketCount; ++i )
	{
		clientAckedPackets[i] = false;
		serverAckedPackets[i] = false;
	}
	
	bool allPacketsAcked = false;

	while ( true )
	{
		if ( !client.IsConnecting() && client.ConnectFailed() )
			break;
			
		if ( allPacketsAcked )
			break;
		
		unsigned char packet[256];
		for ( unsigned int i = 0; i < sizeof(packet); ++i )
			packet[i] = (unsigned char) i;
		
		server.SendPacket( packet, sizeof(packet) );
		client.SendPacket( packet, sizeof(packet) );
		
		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = client.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
			check( bytes_read == sizeof(packet) );
			for ( unsigned int i = 0; i < sizeof(packet); ++i )
				check( packet[i] == (unsigned char) i );
		}

		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = server.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
			check( bytes_read == sizeof(packet) );
			for ( unsigned int i = 0; i < sizeof(packet); ++i )
				check( packet[i] == (unsigned char) i );
		}
		
		int ack_count = 0;
		unsigned int * acks = NULL;
		client.GetReliabilitySystem().GetAcks( &acks, ack_count );
		check( ack_count == 0 || ack_count != 0 && acks );
		for ( int i = 0; i < ack_count; ++i )
		{
			unsigned int ack = acks[i];
			if ( ack < PacketCount )
			{
				check( clientAckedPackets[ack] == false );
				clientAckedPackets[ack] = true;
			}
		}

		server.GetReliabilitySystem().GetAcks( &acks, ack_count );
		check( ack_count == 0 || ack_count != 0 && acks );
		for ( int i = 0; i < ack_count; ++i )
		{
			unsigned int ack = acks[i];
			if ( ack < PacketCount )
			{
				check( serverAckedPackets[ack] == false );
				serverAckedPackets[ack] = true;
			}
		}
		
		unsigned int clientAckCount = 0;
		unsigned int serverAckCount = 0;
		for ( unsigned int i = 0; i < PacketCount; ++i )
		{
 			clientAckCount += clientAckedPackets[i];
 			serverAckCount += serverAckedPackets[i];
		}
		allPacketsAcked = clientAckCount == PacketCount && serverAckCount == PacketCount;
		
		client.Update( DeltaTime );
		server.Update( DeltaTime );
	}
	
	check( client.IsConnected() );
	check( server.IsConnected() );
}

void test_reliable_connection_ack_bits()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test reliable connection ack bits\n" );
	printf( "-----------------------------------------------------\n" );

	const int ServerPort = 30000;
	const int ClientPort = 30001;
	const int ProtocolId = 0x11112222;
	const float DeltaTime = 0.001f;
	const float TimeOut = 0.1f;
	const unsigned int PacketCount = 100;
	
	ReliableConnection client( ProtocolId, TimeOut );
	ReliableConnection server( ProtocolId, TimeOut );
	
	check( client.Start( ClientPort ) );
	check( server.Start( ServerPort ) );
	
	client.Connect( Address(127,0,0,1,ServerPort ) );
	server.Listen();
		
	bool clientAckedPackets[PacketCount];
	bool serverAckedPackets[PacketCount];
	for ( unsigned int i = 0; i < PacketCount; ++i )
	{
		clientAckedPackets[i] = false;
		serverAckedPackets[i] = false;
	}
	
	bool allPacketsAcked = false;

	while ( true )
	{
		if ( !client.IsConnecting() && client.ConnectFailed() )
			break;
			
		if ( allPacketsAcked )
			break;
		
		unsigned char packet[256];
		for ( unsigned int i = 0; i < sizeof(packet); ++i )
			packet[i] = (unsigned char) i;

		for ( int i = 0; i < 10; ++i )
		{
			client.SendPacket( packet, sizeof(packet) );
			
			while ( true )
			{
				unsigned char packet[256];
				int bytes_read = client.ReceivePacket( packet, sizeof(packet) );
				if ( bytes_read == 0 )
					break;
				check( bytes_read == sizeof(packet) );
				for ( unsigned int i = 0; i < sizeof(packet); ++i )
					check( packet[i] == (unsigned char) i );
			}

			int ack_count = 0;
			unsigned int * acks = NULL;
			client.GetReliabilitySystem().GetAcks( &acks, ack_count );
			check( ack_count == 0 || ack_count != 0 && acks );
			for ( int i = 0; i < ack_count; ++i )
			{
				unsigned int ack = acks[i];
				if ( ack < PacketCount )
				{
					check( !clientAckedPackets[ack] );
					clientAckedPackets[ack] = true;
				}
			}

			client.Update( DeltaTime * 0.1f );
		}
		
		server.SendPacket( packet, sizeof(packet) );

		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = server.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
			check( bytes_read == sizeof(packet) );
			for ( unsigned int i = 0; i < sizeof(packet); ++i )
				check( packet[i] == (unsigned char) i );
		}

		int ack_count = 0;
		unsigned int * acks = NULL;
		server.GetReliabilitySystem().GetAcks( &acks, ack_count );
		check( ack_count == 0 || ack_count != 0 && acks );
		for ( int i = 0; i < ack_count; ++i )
		{
			unsigned int ack = acks[i];
			if ( ack < PacketCount )
			{
				check( !serverAckedPackets[ack] );
				serverAckedPackets[ack] = true;
			}
		}
		
		unsigned int clientAckCount = 0;
		unsigned int serverAckCount = 0;
		for ( unsigned int i = 0; i < PacketCount; ++i )
		{
			if ( clientAckedPackets[i] )
				clientAckCount++;
			if ( serverAckedPackets[i] )
				serverAckCount++;
		}
//		printf( "client ack count = %d, server ack count = %d\n", clientAckCount, serverAckCount );
		allPacketsAcked = clientAckCount == PacketCount && serverAckCount == PacketCount;
		
		server.Update( DeltaTime );
	}
	
	check( client.IsConnected() );
	check( server.IsConnected() );
}

void test_reliable_connection_packet_loss()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test reliable connection packet loss\n" );
	printf( "-----------------------------------------------------\n" );

	const int ServerPort = 30000;
	const int ClientPort = 30001;
	const int ProtocolId = 0x11112222;
	const float DeltaTime = 0.001f;
	const float TimeOut = 0.1f;
	const unsigned int PacketCount = 100;
	
	ReliableConnection client( ProtocolId, TimeOut );
	ReliableConnection server( ProtocolId, TimeOut );
	
	client.SetPacketLossMask( 1 );
	server.SetPacketLossMask( 1 );
	
	check( client.Start( ClientPort ) );
	check( server.Start( ServerPort ) );
	
	client.Connect( Address(127,0,0,1,ServerPort ) );
	server.Listen();
		
	bool clientAckedPackets[PacketCount];
	bool serverAckedPackets[PacketCount];
	for ( unsigned int i = 0; i < PacketCount; ++i )
	{
		clientAckedPackets[i] = false;
		serverAckedPackets[i] = false;
	}
	
	bool allPacketsAcked = false;

	while ( true )
	{
		if ( !client.IsConnecting() && client.ConnectFailed() )
			break;
			
		if ( allPacketsAcked )
			break;
		
		unsigned char packet[256];
		for ( unsigned int i = 0; i < sizeof(packet); ++i )
			packet[i] = (unsigned char) i;

		for ( int i = 0; i < 10; ++i )
		{
			client.SendPacket( packet, sizeof(packet) );
			
			while ( true )
			{
				unsigned char packet[256];
				int bytes_read = client.ReceivePacket( packet, sizeof(packet) );
				if ( bytes_read == 0 )
					break;
				check( bytes_read == sizeof(packet) );
				for ( unsigned int i = 0; i < sizeof(packet); ++i )
					check( packet[i] == (unsigned char) i );
			}

			int ack_count = 0;
			unsigned int * acks = NULL;
			client.GetReliabilitySystem().GetAcks( &acks, ack_count );
			check( ack_count == 0 || ack_count != 0 && acks );
			for ( int i = 0; i < ack_count; ++i )
			{
				unsigned int ack = acks[i];
				if ( ack < PacketCount )
				{
					check( !clientAckedPackets[ack] );
					check ( ( ack & 1 ) == 0 );
					clientAckedPackets[ack] = true;
				}
			}

			client.Update( DeltaTime * 0.1f );
		}
		
		server.SendPacket( packet, sizeof(packet) );

		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = server.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
			check( bytes_read == sizeof(packet) );
			for ( unsigned int i = 0; i < sizeof(packet); ++i )
				check( packet[i] == (unsigned char) i );
		}

		int ack_count = 0;
		unsigned int * acks = NULL;
		server.GetReliabilitySystem().GetAcks( &acks, ack_count );
		check( ack_count == 0 || ack_count != 0 && acks );
		for ( int i = 0; i < ack_count; ++i )
		{
			unsigned int ack = acks[i];
			if ( ack < PacketCount )
			{
				check( !serverAckedPackets[ack] );
				check( ( ack & 1 ) == 0 );
				serverAckedPackets[ack] = true;
			}
		}
		
		unsigned int clientAckCount = 0;
		unsigned int serverAckCount = 0;
		for ( unsigned int i = 0; i < PacketCount; ++i )
		{
			if ( ( i & 1 ) != 0 )
			{
				check( clientAckedPackets[i] == false );
				check( serverAckedPackets[i] == false );
			}
			if ( clientAckedPackets[i] )
				clientAckCount++;
			if ( serverAckedPackets[i] )
				serverAckCount++;
		}
		allPacketsAcked = clientAckCount == PacketCount / 2 && serverAckCount == PacketCount / 2;
		
		server.Update( DeltaTime );
	}
	
	check( client.IsConnected() );
	check( server.IsConnected() );
}

void test_reliable_connection_sequence_wrap_around()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test reliable connection sequence wrap around\n" );
	printf( "-----------------------------------------------------\n" );

	const int ServerPort = 30000;
	const int ClientPort = 30001;
	const int ProtocolId = 0x11112222;
	const float DeltaTime = 0.05f;
	const float TimeOut = 1000.0f;
	const unsigned int PacketCount = 256;
	const unsigned int MaxSequence = 31;		// [0,31]

	ReliableConnection client( ProtocolId, TimeOut, MaxSequence );
	ReliableConnection server( ProtocolId, TimeOut, MaxSequence );

	check( client.Start( ClientPort ) );
	check( server.Start( ServerPort ) );

	client.Connect( Address(127,0,0,1,ServerPort ) );
	server.Listen();

	unsigned int clientAckCount[MaxSequence+1];
	unsigned int serverAckCount[MaxSequence+1];
	for ( unsigned int i = 0; i <= MaxSequence; ++i )
	{
		clientAckCount[i] = 0;
		serverAckCount[i] = 0;
	}

	bool allPacketsAcked = false;

	while ( true )
	{
		if ( !client.IsConnecting() && client.ConnectFailed() )
			break;

		if ( allPacketsAcked )
			break;

		unsigned char packet[256];
		for ( unsigned int i = 0; i < sizeof(packet); ++i )
			packet[i] = (unsigned char) i;

		server.SendPacket( packet, sizeof(packet) );
		client.SendPacket( packet, sizeof(packet) );

		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = client.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
			check( bytes_read == sizeof(packet) );
			for ( unsigned int i = 0; i < sizeof(packet); ++i )
				check( packet[i] == (unsigned char) i );
		}

		while ( true )
		{
			unsigned char packet[256];
			int bytes_read = server.ReceivePacket( packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
			check( bytes_read == sizeof(packet) );
			for ( unsigned int i = 0; i < sizeof(packet); ++i )
				check( packet[i] == (unsigned char) i );
		}

		int ack_count = 0;
		unsigned int * acks = NULL;
		client.GetReliabilitySystem().GetAcks( &acks, ack_count );
		check( ack_count == 0 || ack_count != 0 && acks );
		for ( int i = 0; i < ack_count; ++i )
		{
			unsigned int ack = acks[i];
			check( ack <= MaxSequence );
			clientAckCount[ack] += 1;
		}

		server.GetReliabilitySystem().GetAcks( &acks, ack_count );
		check( ack_count == 0 || ack_count != 0 && acks );
		for ( int i = 0; i < ack_count; ++i )
		{
			unsigned int ack = acks[i];
			check( ack <= MaxSequence );
			serverAckCount[ack]++;
		}

		unsigned int totalClientAcks = 0;
		unsigned int totalServerAcks = 0;
		for ( unsigned int i = 0; i <= MaxSequence; ++i )
		{
 			totalClientAcks += clientAckCount[i];
 			totalServerAcks += serverAckCount[i];
		}
		allPacketsAcked = totalClientAcks >= PacketCount && totalServerAcks >= PacketCount;

		// note: test above is not very specific, we can do better...

		client.Update( DeltaTime );
		server.Update( DeltaTime );
	}

	check( client.IsConnected() );
	check( server.IsConnected() );
}

// --------------------------------------------------------

void test_node_join()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test node join\n" );
	printf( "-----------------------------------------------------\n" );
	
	const int MaxNodes = 2;
	const int MeshPort = 30000;
	const int NodePort = 30001;
	const int ProtocolId = 0x12345678;
	const float DeltaTime = 0.01f;
	const float SendRate = 0.01f;
	const float TimeOut = 1.0f;
	
	Mesh mesh( ProtocolId, MaxNodes, SendRate, TimeOut );
	check( mesh.Start( MeshPort ) );
	
	Node node( ProtocolId, SendRate, TimeOut );
	check( node.Start( NodePort ) );
	
	node.Join( Address(127,0,0,1,MeshPort) );
	while ( node.IsJoining() )
	{
		node.Update( DeltaTime );
		mesh.Update( DeltaTime );
	}
	
	check( !node.JoinFailed() );

	mesh.Stop();
}

void test_node_join_fail()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test node join fail\n" );
	printf( "-----------------------------------------------------\n" );
	
	const int MeshPort = 30000;
	const int NodePort = 30001;
	const int ProtocolId = 0x12345678;
	const float DeltaTime = 0.01f;
	const float SendRate = 0.001f;
	const float TimeOut = 0.1f;
	
	Node node( ProtocolId, SendRate, TimeOut );
	check( node.Start( NodePort ) );
	
	node.Join( Address(127,0,0,1,MeshPort) );
	while ( node.IsJoining() )
	{
		node.Update( DeltaTime );
	}
	
	check( node.JoinFailed() );
}

void test_node_join_busy()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test node join busy\n" );
	printf( "-----------------------------------------------------\n" );
	
	const int MaxNodes = 1;
	const int MeshPort = 30000;
	const int NodePort = 30001;
	const int ProtocolId = 0x12345678;
	const float DeltaTime = 0.001f;
	const float SendRate = 0.001f;
	const float TimeOut = 0.1f;
	
	Mesh mesh( ProtocolId, MaxNodes, SendRate, TimeOut );
	check( mesh.Start( MeshPort ) );
	
	Node node( ProtocolId, SendRate, TimeOut );
	check( node.Start( NodePort ) );
	
	node.Join( Address(127,0,0,1,MeshPort) );
	while ( node.IsJoining() )
	{
		node.Update( DeltaTime );
		mesh.Update( DeltaTime );
	}
	
	check( !node.JoinFailed() );

	Node busy( ProtocolId, SendRate, TimeOut );
	check( busy.Start( NodePort + 1 ) );
	
	busy.Join( Address(127,0,0,1,MeshPort) );
	while ( busy.IsJoining() )
	{
		node.Update( DeltaTime );
		busy.Update( DeltaTime );
		mesh.Update( DeltaTime );
	}
	
	check( busy.JoinFailed() );
	check( node.IsConnected() );
	check( mesh.IsNodeConnected( 0 ) );

	mesh.Stop();
}

void test_node_join_multi()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test node join multi\n" );
	printf( "-----------------------------------------------------\n" );
	
	const int MaxNodes = 4;
	const int MeshPort = 30000;
	const int NodePort = 30001;
	const int ProtocolId = 0x12345678;
	const float DeltaTime = 0.01f;
	const float SendRate = 0.01f;
	const float TimeOut = 1.0f;
	
	Mesh mesh( ProtocolId, MaxNodes, SendRate, TimeOut );
	check( mesh.Start( MeshPort ) );
	
	Node * node[MaxNodes];
	for ( int i = 0; i < MaxNodes; ++i )
	{
		node[i] = new Node( ProtocolId, SendRate, TimeOut );
		check( node[i]->Start( NodePort + i ) );
	}
	
	for ( int i = 0; i < MaxNodes; ++i )
		node[i]->Join( Address(127,0,0,1,MeshPort) );		
	
	while ( true )
	{
		bool joining = false;
		for ( int i = 0; i < MaxNodes; ++i )
		{
			node[i]->Update( DeltaTime );	
			if ( node[i]->IsJoining() )
				joining = true;
		}		
		if ( !joining )
			break;
		mesh.Update( DeltaTime );
	}

	for ( int i = 0; i < MaxNodes; ++i )
	{
		check( !node[i]->IsJoining() );
		check( !node[i]->JoinFailed() );
		delete node[i];
	}

	mesh.Stop();
}

void test_node_rejoin()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test node rejoin\n" );
	printf( "-----------------------------------------------------\n" );
	
	const int MaxNodes = 2;
	const int MeshPort = 30000;
	const int NodePort = 30001;
	const int ProtocolId = 0x12345678;
	const float DeltaTime = 0.001f;
	const float SendRate = 0.001f;
	const float TimeOut = 0.1f;
	
	Mesh mesh( ProtocolId, MaxNodes, SendRate, TimeOut );
	check( mesh.Start( MeshPort ) );
	
	Node node( ProtocolId, SendRate, TimeOut * 3 );
	check( node.Start( NodePort ) );
	node.Join( Address(127,0,0,1,MeshPort) );
	while ( node.IsJoining() )
	{
		node.Update( DeltaTime );
		mesh.Update( DeltaTime );
	}
	
	node.Stop();
	check( node.Start( NodePort ) );
	node.Join( Address(127,0,0,1,MeshPort) );
	while ( node.IsJoining() )
	{
		node.Update( DeltaTime );
		mesh.Update( DeltaTime );
	}
		
	check( !node.JoinFailed() );

	mesh.Stop();
}

void test_node_timeout()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test node timeout\n" );
	printf( "-----------------------------------------------------\n" );
	
	const int MaxNodes = 2;
	const int MeshPort = 30000;
	const int NodePort = 30001;
	const int ProtocolId = 0x12345678;
	const float DeltaTime = 0.001f;
	const float SendRate = 0.001f;
	const float TimeOut = 0.1f;
	
	Mesh mesh( ProtocolId, MaxNodes, SendRate, TimeOut );
	check( mesh.Start( MeshPort ) );
	
	Node node( ProtocolId, SendRate, TimeOut );
	check( node.Start( NodePort ) );
	
	node.Join( Address(127,0,0,1,MeshPort) );
	while ( node.IsJoining() || !mesh.IsNodeConnected( 0 ) )
	{
		node.Update( DeltaTime );
		mesh.Update( DeltaTime );
	}
	
	check( !node.JoinFailed() );

	int localNodeId = node.GetLocalNodeId();
	int maxNodes = node.GetMaxNodes();

	check( localNodeId == 0 );
	check( maxNodes == MaxNodes );
	
	check( mesh.IsNodeConnected( localNodeId ) );
	
	while ( mesh.IsNodeConnected( localNodeId ) )
	{
		mesh.Update( DeltaTime );
	}
	
	check( !mesh.IsNodeConnected( localNodeId ) );
	
	while ( node.IsConnected() )
	{
		node.Update( DeltaTime );
	}
	
	check( !node.IsConnected() );
	check( node.GetLocalNodeId() == -1 );

	mesh.Stop();
}

void test_node_payload()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test node payload\n" );
	printf( "-----------------------------------------------------\n" );

	const int MaxNodes = 2;
	const int MeshPort = 30000;
	const int ClientPort = 30001;
	const int ServerPort = 30002;
	const int ProtocolId = 0x12345678;
	const float DeltaTime = 0.01f;
	const float SendRate = 0.01f;
	const float TimeOut = 1.0f;

	Mesh mesh( ProtocolId, MaxNodes, SendRate, TimeOut );
	check( mesh.Start( MeshPort ) );

	Node client( ProtocolId, SendRate, TimeOut );
	check( client.Start( ClientPort ) );

	Node server( ProtocolId, SendRate, TimeOut );
	check( server.Start( ServerPort ) );
	
	mesh.Reserve( 0, Address(127,0,0,1,ServerPort) );

	server.Join( Address(127,0,0,1,MeshPort) );
	client.Join( Address(127,0,0,1,MeshPort) );
	
	bool serverReceivedPacketFromClient = false;
	bool clientReceivedPacketFromServer = false;
	
	while ( !serverReceivedPacketFromClient || !clientReceivedPacketFromServer )
	{
		if ( client.IsConnected() )
		{
			unsigned char packet[] = "client to server";
			client.SendPacket( 0, packet, sizeof(packet) );
		}

		if ( server.IsConnected() )
		{
			unsigned char packet[] = "server to client";
			server.SendPacket( 1, packet, sizeof(packet) );
		}
		
		while ( true )
		{
			int nodeId = -1;
			unsigned char packet[256];
			int bytes_read = client.ReceivePacket( nodeId, packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
			if ( nodeId == 0 && strcmp( (const char*) packet, "server to client" ) == 0 )
				clientReceivedPacketFromServer = true;
		}

		while ( true )
		{
			int nodeId = -1;
			unsigned char packet[256];
			int bytes_read = server.ReceivePacket( nodeId, packet, sizeof(packet) );
			if ( bytes_read == 0 )
				break;
			if ( nodeId == 1 && strcmp( (const char*) packet, "client to server" ) == 0 )
				serverReceivedPacketFromClient = true;
		}
		
		client.Update( DeltaTime );
		server.Update( DeltaTime );

		mesh.Update( DeltaTime );
	}

	check( client.IsConnected() );
	check( server.IsConnected() );

	mesh.Stop();
}

void test_mesh_restart()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test mesh restart\n" );
	printf( "-----------------------------------------------------\n" );
	
	const int MaxNodes = 2;
	const int MeshPort = 30000;
	const int NodePort = 30001;
	const int ProtocolId = 0x12345678;
	const float DeltaTime = 0.001f;
	const float SendRate = 0.001f;
	const float TimeOut = 0.1f;
	
	Mesh mesh( ProtocolId, MaxNodes, SendRate, TimeOut );
	check( mesh.Start( MeshPort ) );
	
	Node node( ProtocolId, SendRate, TimeOut );
	check( node.Start( NodePort ) );
	
	node.Join( Address(127,0,0,1,MeshPort) );
	while ( node.IsJoining() )
	{
		node.Update( DeltaTime );
		mesh.Update( DeltaTime );
	}
	
	check( !node.JoinFailed() );
	check( node.GetLocalNodeId() == 0 );

	mesh.Stop();

	while ( node.IsConnected() )
	{
		node.Update( DeltaTime );
	}

	check( mesh.Start( MeshPort ) );
		
	node.Join( Address(127,0,0,1,MeshPort) );
	while ( node.IsJoining() )
	{
		node.Update( DeltaTime );
		mesh.Update( DeltaTime );
	}
	
	check( !node.JoinFailed() );
	check( node.GetLocalNodeId() == 0 );
}

void test_mesh_nodes()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test mesh nodes\n" );
	printf( "-----------------------------------------------------\n" );
	
	const int MaxNodes = 4;
	const int MeshPort = 30000;
	const int NodePort = 30001;
	const int ProtocolId = 0x12345678;
	const float DeltaTime = 0.01f;
	const float SendRate = 0.01f;
	const float TimeOut = 1.0f;
	
	Mesh mesh( ProtocolId, MaxNodes, SendRate, TimeOut );
	check( mesh.Start( MeshPort ) );
	
	Node * node[MaxNodes];
	for ( int i = 0; i < MaxNodes; ++i )
	{
		node[i] = new Node( ProtocolId, SendRate, TimeOut );
		check( node[i]->Start( NodePort + i ) );
	}
	
	for ( int i = 0; i < MaxNodes; ++i )
		node[i]->Join( Address(127,0,0,1,MeshPort) );		
	
	while ( true )
	{
		bool joining = false;
		for ( int i = 0; i < MaxNodes; ++i )
		{
			node[i]->Update( DeltaTime );	
			if ( node[i]->IsJoining() )
				joining = true;
		}		
		if ( !joining )
			break;
		mesh.Update( DeltaTime );
	}

	for ( int i = 0; i < MaxNodes; ++i )
	{
		check( !node[i]->IsJoining() );
		check( !node[i]->JoinFailed() );
	}

	// wait for all nodes to get address info for other nodes

	while ( true )
	{
		bool allConnected = true;
		for ( int i = 0; i < MaxNodes; ++i )
		{
			node[i]->Update( DeltaTime );	
			for ( int j = 0; j < MaxNodes; ++j )
			if ( !node[i]->IsNodeConnected( j ) )
				allConnected = false;
		}		
		if ( allConnected )
			break;
		mesh.Update( DeltaTime );
	}

	// verify each node has correct addresses for all nodes
	
	for ( int i = 0; i < MaxNodes; ++i )
	{
		for ( int j = 0; j < MaxNodes; ++j )
		{
			check( mesh.IsNodeConnected(j) );
			check( node[i]->IsNodeConnected(j) );
			Address a = mesh.GetNodeAddress(j);
			Address b = node[i]->GetNodeAddress(j);
			check( mesh.GetNodeAddress(j) == node[i]->GetNodeAddress(j) );
		}
	}
	
	// disconnect first node, verify all other node see node disconnect
	
	node[0]->Stop();
	
	while ( true )
	{
		bool othersSeeFirstNodeDisconnected = true;
		for ( int i = 1; i < MaxNodes; ++i )
		{
			if ( node[i]->IsNodeConnected(0) )
				othersSeeFirstNodeDisconnected = false;
		}
		
		bool allOthersConnected = true;
		for ( int i = 1; i < MaxNodes; ++i )
		{
			node[i]->Update( DeltaTime );	
			for ( int j = 1; j < MaxNodes; ++j )
				if ( !node[i]->IsNodeConnected( j ) )
					allOthersConnected = false;
		}		
		
		if ( othersSeeFirstNodeDisconnected && allOthersConnected )
			break;
			
		mesh.Update( DeltaTime );
	}
	
	for ( int i = 1; i < MaxNodes; ++i )
		check( !node[i]->IsNodeConnected(0) );
	
	for ( int i = 1; i < MaxNodes; ++i )
	{
		for ( int j = 1; j < MaxNodes; ++j )
			check( node[i]->IsNodeConnected( j ) );
	}
	
	// reconnect node, verify all nodes are connected again

	check( node[0]->Start( NodePort ) );
	node[0]->Join( Address(127,0,0,1,MeshPort) );		
	
	while ( true )
	{
		bool joining = false;
		for ( int i = 0; i < MaxNodes; ++i )
		{
			node[i]->Update( DeltaTime );	
			if ( node[i]->IsJoining() )
				joining = true;
		}		
		if ( !joining )
			break;
		mesh.Update( DeltaTime );
	}

	for ( int i = 0; i < MaxNodes; ++i )
	{
		check( !node[i]->IsJoining() );
		check( !node[i]->JoinFailed() );
	}

	while ( true )
	{
		bool allConnected = true;
		for ( int i = 0; i < MaxNodes; ++i )
		{
			node[i]->Update( DeltaTime );	
			for ( int j = 0; j < MaxNodes; ++j )
			if ( !node[i]->IsNodeConnected( j ) )
				allConnected = false;
		}		
		if ( allConnected )
			break;
		mesh.Update( DeltaTime );
	}

	for ( int i = 0; i < MaxNodes; ++i )
	{
		for ( int j = 0; j < MaxNodes; ++j )
		{
			check( mesh.IsNodeConnected(j) );
			check( node[i]->IsNodeConnected(j) );
			Address a = mesh.GetNodeAddress(j);
			Address b = node[i]->GetNodeAddress(j);
			check( mesh.GetNodeAddress(j) == node[i]->GetNodeAddress(j) );
		}
	}

	// cleanup
	
	for ( int i = 0; i < MaxNodes; ++i )
		delete node[i];

	mesh.Stop();
}

#endif

void net::TransportLAN::UnitTest()
{
	#ifdef DEBUG
	
	check( InitializeSockets() );
	
	test_connection_join();
	test_connection_join_timeout();
	test_connection_join_busy();
	test_connection_rejoin();
	test_connection_payload();
	
	test_packet_queue();
	test_reliability_system();
	
	test_reliable_connection_join();
	test_reliable_connection_join_timeout();
	test_reliable_connection_join_busy();
	test_reliable_connection_rejoin();
	test_reliable_connection_payload();
	test_reliable_connection_acks();
	test_reliable_connection_ack_bits();
	test_reliable_connection_packet_loss();
	test_reliable_connection_sequence_wrap_around();
	
	test_node_join();
	test_node_join_fail();
	test_node_join_busy();
	test_node_join_multi();
	test_node_rejoin();
	test_node_timeout();
	test_node_payload();
	test_mesh_restart();
	test_mesh_nodes();

	/*
	test_lan_transport_connect();
	test_lan_transport_connect_fail();
	test_lan_transport_connect_busy();
	test_lan_transport_reconnect();
	test_lan_transport_client_server();
	test_lan_transport_peer_to_peer();
	test_lan_transport_reliability();
	*/

	ShutdownSockets();

	printf( "-----------------------------------------------------\n" );
	printf( "passed!\n" );
	
	#endif
}

// -------------------------------------------------------------------------------