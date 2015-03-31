/*
	Simple Network Library from "Networking for Game Programmers"
	http://www.gafferongames.com/networking-for-game-programmers
	Author: Glenn Fiedler <gaffer@gaffer.org>
*/

#ifndef NET_LAN_BEACON_H
#define NET_LAN_BEACON_H

#include "NetSockets.h"

#include <vector>

namespace net
{
	// beacon
	//  + sends broadcast UDP packets to the LAN
	//  + use a beacon to advertise the existence of a server on the LAN
	
	class Beacon
	{
	public:
		
		Beacon( const char name[], unsigned int protocolId, unsigned int listenerPort, unsigned int serverPort )
			: socket( Socket::Broadcast | Socket::NonBlocking )
		{
			strncpy( this->name, name, 64 );
			this->name[64] = '\0';
			this->protocolId = protocolId;
			this->listenerPort = listenerPort;
			this->serverPort = serverPort;
			running = false;
		}
		
		~Beacon()
		{
			if ( running )
				Stop();
		}
		
		bool Start( int port )
		{
			assert( !running );
			printf( "start beacon on port %d\n", port );
			if ( !socket.Open( port ) )
				return false;
			running = true;
			return true;
		}
		
		void Stop()
		{
			assert( running );
			printf( "stop beacon\n" );
			socket.Close();
			running = false;
		}
		
		void Update( float deltaTime )
		{
			assert( running );
			unsigned char packet[12+1+64];
			WriteInteger( packet, 0 );
			WriteInteger( packet + 4, protocolId );
			WriteInteger( packet + 8, serverPort );
			packet[12] = (unsigned char) strlen( name );
			assert( packet[12] < 63);
			memcpy( packet + 13, name, strlen( name ) );
			if ( !socket.Send( Address(255,255,255,255,listenerPort), packet, 12 + 1 + packet[12] ) )
				printf( "failed to send broadcast packet\n" );
			Address sender;
			while ( socket.Receive( sender, packet, 256 ) );
		}
		
	private:
		
		char name[64+1];
		unsigned int protocolId;
		unsigned int listenerPort;
		unsigned int serverPort;
		bool running;
		Socket socket;
	};
	
	// listener entry
	//  + an entry in the list of servers on the LAN
	
	struct ListenerEntry
	{
		char name[64+1];
		Address address;
		float timeoutAccumulator;
	};

	// listener
	//  + listens for broadcast packets sent over the LAN
	//  + use a listener to get a list of all the servers on the LAN
	
	class Listener
	{
	public:
		
		Listener( unsigned int protocolId, float timeout = 10.0f )
		{
			this->protocolId = protocolId;
			this->timeout = timeout;
			running = false;
			ClearData();
		}
		
		~Listener()
		{
			if ( running )
				Stop();
		}
		
		bool Start( int port )
		{
			assert( !running );
			printf( "start listener on port %d\n", port );
			if ( !socket.Open( port ) )
				return false;
			running = true;
			return true;
		}
		
		void Stop()
		{
			assert( running );
			printf( "stop listener\n" );
			socket.Close();
			running = false;
			ClearData();
		}
		
		void Update( float deltaTime )
		{
			assert( running );
			unsigned char packet[256];
			while ( true )
			{
				Address sender;
				int bytes_read = socket.Receive( sender, packet, 256 );
				if ( bytes_read == 0 )
					break;
				if ( bytes_read < 13 )
					continue;
				unsigned int packet_zero;
				unsigned int packet_protocolId;
				unsigned int packet_serverPort;
				unsigned char packet_stringLength;
				ReadInteger( packet, packet_zero );
				ReadInteger( packet + 4, packet_protocolId );
				ReadInteger( packet + 8, packet_serverPort );
				packet_stringLength = packet[12];
				if ( packet_zero != 0 )
					continue;
				if ( packet_protocolId != protocolId )
					continue;
				if ( packet_stringLength > 63 )
					continue;
				if ( packet_stringLength + 12 + 1 > bytes_read )
					continue;
				ListenerEntry entry;
				memcpy( entry.name, packet + 13, packet_stringLength );
				entry.name[packet_stringLength] = '\0';
				entry.address = Address( sender.GetA(), sender.GetB(), sender.GetC(), sender.GetD(), packet_serverPort );
				entry.timeoutAccumulator = 0.0f;
				ListenerEntry * existingEntry = FindEntry( entry );
				if ( existingEntry )
					existingEntry->timeoutAccumulator = 0.0f;
				else
					entries.push_back( entry );
			}
			std::vector<ListenerEntry>::iterator itor = entries.begin();
			while ( itor != entries.end() )
			{
				itor->timeoutAccumulator += deltaTime;
				if ( itor->timeoutAccumulator > timeout )
					itor = entries.erase( itor );
				else
					++itor;
			}
		}
		
		int GetEntryCount() const
		{
			return entries.size();
		}
		
		const ListenerEntry & GetEntry( int index ) const
		{
			assert( index >= 0 );
			assert( index < (int) entries.size() );
			return entries[index];
		}
		
	protected:
		
		ListenerEntry * FindEntry( const ListenerEntry & entry )
		{
			for ( int i = 0; i < (int) entries.size(); ++i )
			{
				if ( entries[i].address == entry.address && strcmp( entries[i].name, entry.name ) == 0 )
					return &entries[i];
			}
			return NULL;
		}
		
	private:
		
		void ClearData()
		{
			entries.clear();
		}
		
		std::vector<ListenerEntry> entries;
		unsigned int protocolId;
		float timeout;
		bool running;
		Socket socket;
	};
}

#endif
