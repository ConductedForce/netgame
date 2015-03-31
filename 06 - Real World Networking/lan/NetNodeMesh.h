/*
	Simple Network Library from "Networking for Game Programmers"
	http://www.gafferongames.com/networking-for-game-programmers
	Author: Glenn Fiedler <gaffer@gaffer.org>
*/

#ifndef NET_LAN_NODE_MESH_H
#define NET_LAN_NODE_MESH_H

#include "NetSockets.h"
#include "NetReliability.h"

#include <assert.h>
#include <vector>
#include <map>
#include <stack>

namespace net
{
	// node mesh
	//  + manages node join and leave
	//  + updates each node with set of currently joined nodes
	
	class Mesh
	{
		struct NodeState
		{
			enum Mode { Disconnected, ConnectionAccept, Connected };
			Mode mode;
			float timeoutAccumulator;
			Address address;
			int nodeId;
			NodeState()
			{
				mode = Disconnected;
				address = Address();
				nodeId = -1;
				timeoutAccumulator = 0.0f;
			}
		};
		
		unsigned int protocolId;
		float sendRate;
		float timeout;

		Socket socket;
		std::vector<NodeState> nodes;
		typedef std::map<int,NodeState*> IdToNode;
		typedef std::map<Address,NodeState*> AddrToNode;
		IdToNode id2node;
		AddrToNode addr2node;
		bool running;
		float sendAccumulator;
				
	public:

		Mesh( unsigned int protocolId, int maxNodes = 255, float sendRate = 0.25f, float timeout = 10.0f )
		{
			assert( maxNodes >= 1 );
			assert( maxNodes <= 255 );
			this->protocolId = protocolId;
			this->sendRate = sendRate;
			this->timeout = timeout;
			nodes.resize( maxNodes );
			running = false;
			sendAccumulator = 0.0f;
		}
		
		~Mesh()
		{
			if ( running )
				Stop();
		}
		
		bool Start( int port )
		{
			assert( !running );
			printf( "start mesh on port %d\n", port );
			if ( !socket.Open( port ) )
				return false;
			running = true;
			return true;
		}
		
		void Stop()
		{
			assert( running );
			printf( "stop mesh\n" );
			socket.Close();
			id2node.clear();
			addr2node.clear();
			for ( unsigned int i = 0; i < nodes.size(); ++i )
				nodes[i] = NodeState();
			running = false;
			sendAccumulator = 0.0f;
		}	
		
		void Update( float deltaTime )
		{
			assert( running );
			ReceivePackets();
			SendPackets( deltaTime );
			CheckForTimeouts( deltaTime );
		}
		
	    bool IsNodeConnected( int nodeId )
		{
			assert( nodeId >= 0 );
			assert( nodeId < (int) nodes.size() );
			return nodes[nodeId].mode == NodeState::Connected;
		}
		
		Address GetNodeAddress( int nodeId )
		{
			assert( nodeId >= 0 );
			assert( nodeId < (int) nodes.size() );
			return nodes[nodeId].address;
		}

	    int GetMaxNodes() const
		{
			assert( nodes.size() <= 255 );
			return (int) nodes.size();
		}
		
		void Reserve( int nodeId, const Address & address )
		{
			assert( nodeId >= 0 );
			assert( nodeId < (int) nodes.size() );
			printf( "mesh reserves node id %d for %d.%d.%d.%d:%d\n", 
				nodeId, address.GetA(), address.GetB(), address.GetC(), address.GetD(), address.GetPort() );
			nodes[nodeId].mode = NodeState::ConnectionAccept;
			nodes[nodeId].nodeId = nodeId;
			nodes[nodeId].address = address;
			addr2node.insert( std::make_pair( address, &nodes[nodeId] ) );
		}
		
	protected:
		
		void ReceivePackets()
		{
			while ( true )
			{
				Address sender;
				unsigned char data[256];
				int size = socket.Receive( sender, data, sizeof(data) );
				if ( !size )
					break;
				ProcessPacket( sender, data, size );
			}
		}

