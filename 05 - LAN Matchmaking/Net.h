/*
	Simple Network Library from "Networking for Game Programmers"
	http://www.gaffer.org/networking-for-game-programmers
	Author: Glenn Fiedler <gaffer@gaffer.org>
*/

#ifndef NET_H
#define NET_H

// platform detection

#define PLATFORM_WINDOWS  1
#define PLATFORM_MAC      2
#define PLATFORM_UNIX     3

#if defined(_WIN32)
#define PLATFORM PLATFORM_WINDOWS
#elif defined(__APPLE__)
#define PLATFORM PLATFORM_MAC
#else
#define PLATFORM PLATFORM_UNIX
#endif

#if PLATFORM == PLATFORM_WINDOWS

	#include <winsock2.h>
	#pragma comment( lib, "wsock32.lib" )
	#pragma warning( disable : 4996 )

#elif PLATFORM == PLATFORM_MAC || PLATFORM == PLATFORM_UNIX

	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <fcntl.h>

#else

	#error unknown platform!

#endif

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <vector>
#include <map>
#include <stack>
#include <list>
#include <algorithm>
#include <functional>
#if PLATFORM != PLATFORM_WINDOWS
#include <unistd.h>
#endif

namespace net
{
	// platform independent wait for n seconds

#if PLATFORM == PLATFORM_WINDOWS

	inline void wait_seconds( float seconds )
	{
		Sleep( (int) ( seconds * 1000.0f ) );
	}

#else

	inline void wait_seconds( float seconds ) { usleep( (int) ( seconds * 1000000.0f ) ); }

#endif

	// internet address

	class Address
	{
	public:
	
		Address()
		{
			address = 0;
			port = 0;
		}
	
		Address( unsigned char a, unsigned char b, unsigned char c, unsigned char d, unsigned short port )
		{
			this->address = ( a << 24 ) | ( b << 16 ) | ( c << 8 ) | d;
			this->port = port;
		}
	
		Address( unsigned int address, unsigned short port )
		{
			this->address = address;
			this->port = port;
		}
	
		unsigned int GetAddress() const
		{
			return address;
		}
	
		unsigned char GetA() const
		{
			return ( unsigned char ) ( address >> 24 );
		}
	
		unsigned char GetB() const
		{
			return ( unsigned char ) ( address >> 16 );
		}
	
		unsigned char GetC() const
		{
			return ( unsigned char ) ( address >> 8 );
		}
	
		unsigned char GetD() const
		{
			return ( unsigned char ) ( address );
		}
	
		unsigned short GetPort() const
		{ 
			return port;
		}
	
		bool operator == ( const Address & other ) const
		{
			return address == other.address && port == other.port;
		}
	
		bool operator != ( const Address & other ) const
		{
			return ! ( *this == other );
		}
		
		bool operator < ( const Address & other ) const
		{
			// note: this is so we can use address as a key in std::map
			if ( address < other.address )
				return true;
			if ( address > other.address )
				return false;
			else
				return port < other.port;
		}
	
	private:
	
		unsigned int address;
		unsigned short port;
	};

	// sockets

	inline bool InitializeSockets()
	{
		#if PLATFORM == PLATFORM_WINDOWS
	    WSADATA WsaData;
		return WSAStartup( MAKEWORD(2,2), &WsaData ) == NO_ERROR;
		#else
		return true;
		#endif
	}

	inline void ShutdownSockets()
	{
		#if PLATFORM == PLATFORM_WINDOWS
		WSACleanup();
		#endif
	}

	class Socket
	{
	public:
	
		enum Options
		{
			NonBlocking = 1,
			Broadcast = 2
		};
	
		Socket( int options = NonBlocking )
		{
			this->options = options;
			socket = 0;
		}
	
		~Socket()
		{
			Close();
		}
	
		bool Open( unsigned short port )
		{
			assert( !IsOpen() );
		
			// create socket

			socket = ::socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );

			if ( socket <= 0 )
			{
				printf( "failed to create socket\n" );
				socket = 0;
				return false;
			}

