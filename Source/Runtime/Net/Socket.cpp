/*! \file Socket.cpp
	\copyright Copyright (c) 2012 Sunside Inc., All Rights Reserved.
	\copyright See Radiance/LICENSE for licensing terms.
	\author Joe Riedel
	\ingroup runtime
*/

#include RADPCH
#include "Socket.h"
#if !defined(RAD_OPT_WINX)
#include <ifaddrs.h>
#endif

namespace net {

#if defined(RAD_OPT_WIN) && !defined(RAD_OPT_SHIP)
#define SOCKSTART static WinSockStart s_sockets;
#pragma comment(lib, "Ws2_32.lib")
namespace {
class WinSockStart {
public:
	WinSockStart() {
		WSADATA d;
		RAD_VERIFY(WSAStartup(MAKEWORD(2, 2), &d) == 0);
	}

	~WinSockStart() {
		WSACleanup();
	}
};
}
#endif

namespace {
in_addr s_localIP;
char s_szHost[256];
}

void SocketStart() {
#if defined(SOCKSTART)
	SOCKSTART
#endif
	s_localIP.s_addr = 0;
	s_szHost[0] = 0;

	if (gethostname(s_szHost, sizeof(s_szHost)) == 0) {
#if defined(RAD_OPT_WIN)
		const hostent *z = gethostbyname(s_szHost);
		if (z) {
			if (z->h_addrtype == AF_INET) {
				for (int i = 0; z->h_addr_list[i]; ++i) {
					U8 mask = z->h_addr_list[i][0];
					
					if (i < 1 || (mask == 192) || (mask == 10))
						s_localIP.s_addr = *((ULONG*)z->h_addr_list[i]);
					if (mask == 192)
						break; // 192 is best (local intranet).
				}
			}
		}
#else
		// find a 192, or 10 interface (ideal)
		struct ifaddrs *interfaces = 0;
		if (getifaddrs(&interfaces))
			return;
		
		int c = 0;
		for (struct ifaddrs *cur = interfaces; cur; cur = cur->ifa_next) {
			if (cur->ifa_addr && (cur->ifa_addr->sa_family == AF_INET)) {
				U8 mask = (((sockaddr_in*)cur->ifa_addr)->sin_addr.s_addr) & 0xff;
				
				if ((c < 1) || (mask == 192) || (mask == 10))
					s_localIP.s_addr = ((sockaddr_in*)cur->ifa_addr)->sin_addr.s_addr;
					
				++c;
				
				if (mask == 192)
					break; // best case.
			}
		}
		
		freeifaddrs(interfaces);
#endif
	}
}

const in_addr &GetLocalIP() {
	return s_localIP;
}

const char *GetHostName() {
	return s_szHost;
}

int Socket::sendto(const char *buf, int len, int flags, const struct sockaddr *to, int tolen) {
	int n = 0;
	int z;

	while ((z=::sendto(m_sd, buf+n, len-n, flags, to, tolen)) >= 0) {
		n += z;
		if (n >= len)
			break;
	}

	if (z < 0)
		return z;

	return n;
}

} // net