		void ProcessPacket( const Address & sender, unsigned char data[], int size )
		{
			assert( sender != Address() );
			assert( size > 0 );
			assert( data );
			// ignore packets that dont have the correct protocol id
			unsigned int firstIntegerInPacket = ( unsigned(data[0]) << 24 ) | ( unsigned(data[1]) << 16 ) |
			                                    ( unsigned(data[2]) << 8 )  | unsigned(data[3]);
			if ( firstIntegerInPacket != protocolId )
				return;
			// determine packet type
			enum PacketType { JoinRequest, KeepAlive };
			PacketType packetType;
			if ( data[4] == 0 )
				packetType = JoinRequest;
			else if ( data[4] == 1 )
				packetType = KeepAlive;
			else
				return;
			// process packet type
			switch ( packetType )
			{
				case JoinRequest:
				{
					// is address already joining or joined?
					AddrToNode::iterator itor = addr2node.find( sender );
					if ( itor == addr2node.end() )
					{
						// no entry for address, start join process...
						int freeSlot = -1;
						for ( unsigned int i = 0; i < nodes.size(); ++i )
						{
							if ( nodes[i].mode == NodeState::Disconnected )
							{
								freeSlot = (int) i;
								break;
							}
						}
						if ( freeSlot >= 0 )
						{
							printf( "mesh accepts %d.%d.%d.%d:%d as node %d\n", 
								sender.GetA(), sender.GetB(), sender.GetC(), sender.GetD(), sender.GetPort(), freeSlot );
							assert( nodes[freeSlot].mode == NodeState::Disconnected );
							nodes[freeSlot].mode = NodeState::ConnectionAccept;
							nodes[freeSlot].nodeId = freeSlot;
							nodes[freeSlot].address = sender;
							addr2node.insert( std::make_pair( sender, &nodes[freeSlot] ) );
						}
					}
					else if ( itor->second->mode == NodeState::ConnectionAccept )
					{
						// reset timeout accumulator, but only while joining
						itor->second->timeoutAccumulator = 0.0f;
					}
				}
				break;
				case KeepAlive:
				{
					AddrToNode::iterator itor = addr2node.find( sender );
					if ( itor != addr2node.end() )
					{
						// progress from "connection accept" to "connected"
						if ( itor->second->mode == NodeState::ConnectionAccept )
						{
							itor->second->mode = NodeState::Connected;
							printf( "mesh completes join of node %d\n", itor->second->nodeId );
						}
						// reset timeout accumulator for node
						itor->second->timeoutAccumulator = 0.0f;
					}
				}
				break;
			}
		}
		
		void SendPackets( float deltaTime )
		{
			sendAccumulator += deltaTime;
			while ( sendAccumulator > sendRate )
			{
				for ( unsigned int i = 0; i < nodes.size(); ++i )
				{
					if ( nodes[i].mode == NodeState::ConnectionAccept )
					{
						// node is negotiating join: send "connection accepted" packets
						unsigned char packet[7];
						packet[0] = (unsigned char) ( ( protocolId >> 24 ) & 0xFF );
						packet[1] = (unsigned char) ( ( protocolId >> 16 ) & 0xFF );
						packet[2] = (unsigned char) ( ( protocolId >> 8 ) & 0xFF );
						packet[3] = (unsigned char) ( ( protocolId ) & 0xFF );
						packet[4] = 0;
						packet[5] = (unsigned char) i;
						packet[6] = (unsigned char) nodes.size();
						socket.Send( nodes[i].address, packet, sizeof(packet) );
					}
					else if ( nodes[i].mode == NodeState::Connected )
					{
						// node is connected: send "update" packets
						unsigned char *packet = new unsigned char[5+6*nodes.size()];
						packet[0] = (unsigned char) ( ( protocolId >> 24 ) & 0xFF );
						packet[1] = (unsigned char) ( ( protocolId >> 16 ) & 0xFF );
						packet[2] = (unsigned char) ( ( protocolId >> 8 ) & 0xFF );
						packet[3] = (unsigned char) ( ( protocolId ) & 0xFF );
						packet[4] = 1;
						unsigned char * ptr = &packet[5];
						for ( unsigned int j = 0; j < nodes.size(); ++j )
						{
							ptr[0] = (unsigned char) nodes[j].address.GetA();
							ptr[1] = (unsigned char) nodes[j].address.GetB();
							ptr[2] = (unsigned char) nodes[j].address.GetC();
							ptr[3] = (unsigned char) nodes[j].address.GetD();
							ptr[4] = (unsigned char) ( ( nodes[j].address.GetPort() >> 8 ) & 0xFF );
							ptr[5] = (unsigned char) ( ( nodes[j].address.GetPort() ) & 0xFF );
							ptr += 6;
						}
						socket.Send( nodes[i].address, packet, sizeof(packet) );
					}
				}
				sendAccumulator -= sendRate;
			}
		}
		
		void CheckForTimeouts( float deltaTime )
		{
			for ( unsigned int i = 0; i < nodes.size(); ++i )
			{
				if ( nodes[i].mode != NodeState::Disconnected )
				{
					nodes[i].timeoutAccumulator += deltaTime;
					if ( nodes[i].timeoutAccumulator > timeout )
					{
						printf( "mesh timed out node %d\n", i );
						AddrToNode::iterator addr_itor = addr2node.find( nodes[i].address );
						assert( addr_itor != addr2node.end() );
						addr2node.erase( addr_itor );
						nodes[i] = NodeState();
					}
				}
			}
		}
	};

	// node
	
	class Node
	{
		struct NodeState
		{
			bool connected;
			Address address;
			NodeState()
			{
				connected = false;
				address = Address();
			}
		};
		