			// bind to port

			sockaddr_in address;
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = INADDR_ANY;
			address.sin_port = htons( (unsigned short) port );
		
			if ( bind( socket, (const sockaddr*) &address, sizeof(sockaddr_in) ) < 0 )
			{
				printf( "failed to bind socket\n" );
				Close();
				return false;
			}

			// set non-blocking io

			if ( options & NonBlocking )
			{
				#if PLATFORM == PLATFORM_MAC || PLATFORM == PLATFORM_UNIX
		
					int nonBlocking = 1;
					if ( fcntl( socket, F_SETFL, O_NONBLOCK, nonBlocking ) == -1 )
					{
						printf( "failed to set non-blocking socket\n" );
						Close();
						return false;
					}
			
				#elif PLATFORM == PLATFORM_WINDOWS
		
					DWORD nonBlocking = 1;
					if ( ioctlsocket( socket, FIONBIO, &nonBlocking ) != 0 )
					{
						printf( "failed to set non-blocking socket\n" );
						Close();
						return false;
					}

				#endif
			}
			
			// set broadcast socket
			
			if ( options & Broadcast )
			{
				int enable = 1;
				if ( setsockopt( socket, SOL_SOCKET, SO_BROADCAST, (const char*) &enable, sizeof( enable ) ) < 0 )
				{
					printf( "failed to set socket to broadcast\n" );
					Close();
					return false;
				}
			}
		
			return true;
		}
	
		void Close()
		{
			if ( socket != 0 )
			{
				#if PLATFORM == PLATFORM_MAC || PLATFORM == PLATFORM_UNIX
				close( socket );
				#elif PLATFORM == PLATFORM_WINDOWS
				closesocket( socket );
				#endif
				socket = 0;
			}
		}
	
		bool IsOpen() const
		{
			return socket != 0;
		}
	
		bool Send( const Address & destination, const void * data, int size )
		{
			assert( data );
			assert( size > 0 );
		
			if ( socket == 0 )
				return false;
		
			assert( destination.GetAddress() != 0 );
			assert( destination.GetPort() != 0 );
		
			sockaddr_in address;
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = htonl( destination.GetAddress() );
			address.sin_port = htons( (unsigned short) destination.GetPort() );

			int sent_bytes = sendto( socket, (const char*)data, size, 0, (sockaddr*)&address, sizeof(sockaddr_in) );

			return sent_bytes == size;
		}
	
		int Receive( Address & sender, void * data, int size )
		{
			assert( data );
			assert( size > 0 );
		
			if ( socket == 0 )
				return false;
			
			#if PLATFORM == PLATFORM_WINDOWS
			typedef int socklen_t;
			#endif
			
			sockaddr_in from;
			socklen_t fromLength = sizeof( from );

			int received_bytes = recvfrom( socket, (char*)data, size, 0, (sockaddr*)&from, &fromLength );

			if ( received_bytes <= 0 )
				return 0;

			unsigned int address = ntohl( from.sin_addr.s_addr );
			unsigned short port = ntohs( from.sin_port );

			sender = Address( address, port );

			return received_bytes;
		}
		
	private:
	
