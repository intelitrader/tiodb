#include "tioclient_mock.h"

int stub_mock() 
{ 
	return 0;
}

#pragma warning(disable: 4113)
#pragma warning(disable: 4133)
#pragma warning(disable: 4047)

SOCKET(*socket_mock)(int af, int type, int protocol) = stub_mock;
int (*ioctlsocket_mock)(SOCKET s, long cmd, u_long FAR* argp) = stub_mock;
int (*send_mock)(SOCKET s, const char FAR* buf, int len, int flags) = stub_mock;
int (*select_mock)(int nfds, fd_set FAR* readfds, fd_set FAR* writefds, fd_set FAR* exceptfds, const struct timeval FAR* timeout) = stub_mock;
int (*recv_mock)(SOCKET s, char FAR* buf, int len, int flags) = stub_mock;
struct hostent FAR* (*gethostbyname_mock)(  const char FAR * name) = stub_mock;
int (*getaddrinfo_mock)(PCSTR pNodeName, PCSTR pServiceName, const ADDRINFOA* pHints, PADDRINFOA* ppResult) = stub_mock;
void (*freeaddrinfo_mock)(PADDRINFOA pAddrInfo) = stub_mock;
u_short (*htons_mock)(u_short hostshort)= stub_mock;
int (*connect_mock)(SOCKET s, const struct sockaddr FAR * name, int namelen)= stub_mock;
int (*closesocket_mock)(SOCKET s) = stub_mock;
int (*setsockopt_mock)(SOCKET s, int level, int optname, const char FAR * optval, int optlen) = stub_mock;
int (*WSAStartup_mock)(WORD wVersionRequested, LPWSADATA lpWSAData) = stub_mock;

#if !INCL_WINSOCK_API_PROTOTYPES
SOCKET socket(int af, int type, int protocol)
{
	return socket_mock(af, type, protocol);
}

int ioctlsocket( SOCKET s, long cmd, u_long FAR* argp)
{
	return ioctlsocket_mock(s, cmd, argp);
}

int send(	SOCKET s,	const char FAR* buf,	int len,	int flags)
{
	return send_mock(s, buf, len, flags);
}

int select(	int nfds,	fd_set FAR* readfds,	fd_set FAR* writefds,	fd_set FAR* exceptfds,	const struct timeval FAR* timeout)
{
	return select_mock(nfds, readfds, writefds, exceptfds, timeout);
}

int recv(	SOCKET s,	char FAR* buf,	int len,	int flags)
{
	return recv_mock(s, buf, len, flags);
}

struct hostent FAR*	gethostbyname(		const char FAR * name	)
{
	return gethostbyname_mock(name);
}

int getaddrinfo(PCSTR pNodeName, PCSTR pServiceName, const ADDRINFOA* pHints, PADDRINFOA* ppResult)
{
	return getaddrinfo_mock(pNodeName, pServiceName, pHints, ppResult);
}

void freeaddrinfo(PADDRINFOA pAddrInfo)
{
	freeaddrinfo_mock(pAddrInfo);
}

u_short htons(	u_short hostshort)
{
	return htons_mock(hostshort);
}

int connect(	SOCKET s,	const struct sockaddr FAR * name,	int namelen)
{
	return connect_mock(s, name, namelen);
}

int closesocket(	SOCKET s)
{
	return closesocket_mock(s);
}

int setsockopt(	SOCKET s,	int level,	int optname,	const char FAR * optval,	int optlen)
{
	return setsockopt_mock(s, level, optname, optval, optlen);
}

int WSAStartup(	WORD wVersionRequested,	LPWSADATA lpWSAData)
{
	return WSAStartup_mock(wVersionRequested, lpWSAData);
}
#endif