		struct BufferedPacket
		{
			int nodeId;
			std::vector<unsigned char> data;
		};
		
		typedef std::stack<BufferedPacket*> PacketBuffer;
		PacketBuffer receivedPackets;

		unsigned int protocolId;
		float sendRate;
		float timeout;
		int maxPacketSize;

		Socket socket;
		std::vector<NodeState> nodes;
		typedef std::map<int,NodeState*> IdToNode;
		typedef std::map<Address,NodeState*> AddrToNode;
		AddrToNode addr2node;
		bool running;
		float sendAccumulator;
		float timeoutAccumulator;
		enum State
		{
			Disconnected,
			Joining,
			Joined,
			JoinFail
		};
		State state;
		Address meshAddress;
		int localNodeId;

	public:

		Node( unsigned int protocolId, float sendRate = 0.25f, float timeout = 10.0f, int maxPacketSize = 1024 )
		{
			this->protocolId = protocolId;
			this->sendRate = sendRate;
			this->timeout = timeout;
			this->maxPacketSize = maxPacketSize;
			state = Disconnected;
			running = false;
			ClearData();
		}

		~Node()
		{
			if ( running )
				Stop();
		}

		bool Start( int port )
		{
			assert( !running );
			printf( "start node on port %d\n", port );
			if ( !socket.Open( port ) )
				return false;
			running = true;
			return true;
		}

		void Stop()
		{
			assert( running );
			printf( "stop node\n" );
			ClearData();
			socket.Close();
			running = false;
		}	
		
		void Join( const Address & address )
		{
			printf( "node join %d.%d.%d.%d:%d\n", 
				address.GetA(), address.GetB(), address.GetC(), address.GetD(), address.GetPort() );
			ClearData();
			state = Joining;
			meshAddress = address;
		}
		
		bool IsJoining() const
		{
			return state == Joining;
		}
		
		bool JoinFailed() const
		{
			return state == JoinFail;
		}
		
		bool IsConnected() const
		{
			return state == Joined;
		}
		
		int GetLocalNodeId() const
		{
			return localNodeId;
		}

		void Update( float deltaTime )
		{
			assert( running );
			ReceivePackets();
			SendPackets( deltaTime );
			CheckForTimeout( deltaTime );
		}

	    bool IsNodeConnected( int nodeId )
		{
			assert( nodeId >= 0 );
			assert( nodeId < (int) nodes.size() );
			return nodes[nodeId].connected;
		}

		Address GetNodeAddress( int nodeId )
		{
			assert( nodeId >= 0 );
			assert( nodeId < (int) nodes.size() );
			return nodes[nodeId].address;
		}

	    int GetMaxNodes() const
		{
			assert( nodes.size() <= 255 );
			return (int) nodes.size();
		}
		
		bool SendPacket( int nodeId, const unsigned char data[], int size )
		{
			assert( running );
			if ( nodes.size() == 0 )
				return false;	// not connected yet
			assert( nodeId >= 0 );
			assert( nodeId < (int) nodes.size() );
			if ( nodeId < 0 || nodeId >= (int) nodes.size() )
				return false;
			if ( !nodes[nodeId].connected )
				return false;
			assert( size <= maxPacketSize );
			if ( size > maxPacketSize )
				return false;
			return socket.Send( nodes[nodeId].address, data, size );
		}
		
		int ReceivePacket( int & nodeId, unsigned char data[], int size )
		{
			assert( running );
			if ( !receivedPackets.empty() )
			{
				BufferedPacket * packet = receivedPackets.top();
				assert( packet );
				if ( (int) packet->data.size() <= size )
				{
					nodeId = packet->nodeId;
					size = packet->data.size();
					memcpy( data, &packet->data[0], size );
					delete packet;
					receivedPackets.pop();
					return size;
				}
				else
				{
					delete packet;
					receivedPackets.pop();
				}
			}
			return 0;
		}

	protected:

		void ReceivePackets()
		{
			while ( true )
			{
				Address sender;
				unsigned char *data = new unsigned char[maxPacketSize];
				int size = socket.Receive( sender, data, sizeof(data) );
				if ( !size )
					break;
				ProcessPacket( sender, data, size );
			}
		}

