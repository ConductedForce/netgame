/*
	Simple Network Library from "Networking for Game Programmers"
	http://www.gaffer.org/networking-for-game-programmers
	Author: Glenn Fiedler <gaffer@gaffer.org>
*/

#ifndef NET_LAN_CONNECTION_H
#define NET_LAN_CONNECTION_H

#include "NetSockets.h"
#include "NetReliability.h"

namespace net
{
	// virtual connection over UDP
	//  + very simple, just for learning purposes do not use in production code
	
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
	
	// connection with reliability (seq/ack)
	//  + just for demonstrating the reliability system

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
}

#endif
