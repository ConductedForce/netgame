/*
	Simple Network Library from "Networking for Game Programmers"
	http://www.gafferongames.com/networking-for-game-programmers
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
	#pragma comment( lib, "ws2_32.lib" )
	#pragma warning( disable : 4996  ) // get rid of all secure crt warning. (sscanf_s)

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

namespace net
{
	// platform independent wait for n seconds

#if PLATFORM == PLATFORM_WINDOWS

	void wait_seconds( float seconds )
	{
		Sleep( (int) ( seconds * 1000.0f ) );
	}

#else

	#include <unistd.h>
	void wait_seconds( float seconds ) { usleep( (int) ( seconds * 1000000.0f ) ); }

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
			return this->address == other.address && this->port == other.port;
		}
	
		bool operator != ( const Address & other ) const
		{
			return ! ( *this == other );
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
	
		Socket()
		{
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
			unsigned int port = ntohs( from.sin_port );

			sender = Address( address, port );

			return received_bytes;
		}
		
	private:
	
		int socket;
	};
}

#endif
