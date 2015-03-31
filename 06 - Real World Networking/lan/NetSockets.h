/*
	Simple Network Library from "Networking for Game Programmers"
	http://www.gaffer.org/networking-for-game-programmers
	Author: Glenn Fiedler <gaffer@gaffer.org>
*/

#ifndef NET_SOCKETS_H
#define NET_SOCKETS_H

#include "NetPlatform.h"
#include "NetAddress.h"

#if PLATFORM == PLATFORM_WINDOWS

	#include <winsock2.h>
	#pragma comment( lib, "ws2_32.lib" )

#elif PLATFORM == PLATFORM_MAC || PLATFORM == PLATFORM_UNIX

	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <fcntl.h>
	#include <unistd.h>

#endif

#include <assert.h>
#include <stdio.h>

namespace net
{
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
	
	// get host name helper
	
	inline bool GetHostName( char * hostname, int size )
	{
		gethostname( hostname, size );
		hostname[size-1] = '\0';
		return true;		// todo: proper return value check
	}

	// helper functions for reading and writing integer values to packets
	
	inline void WriteByte( unsigned char * data, unsigned char value )
	{
		*data = value;
	}

	inline void ReadByte( const unsigned char * data, unsigned char & value )
	{
		value = *data;
	}

	inline void WriteShort( unsigned char * data, unsigned short value )
	{
		data[0] = (unsigned char) ( ( value >> 8 ) & 0xFF );
		data[1] = (unsigned char) ( value & 0xFF );
	}

	inline void ReadShort( const unsigned char * data, unsigned short & value )
	{
		value = ( ( (unsigned int)data[0] << 8 )  | ( (unsigned int)data[1] ) );				
	}

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
}

#endif