		int socket;
		int options;
	};
	
	// connection
	
	class Connection
	{
	public:
		
		enum Mode
		{
			None,	
			Client,
			Server
		};
		
		Connection( unsigned int protocolId, float timeout )
		{
			this->protocolId = protocolId;
			this->timeout = timeout;
			mode = None;
			running = false;
			ClearData();
		}
		
		virtual ~Connection()
		{
			if ( IsRunning() )
				Stop();
		}
		
		bool Start( int port )
		{
			assert( !running );
			printf( "start connection on port %d\n", port );
			if ( !socket.Open( port ) )
				return false;
			running = true;
			OnStart();
			return true;
		}
		
		void Stop()
		{
			assert( running );
			printf( "stop connection\n" );
			bool connected = IsConnected();
			ClearData();
			socket.Close();
			running = false;
			if ( connected )
				OnDisconnect();
			OnStop();
		}
		
		bool IsRunning() const
		{
			return running;
		}
		
		void Listen()
		{
			printf( "server listening for connection\n" );
			bool connected = IsConnected();
			ClearData();
			if ( connected )
				OnDisconnect();
			mode = Server;
			state = Listening;
		}
		
		void Connect( const Address & address )
		{
			printf( "client connecting to %d.%d.%d.%d:%d\n", 
				address.GetA(), address.GetB(), address.GetC(), address.GetD(), address.GetPort() );
			bool connected = IsConnected();
			ClearData();
			if ( connected )
				OnDisconnect();
			mode = Client;
			state = Connecting;
			this->address = address;
		}
		
		bool IsConnecting() const
		{
			return state == Connecting;
		}
		
		bool ConnectFailed() const
		{
			return state == ConnectFail;
		}
		
		bool IsConnected() const
		{
			return state == Connected;
		}
		
		bool IsListening() const
		{
			return state == Listening;
		}
		
		Mode GetMode() const
		{
			return mode;
		}
		
		virtual void Update( float deltaTime )
		{
			assert( running );
			timeoutAccumulator += deltaTime;
			if ( timeoutAccumulator > timeout )
			{
				if ( state == Connecting )
				{
					printf( "connect timed out\n" );
					ClearData();
					state = ConnectFail;
					OnDisconnect();
				}
				else if ( state == Connected )
				{
					printf( "connection timed out\n" );
					ClearData();
					if ( state == Connecting )
						state = ConnectFail;
					OnDisconnect();
				}
			}
		}
		
		virtual bool SendPacket( const unsigned char data[], int size )
		{
			assert( running );
			if ( address.GetAddress() == 0 )
				return false;
			unsigned char *packet = new unsigned char[size+4];
			packet[0] = (unsigned char) ( protocolId >> 24 );
			packet[1] = (unsigned char) ( ( protocolId >> 16 ) & 0xFF );
			packet[2] = (unsigned char) ( ( protocolId >> 8 ) & 0xFF );
			packet[3] = (unsigned char) ( ( protocolId ) & 0xFF );
			memcpy( &packet[4], data, size );
			return socket.Send( address, packet, size + 4 );
		}
		
		virtual int ReceivePacket( unsigned char data[], int size )
		{
			assert( running );
			unsigned char *packet = new unsigned char[size+4];
			Address sender;
			int bytes_read = socket.Receive( sender, packet, size + 4 );
			if ( bytes_read == 0 )
				return 0;
			if ( bytes_read <= 4 )
				return 0;
			if ( packet[0] != (unsigned char) ( protocolId >> 24 ) || 
				 packet[1] != (unsigned char) ( ( protocolId >> 16 ) & 0xFF ) ||
				 packet[2] != (unsigned char) ( ( protocolId >> 8 ) & 0xFF ) ||
				 packet[3] != (unsigned char) ( protocolId & 0xFF ) )
				return 0;
			if ( mode == Server && !IsConnected() )
			{
				printf( "server accepts connection from client %d.%d.%d.%d:%d\n", 
					sender.GetA(), sender.GetB(), sender.GetC(), sender.GetD(), sender.GetPort() );
				state = Connected;
				address = sender;
				OnConnect();
			}
			if ( sender == address )
			{
				if ( mode == Client && state == Connecting )
				{
					printf( "client completes connection with server\n" );
					state = Connected;
					OnConnect();
				}
				timeoutAccumulator = 0.0f;
				memcpy( data, &packet[4], bytes_read - 4 );
				return bytes_read - 4;
			}
			return 0;
		}
		
		int GetHeaderSize() const
		{
			return 4;
		}
		
	protected:
		
		virtual void OnStart()		{}
		virtual void OnStop()		{}
		virtual void OnConnect()    {}
		virtual void OnDisconnect() {}
			
	private:
		
		void ClearData()
		{
			state = Disconnected;
			timeoutAccumulator = 0.0f;
			address = Address();
		}
	
		enum State
		{
			Disconnected,
			Listening,
			Connecting,
			ConnectFail,
			Connected
		};

		unsigned int protocolId;
		float timeout;
		
		bool running;
		Mode mode;
		State state;
		Socket socket;
		float timeoutAccumulator;
		Address address;
	};
	
	// packet queue to store information about sent and received packets sorted in sequence order
	//  + we define ordering using the "sequence_more_recent" function, this works provided there is a large gap when sequence wrap occurs
	
	struct PacketData
	{
		unsigned int sequence;			// packet sequence number
		float time;					    // time offset since packet was sent or received (depending on context)
		int size;						// packet size in bytes
	};

	inline bool sequence_more_recent( unsigned int s1, unsigned int s2, unsigned int max_sequence )
	{
		return ( s1 > s2 ) && ( s1 - s2 <= max_sequence/2 ) || ( s2 > s1 ) && ( s2 - s1 > max_sequence/2 );
	}		
	
	class PacketQueue : public std::list<PacketData>
	{
	public:
		
		bool exists( unsigned int sequence )
		{
			for ( iterator itor = begin(); itor != end(); ++itor )
				if ( itor->sequence == sequence )
					return true;
			return false;
		}
		
		void insert_sorted( const PacketData & p, unsigned int max_sequence )
		{
			if ( empty() )
			{
				push_back( p );
			}
			else
			{
				if ( !sequence_more_recent( p.sequence, front().sequence, max_sequence ) )
				{
					push_front( p );
				}
				else if ( sequence_more_recent( p.sequence, back().sequence, max_sequence ) )
				{
					push_back( p );
				}
				else
				{
					for ( PacketQueue::iterator itor = begin(); itor != end(); itor++ )
					{
						assert( itor->sequence != p.sequence );
						if ( sequence_more_recent( itor->sequence, p.sequence, max_sequence ) )
						{
							insert( itor, p );
							break;
						}
					}
				}
			}
		}
		
		void verify_sorted( unsigned int max_sequence )
		{
			PacketQueue::iterator prev = end();
			for ( PacketQueue::iterator itor = begin(); itor != end(); itor++ )
			{
				assert( itor->sequence <= max_sequence );
				if ( prev != end() )
				{
					assert( sequence_more_recent( itor->sequence, prev->sequence, max_sequence ) );
					prev = itor;
				}
			}
		}
	};

	// reliability system to support reliable connection
	//  + manages sent, received, pending ack and acked packet queues
	//  + separated out from reliable connection because it is quite complex and i want to unit test it!
	
	class ReliabilitySystem
	{
	public:
		
		ReliabilitySystem( unsigned int max_sequence = 0xFFFFFFFF )
		{
			this->rtt_maximum = rtt_maximum;
			this->max_sequence = max_sequence;
			Reset();
		}
		
		void Reset()
		{
			local_sequence = 0;
			remote_sequence = 0;
			sentQueue.clear();
			receivedQueue.clear();
			pendingAckQueue.clear();
			ackedQueue.clear();
			sent_packets = 0;
			recv_packets = 0;
			lost_packets = 0;
			acked_packets = 0;
			sent_bandwidth = 0.0f;
			acked_bandwidth = 0.0f;
			rtt = 0.0f;
			rtt_maximum = 1.0f;
		}
		
		void PacketSent( int size )
		{
			if ( sentQueue.exists( local_sequence ) )
			{
				printf( "local sequence %d exists\n", local_sequence );				
				for ( PacketQueue::iterator itor = sentQueue.begin(); itor != sentQueue.end(); ++itor )
					printf( " + %d\n", itor->sequence );
			}
			assert( !sentQueue.exists( local_sequence ) );
			assert( !pendingAckQueue.exists( local_sequence ) );
			PacketData data;
			data.sequence = local_sequence;
			data.time = 0.0f;
			data.size = size;
			sentQueue.push_back( data );
			pendingAckQueue.push_back( data );
			sent_packets++;
			local_sequence++;
			if ( local_sequence > max_sequence )
				local_sequence = 0;
		}
		
		void PacketReceived( unsigned int sequence, int size )
		{
			recv_packets++;
			if ( receivedQueue.exists( sequence ) )
				return;
			PacketData data;
			data.sequence = sequence;
			data.time = 0.0f;
			data.size = size;
			receivedQueue.push_back( data );
			if ( sequence_more_recent( sequence, remote_sequence, max_sequence ) )
				remote_sequence = sequence;
		}

		unsigned int GenerateAckBits()
		{
			return generate_ack_bits( GetRemoteSequence(), receivedQueue, max_sequence );
		}
		
		void ProcessAck( unsigned int ack, unsigned int ack_bits )
		{
			process_ack( ack, ack_bits, pendingAckQueue, ackedQueue, acks, acked_packets, rtt, max_sequence );
		}
				
		void Update( float deltaTime )
		{
			acks.clear();
			AdvanceQueueTime( deltaTime );
			UpdateQueues();
			UpdateStats();
			#ifdef NET_UNIT_TEST
			Validate();
			#endif
		}
		
		void Validate()
		{
			sentQueue.verify_sorted( max_sequence );
			receivedQueue.verify_sorted( max_sequence );
			pendingAckQueue.verify_sorted( max_sequence );
			ackedQueue.verify_sorted( max_sequence );
		}

		// utility functions

		static bool sequence_more_recent( unsigned int s1, unsigned int s2, unsigned int max_sequence )
		{
			return ( s1 > s2 ) && ( s1 - s2 <= max_sequence/2 ) || ( s2 > s1 ) && ( s2 - s1 > max_sequence/2 );
		}
		
		static int bit_index_for_sequence( unsigned int sequence, unsigned int ack, unsigned int max_sequence )
		{
			assert( sequence != ack );
			assert( !sequence_more_recent( sequence, ack, max_sequence ) );
			if ( sequence > ack )
			{
				assert( ack < 33 );
				assert( max_sequence >= sequence );
 				return ack + ( max_sequence - sequence );
			}
			else
			{
				assert( ack >= 1 );
				assert( sequence <= ack - 1 );
 				return ack - 1 - sequence;
			}
		}
		
		static unsigned int generate_ack_bits( unsigned int ack, const PacketQueue & received_queue, unsigned int max_sequence )
		{
			unsigned int ack_bits = 0;
			for ( PacketQueue::const_iterator itor = received_queue.begin(); itor != received_queue.end(); itor++ )
			{
				if ( itor->sequence == ack || sequence_more_recent( itor->sequence, ack, max_sequence ) )
					break;
				int bit_index = bit_index_for_sequence( itor->sequence, ack, max_sequence );
				if ( bit_index <= 31 )
					ack_bits |= 1 << bit_index;
			}
			return ack_bits;
		}
		
		static void process_ack( unsigned int ack, unsigned int ack_bits, 
								 PacketQueue & pending_ack_queue, PacketQueue & acked_queue, 
								 std::vector<unsigned int> & acks, unsigned int & acked_packets, 
								 float & rtt, unsigned int max_sequence )
		{
			if ( pending_ack_queue.empty() )
				return;
				
			PacketQueue::iterator itor = pending_ack_queue.begin();
			while ( itor != pending_ack_queue.end() )
			{
				bool acked = false;
				
				if ( itor->sequence == ack )
				{
					acked = true;
				}
				else if ( !sequence_more_recent( itor->sequence, ack, max_sequence ) )
				{
					int bit_index = bit_index_for_sequence( itor->sequence, ack, max_sequence );
					if ( bit_index <= 31 )
						acked = ( ack_bits >> bit_index ) & 1;
				}
				
				if ( acked )
				{
					rtt += ( itor->time - rtt ) * 0.1f;

					acked_queue.insert_sorted( *itor, max_sequence );
					acks.push_back( itor->sequence );
					acked_packets++;
					itor = pending_ack_queue.erase( itor );
				}
				else
					++itor;
			}
		}
		
		// data accessors
				
		unsigned int GetLocalSequence() const
		{
			return local_sequence;
		}

		unsigned int GetRemoteSequence() const
		{
			return remote_sequence;
		}
		
		unsigned int GetMaxSequence() const
		{
			return max_sequence;
		}
				
 		void GetAcks( unsigned int ** acks, int & count )
		{
			*acks = &this->acks[0];
			count = (int) this->acks.size();
		}
		
		unsigned int GetSentPackets() const
		{
			return sent_packets;
		}
		
		unsigned int GetReceivedPackets() const
		{
			return recv_packets;
		}

		unsigned int GetLostPackets() const
		{
			return lost_packets;
		}

		unsigned int GetAckedPackets() const
		{
			return acked_packets;
		}

		float GetSentBandwidth() const
		{
			return sent_bandwidth;
		}

		float GetAckedBandwidth() const
		{
			return acked_bandwidth;
		}

		float GetRoundTripTime() const
		{
			return rtt;
		}
		
		int GetHeaderSize() const
		{
			return 12;
		}

	protected:
		
		void AdvanceQueueTime( float deltaTime )
		{
			for ( PacketQueue::iterator itor = sentQueue.begin(); itor != sentQueue.end(); itor++ )
				itor->time += deltaTime;

			for ( PacketQueue::iterator itor = receivedQueue.begin(); itor != receivedQueue.end(); itor++ )
				itor->time += deltaTime;

			for ( PacketQueue::iterator itor = pendingAckQueue.begin(); itor != pendingAckQueue.end(); itor++ )
				itor->time += deltaTime;

			for ( PacketQueue::iterator itor = ackedQueue.begin(); itor != ackedQueue.end(); itor++ )
				itor->time += deltaTime;
		}
		
		void UpdateQueues()
		{
			const float epsilon = 0.001f;

			while ( sentQueue.size() && sentQueue.front().time > rtt_maximum + epsilon )
				sentQueue.pop_front();

			if ( receivedQueue.size() )
			{
				const unsigned int latest_sequence = receivedQueue.back().sequence;
				const unsigned int minimum_sequence = latest_sequence >= 34 ? ( latest_sequence - 34 ) : max_sequence - ( 34 - latest_sequence );
				while ( receivedQueue.size() && !sequence_more_recent( receivedQueue.front().sequence, minimum_sequence, max_sequence ) )
					receivedQueue.pop_front();
			}

			while ( ackedQueue.size() && ackedQueue.front().time > rtt_maximum * 2 - epsilon )
				ackedQueue.pop_front();

			while ( pendingAckQueue.size() && pendingAckQueue.front().time > rtt_maximum + epsilon )
			{
				pendingAckQueue.pop_front();
				lost_packets++;
			}
		}
		
		void UpdateStats()
		{
			int sent_bytes_per_second = 0;
			for ( PacketQueue::iterator itor = sentQueue.begin(); itor != sentQueue.end(); ++itor )
				sent_bytes_per_second += itor->size;
			int acked_packets_per_second = 0;
			int acked_bytes_per_second = 0;
			for ( PacketQueue::iterator itor = ackedQueue.begin(); itor != ackedQueue.end(); ++itor )
			{
				if ( itor->time >= rtt_maximum )
				{
					acked_packets_per_second++;
					acked_bytes_per_second += itor->size;
				}
			}
			sent_bytes_per_second /= rtt_maximum;
			acked_bytes_per_second /= rtt_maximum;
			sent_bandwidth = sent_bytes_per_second * ( 8 / 1000.0f );
			acked_bandwidth = acked_bytes_per_second * ( 8 / 1000.0f );
		}
		
	private:
		
		unsigned int max_sequence;			// maximum sequence value before wrap around (used to test sequence wrap at low # values)
		unsigned int local_sequence;		// local sequence number for most recently sent packet
		unsigned int remote_sequence;		// remote sequence number for most recently received packet
		
		unsigned int sent_packets;			// total number of packets sent
		unsigned int recv_packets;			// total number of packets received
		unsigned int lost_packets;			// total number of packets lost
		unsigned int acked_packets;			// total number of packets acked

		float sent_bandwidth;				// approximate sent bandwidth over the last second
		float acked_bandwidth;				// approximate acked bandwidth over the last second
		float rtt;							// estimated round trip time
		float rtt_maximum;					// maximum expected round trip time (hard coded to one second for the moment)

		std::vector<unsigned int> acks;		// acked packets from last set of packet receives. cleared each update!

		PacketQueue sentQueue;				// sent packets used to calculate sent bandwidth (kept until rtt_maximum)
		PacketQueue pendingAckQueue;		// sent packets which have not been acked yet (kept until rtt_maximum * 2 )
		PacketQueue receivedQueue;			// received packets for determining acks to send (kept up to most recent recv sequence - 32)
		PacketQueue ackedQueue;				// acked packets (kept until rtt_maximum * 2)
	};

	// helper functions for reading and writing integer values to packets
	
	inline void WriteInteger( unsigned char * data, unsigned int value )
	{
		data[0] = (unsigned char) ( value >> 24 );
		data[1] = (unsigned char) ( ( value >> 16 ) & 0xFF );
		data[2] = (unsigned char) ( ( value >> 8 ) & 0xFF );
		data[3] = (unsigned char) ( value & 0xFF );
	}

	inline void ReadInteger( const unsigned char * data, unsigned int & value )
	{
		value = ( ( (unsigned int)data[0] << 24 ) | ( (unsigned int)data[1] << 16 ) | 
			      ( (unsigned int)data[2] << 8 )  | ( (unsigned int)data[3] ) );				
	}
	
	// connection with reliability (seq/ack)

	class ReliableConnection : public Connection
	{
	public:
		
		ReliableConnection( unsigned int protocolId, float timeout, unsigned int max_sequence = 0xFFFFFFFF )
			: Connection( protocolId, timeout ), reliabilitySystem( max_sequence )
		{
			ClearData();
			#ifdef NET_UNIT_TEST
			packet_loss_mask = 0;
			#endif
		}
	
		~ReliableConnection()
		{
			if ( IsRunning() )
				Stop();
		}
		
		// overriden functions from "Connection"
				
		bool SendPacket( const unsigned char data[], int size )
		{
			#ifdef NET_UNIT_TEST
			if ( reliabilitySystem.GetLocalSequence() & packet_loss_mask )
			{
				reliabilitySystem.PacketSent( size );
				return true;
			}
			#endif
			const int header = 12;
			unsigned char *packet = new unsigned char[header+size];
			unsigned int seq = reliabilitySystem.GetLocalSequence();
			unsigned int ack = reliabilitySystem.GetRemoteSequence();
			unsigned int ack_bits = reliabilitySystem.GenerateAckBits();
			WriteHeader( packet, seq, ack, ack_bits );
			memcpy( packet + header, data, size );
 			if ( !Connection::SendPacket( packet, size + header ) )
				return false;
			reliabilitySystem.PacketSent( size );
			return true;
		}	
		
		int ReceivePacket( unsigned char data[], int size )
		{
			const int header = 12;
			if ( size <= header )
				return false;
			unsigned char *packet = new unsigned char[header+size];
			int received_bytes = Connection::ReceivePacket( packet, size + header );
			if ( received_bytes == 0 )
				return false;
			if ( received_bytes <= header )
				return false;
			unsigned int packet_sequence = 0;
			unsigned int packet_ack = 0;
			unsigned int packet_ack_bits = 0;
			ReadHeader( packet, packet_sequence, packet_ack, packet_ack_bits );
			reliabilitySystem.PacketReceived( packet_sequence, received_bytes - header );
			reliabilitySystem.ProcessAck( packet_ack, packet_ack_bits );
			memcpy( data, packet + header, received_bytes - header );
			return received_bytes - header;
		}
		
		void Update( float deltaTime )
		{
			Connection::Update( deltaTime );
			reliabilitySystem.Update( deltaTime );
		}
		
		int GetHeaderSize() const
		{
			return Connection::GetHeaderSize() + reliabilitySystem.GetHeaderSize();
		}
		
		ReliabilitySystem & GetReliabilitySystem()
		{
			return reliabilitySystem;
		}

		// unit test controls
		
		#ifdef NET_UNIT_TEST
		void SetPacketLossMask( unsigned int mask )
		{
			packet_loss_mask = mask;
		}
		#endif
		
	protected:		
		
		void WriteHeader( unsigned char * header, unsigned int sequence, unsigned int ack, unsigned int ack_bits )
		{
			WriteInteger( header, sequence );
			WriteInteger( header + 4, ack );
			WriteInteger( header + 8, ack_bits );
		}

		void ReadHeader( const unsigned char * header, unsigned int & sequence, unsigned int & ack, unsigned int & ack_bits )
		{
			ReadInteger( header, sequence );
			ReadInteger( header + 4, ack );
			ReadInteger( header + 8, ack_bits );
		}

		virtual void OnStop()
		{
			ClearData();
		}
		
		virtual void OnDisconnect()
		{
			ClearData();
		}
		
	private:

		void ClearData()
		{
			reliabilitySystem.Reset();
		}

		#ifdef NET_UNIT_TEST
		unsigned int packet_loss_mask;			// mask sequence number, if non-zero, drop packet - for unit test only
		#endif
		
		ReliabilitySystem reliabilitySystem;	// reliability system: manages sequence numbers and acks, tracks network stats etc.
	};

	class FlowControl
	{
	public:
	
		FlowControl()
		{
			printf( "flow control initialized\n" );
			Reset();
		}
	
		void Reset()
		{
			mode = Bad;
			penalty_time = 4.0f;
			good_conditions_time = 0.0f;
			penalty_reduction_accumulator = 0.0f;
		}
	
		void Update( float deltaTime, float rtt )
		{
			const float RTT_Threshold = 250.0f;

			if ( mode == Good )
			{
				if ( rtt > RTT_Threshold )
				{
					printf( "*** dropping to bad mode ***\n" );
					mode = Bad;
					if ( good_conditions_time < 10.0f && penalty_time < 60.0f )
					{
						penalty_time *= 2.0f;
						if ( penalty_time > 60.0f )
							penalty_time = 60.0f;
						printf( "penalty time increased to %.1f\n", penalty_time );
					}
					good_conditions_time = 0.0f;
					penalty_reduction_accumulator = 0.0f;
					return;
				}
			
				good_conditions_time += deltaTime;
				penalty_reduction_accumulator += deltaTime;
			
				if ( penalty_reduction_accumulator > 10.0f && penalty_time > 1.0f )
				{
					penalty_time /= 2.0f;
					if ( penalty_time < 1.0f )
						penalty_time = 1.0f;
					printf( "penalty time reduced to %.1f\n", penalty_time );
					penalty_reduction_accumulator = 0.0f;
				}
			}
		
			if ( mode == Bad )
			{
				if ( rtt <= RTT_Threshold )
					good_conditions_time += deltaTime;
				else
					good_conditions_time = 0.0f;
				
				if ( good_conditions_time > penalty_time )
				{
					printf( "*** upgrading to good mode ***\n" );
					good_conditions_time = 0.0f;
					penalty_reduction_accumulator = 0.0f;
					mode = Good;
					return;
				}
			}
		}
	
		float GetSendRate()
		{
			return mode == Good ? 30.0f : 10.0f;
		}
	
	private:

		enum Mode
		{
			Good,
			Bad
		};

		Mode mode;
		float penalty_time;
		float good_conditions_time;
		float penalty_reduction_accumulator;
	};

	// mesh
	
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

	    int GetMaxAllowedNodes() const
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

	    int GetMaxAllowedNodes() const
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
					memcpy( data, &packet->data[0], packet->data.size() );
					delete packet;
					receivedPackets.pop();
					return packet->data.size();
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
	
	// beacon
	//  + sends broadcast UDP packets to the LAN
	//  + use a beacon to advertise the existence of a server
	
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
