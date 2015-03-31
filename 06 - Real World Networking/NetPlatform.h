/*
	Simple Network Library from "Networking for Game Programmers"
	http://www.gafferongames.com/networking-for-game-programmers
	Author: Glenn Fiedler <gaffer@gaffer.org>
*/

#ifndef NET_PLATFORM_H
#define NET_PLATFORM_H

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

#ifndef PLATFORM
#error unknown platform!
#endif

#include <assert.h>
#include <stdio.h>


namespace net
{
	// platform independent wait for n seconds

#if PLATFORM == PLATFORM_WINDOWS

	#include <windows.h>

	inline void wait_seconds( float seconds )
	{
		Sleep(seconds * 1000.0f);
	}

#else

	#include <unistd.h>

	inline void wait_seconds( float seconds ) { usleep( (int) ( seconds * 1000000.0f ) ); }

#endif
}

#endif
