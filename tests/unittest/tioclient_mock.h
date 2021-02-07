#include <WinSock2.h>

extern SOCKET (*socket_mock)(int af, int type, int protocol);
extern int (*ioctlsocket_mock)(SOCKET s, long cmd, u_long FAR* argp);
extern int (*send_mock)(SOCKET s, const char FAR* buf, int len, int flags);
extern int (*select_mock)(int nfds, fd_set FAR* readfds, fd_set FAR* writefds, fd_set FAR* exceptfds, const struct timeval FAR* timeout);
extern int (*recv_mock)(SOCKET s, char FAR* buf, int len, int flags);
extern struct hostent FAR* (*gethostbyname_mock)(  const char FAR * name);
extern int (*getaddrinfo_mock)(PCSTR pNodeName, PCSTR pServiceName, const ADDRINFOA* pHints, PADDRINFOA* ppResult);
extern void (*freeaddrinfo_mock)(PADDRINFOA pAddrInfo);
extern u_short (*htons_mock)(u_short hostshort);
extern int (*connect_mock)(SOCKET s, const struct sockaddr FAR * name, int namelen);
extern int (*closesocket_mock)(SOCKET s);
extern int (*setsockopt_mock)(SOCKET s, int level, int optname, const char FAR * optval, int optlen);
extern int (*WSAStartup_mock)(WORD wVersionRequested, LPWSADATA lpWSAData);