		void ProcessPacket( const Address & sender, unsigned char data[], int size )
		{
			assert( sender != Address() );
			assert( size > 0 );
			assert( data );
			// is packet from the mesh?
			if ( sender == meshAddress )
			{
				// *** packet sent from the mesh ***
				// ignore packets that dont have the correct protocol id
				unsigned int firstIntegerInPacket = ( unsigned(data[0]) << 24 ) | ( unsigned(data[1]) << 16 ) |
				                                    ( unsigned(data[2]) << 8 )  | unsigned(data[3]);
				if ( firstIntegerInPacket != protocolId )
					return;
				// determine packet type
				enum PacketType { ConnectionAccepted, Update };
				PacketType packetType;
				if ( data[4] == 0 )
					packetType = ConnectionAccepted;
				else if ( data[4] == 1 )
					packetType = Update;
				else
					return;
				// handle packet type
				switch ( packetType )
				{
					case ConnectionAccepted:
					{
						if ( size != 7 )
							return;
						if ( state == Joining )
						{
							localNodeId = data[5];
							nodes.resize( data[6] );
							printf( "node accepts join as node %d of %d\n", localNodeId, (int) nodes.size() );
							state = Joined;
						}
						timeoutAccumulator = 0.0f;
					}
					break;
					case Update:
					{
						if ( size != (int) ( 5 + nodes.size() * 6 ) )
							return;
						if ( state == Joined )
						{
							// process update packet
							unsigned char * ptr = &data[5];
							for ( unsigned int i = 0; i < nodes.size(); ++i )
							{
								unsigned char a = ptr[0];
								unsigned char b = ptr[1];
								unsigned char c = ptr[2];
								unsigned char d = ptr[3];
								unsigned short port = (unsigned short)ptr[4] << 8 | (unsigned short)ptr[5];
								Address address( a, b, c, d, port );
								if ( address.GetAddress() != 0 )
								{
									// node is connected
									if ( address != nodes[i].address )
									{
										printf( "node %d: node %d connected\n", localNodeId, i );
										nodes[i].connected = true;
										nodes[i].address = address;
										addr2node[address] = &nodes[i];
									}
								}
								else
								{
									// node is not connected
									if ( nodes[i].connected )
									{
										printf( "node %d: node %d disconnected\n", localNodeId, i );
										AddrToNode::iterator itor = addr2node.find( nodes[i].address );
										assert( itor != addr2node.end() );
										addr2node.erase( itor );
										nodes[i].connected = false;
										nodes[i].address = Address();
									}
								}
								ptr += 6;
							}
						}
						timeoutAccumulator = 0.0f;
					}
					break;
				}
			}
			else
			{
				AddrToNode::iterator itor = addr2node.find( sender );
				if ( itor != addr2node.end() )
				{
					// *** packet sent from another node ***
					NodeState * node = itor->second;
					assert( node );
					int nodeId = (int) ( node - &nodes[0] );
					assert( nodeId >= 0 );
					assert( nodeId < (int) nodes.size() );
					BufferedPacket * packet = new BufferedPacket;
					packet->nodeId = nodeId;
					packet->data.resize( size );
					memcpy( &packet->data[0], data, size );
					receivedPackets.push( packet );
				}
			}
		}

		void SendPackets( float deltaTime )
		{
			sendAccumulator += deltaTime;
			while ( sendAccumulator > sendRate )
			{
				if ( state == Joining )
				{
					// node is joining: send "join request" packets
					unsigned char packet[5];
					packet[0] = (unsigned char) ( ( protocolId >> 24 ) & 0xFF );
					packet[1] = (unsigned char) ( ( protocolId >> 16 ) & 0xFF );
					packet[2] = (unsigned char) ( ( protocolId >> 8 ) & 0xFF );
					packet[3] = (unsigned char) ( ( protocolId ) & 0xFF );
					packet[4] = 0;
					socket.Send( meshAddress, packet, sizeof(packet) );
				}
				else if ( state == Joined )
				{
					// node is joined: send "keep alive" packets
					unsigned char packet[5];
					packet[0] = (unsigned char) ( ( protocolId >> 24 ) & 0xFF );
					packet[1] = (unsigned char) ( ( protocolId >> 16 ) & 0xFF );
					packet[2] = (unsigned char) ( ( protocolId >> 8 ) & 0xFF );
					packet[3] = (unsigned char) ( ( protocolId ) & 0xFF );
					packet[4] = 1;
					socket.Send( meshAddress, packet, sizeof(packet) );
				}
				sendAccumulator -= sendRate;
			}
		}

		void CheckForTimeout( float deltaTime )
		{
			if ( state == Joining || state == Joined )
			{
				timeoutAccumulator += deltaTime;
				if ( timeoutAccumulator > timeout )
				{
					if ( state == Joining )
					{
						printf( "node join failed\n" );
						state = JoinFail;
					}
					else
					{
						printf( "node timed out\n" );
						state = Disconnected;
					}
					ClearData();
				}
			}
		}
		
		void ClearData()
		{
			nodes.clear();
			addr2node.clear();
			while ( !receivedPackets.empty() )
			{
				BufferedPacket * packet = receivedPackets.top();
				delete packet;
				receivedPackets.pop();
			}
			sendAccumulator = 0.0f;
			timeoutAccumulator = 0.0f;
			localNodeId = -1;
			meshAddress = Address();
		}
	};
}

#endif
