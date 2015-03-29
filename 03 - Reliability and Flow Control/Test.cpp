/*
	Unit Tests for Reliable Connection
	From "Networking for Game Programmers" - http://www.gafferongames.com/networking-for-game-programmers
	Author: Glenn Fiedler <gaffer@gaffer.org>
*/

#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#define NET_UNIT_TEST

#include "Net.h"

using namespace std;
using namespace net;

#ifdef DEBUG
#define check assert
#else
#define check(n) if ( !(n) ) { printf( "check failed\n" ); exit(1); }
#endif

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

void test_join()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test join\n" );
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

void test_join_timeout()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test join timeout\n" );
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

void test_join_busy()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test join busy\n" );
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

void test_rejoin()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test rejoin\n" );
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

void test_payload()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test payload\n" );
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

void test_acks()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test acks\n" );
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

void test_ack_bits()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test ack bits\n" );
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

void test_packet_loss()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test packet loss\n" );
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

void test_sequence_wrap_around()
{
	printf( "-----------------------------------------------------\n" );
	printf( "test sequence wrap around\n" );
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

void tests()
{
	test_packet_queue();
	test_reliability_system();
	test_join();
	test_join_timeout();
	test_join_busy();
	test_rejoin();
	test_payload();
	test_acks();
	test_ack_bits();
	test_packet_loss();
	test_sequence_wrap_around();

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
