/*
	Simple Network Library from "Networking for Game Programmers"
	http://www.gafferongames.com/networking-for-game-programmers
	Author: Glenn Fiedler <gaffer@gaffer.org>
*/

#ifndef NET_TRANSPORT_H
#define NET_TRANSPORT_H

namespace net
{
	// transport type
	
	enum TransportType
	{
		Transport_None,
		Transport_LAN,
		Transport_RakNet,
		Transport_OpenTNL,
		Transport_eNet
	};
	
	// abstract network transport interface
	//  + implement this interface for different transport layers
	//  + use the reliability system classes to implement seq/ack based reliability
	
	class Transport
	{
	public:

		// static methods
		
		static bool Initialize( TransportType type );
		
		static void Shutdown();
		
		static Transport * Create();
		
		static void Destroy( Transport * transport );
		
		// transport interface
		
		virtual ~Transport() {};
		
		virtual bool IsNodeConnected( int nodeId ) = 0;
		
		virtual int GetLocalNodeId() const = 0;
		
		virtual int GetMaxNodes() const = 0;

		virtual bool SendPacket( int nodeId, const unsigned char data[], int size ) = 0;
		
		virtual int ReceivePacket( int & nodeId, unsigned char data[], int size ) = 0;

		virtual class ReliabilitySystem & GetReliability( int nodeId ) = 0;

		virtual void Update( float deltaTime ) = 0;
		
		virtual TransportType GetType() const = 0;
	};	

	// lan transport implementation
	//  + servers are advertised via net beacon
	//  + lan lobby is filled via net listener
	//  + a mesh runs on the server IP and manages node connections
	//  + a node runs on each transport, for the server with the mesh a local node also runs
	
	class TransportLAN : public Transport
	{
	public:

		// static interface
		
		static bool Initialize();

		static void Shutdown();

		static bool GetHostName( char hostname[], int size );

		static void UnitTest();

		// lan specific interface
		
		TransportLAN();
		~TransportLAN();
		
		struct Config
		{
			unsigned short meshPort;
			unsigned short serverPort;
			unsigned short clientPort;
			unsigned short beaconPort;
			unsigned short listenerPort;
			unsigned int protocolId;
			float meshSendRate;
			float timeout;
			int maxNodes;
			
			Config()
			{
				meshPort = 30000;
				clientPort = 30001;
				serverPort = 30002;
				beaconPort = 40000;
				listenerPort = 40001;
				protocolId = 0x12345678;
				meshSendRate = 0.25f;
				timeout = 10.0f;
				maxNodes = 4;
			}
		};
		
		void Configure( Config & config );
		
		const Config & GetConfig() const;
		
		bool StartServer( const char name[] );
		
		bool ConnectClient( const char server[] );

		bool IsConnected() const;
		
		bool ConnectFailed() const;

		bool EnterLobby();
		
		int GetLobbyEntryCount();
		
		struct LobbyEntry
		{
			char name[65];
			char address[65];
		};
		
		bool GetLobbyEntryAtIndex( int index, LobbyEntry & entry );
		
		void Stop();

		// implement transport interface
		
		bool IsNodeConnected( int nodeId );
		
		int GetLocalNodeId() const;
		
		int GetMaxNodes() const;

		bool SendPacket( int nodeId, const unsigned char data[], int size );
		
		int ReceivePacket( int & nodeId, unsigned char data[], int size );

		class ReliabilitySystem & GetReliability( int nodeId );

		void Update( float deltaTime );
		
		TransportType GetType() const;
				
	private:

		Config config;
		class Mesh * mesh;
		class Node * node;
		class Beacon * beacon;
		class Listener * listener;
		float beaconAccumulator;

		bool connectingByName;
		char connectName[65];
		float connectAccumulator;
		bool connectFailed;
	};
}

#endif
