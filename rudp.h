#ifndef __rudp_h__
#define __rudp_h__

#ifdef WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif
#include "platform_adpt.h"

typedef void* RUDPSOCKET;
#define INVALID_RUDPSOCKET	NULL
/*
typedef struct _tagIOVEC {
	void *iov_base;
	unsigned int iov_len;
} IOVEC;
*/
/*
 * Errors
 */
#define ERUDP_FIRST         -10000
#define ERUDP_NOT_SOCKET    (ERUDP_FIRST-1)
#define ERUDP_NOT_ALLOWED   (ERUDP_FIRST-2)
#define ERUDP_CONN_FAILED   (ERUDP_FIRST-3)
#define ERUDP_CONNECTED     (ERUDP_FIRST-4)
#define ERUDP_IN_PROGRESS   (ERUDP_FIRST-5)
#define ERUDP_NO_CONN       (ERUDP_FIRST-6)
#define ERUDP_BIND          (ERUDP_FIRST-7)
#define ERUDP_RESETED       (ERUDP_FIRST-8)
#define ERUDP_TIMEOUTED     (ERUDP_FIRST-9)
#define ERUDP_INVALID       (ERUDP_FIRST-10)
#define ERUDP_PEER_CLOSED   (ERUDP_FIRST-11) //normal close
#define ERUDP_AGAIN         (ERUDP_FIRST-12)

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*NONRUDPPACKETCB)(const uint8_t *buff, int len, void *p_user);

int RUDPStart();
int RUDPCleanup();

RUDPSOCKET RUDPSocket();
RUDPSOCKET RUDPSocketFromUdp(int udpsock);
int RUDPSetInvalidPacketCB(RUDPSOCKET, NONRUDPPACKETCB pkt_cb, void *p_user);
int RUDPClose(RUDPSOCKET sock);

int RUDPListen(RUDPSOCKET sock, int n);
int RUDPAccept(RUDPSOCKET sock, /*OUT*/RUDPSOCKET *accepted, struct sockaddr *addr, int *addrlen);
int RUDPBind(RUDPSOCKET sock, const struct sockaddr *addr, int addrlen);
int RUDPConnect(RUDPSOCKET sock, const struct sockaddr* addr, int addr_len);

// Set rudp socket to connected state(RS_ESTABLSHED) with default setting(receiver's buffer, ...) 
// peer_rbuf_sz: size of peer's rbuff
int RUDPConnected(RUDPSOCKET sock, const struct sockaddr* addr, int peer_rbuf_sz);

#define RUDPMSG_DONTWAIT	0x0001
//flags: 0 or RUDPMSG_xxx
int RUDPRecv(RUDPSOCKET sock, /*OUT*/int *chno, void *ptr, int len, int flags);
int RUDPRecvChn(RUDPSOCKET sock, int *chno, void *ptr, int len, int flags);
int RUDPSend(RUDPSOCKET sock, int chno, const void *ptr, int len, int flags);
int RUDPSendEx(RUDPSOCKET sock, int chno, int priority, const void *ptr, int len, int flags);
int RUDPSendV(RUDPSOCKET sock, int chno, const PA_IOVEC *v, unsigned int size, int flags);
int RUDPSendVEx(RUDPSOCKET sock, int chno, int priority, const PA_IOVEC *v, unsigned int size, int flags);


#define OPT_UDP_SNDBUF    1
#define OPT_UDP_RCVBUF    2
#define OPT_RUDP_SNDBUF   3 //called after connection
#define OPT_RUDP_RCVBUF   4 //called before connection
#define OPT_LINGER        5
#define OPT_FC            6
#define OPT_MSS           7
#define OPT_SNDTIMEO      11
#define OPT_RCVTIMEO      12
#define OPT_REUSEADDR     13 //default: 1
#define OPT_ADHOC         14 //simultaneously connect ?
#define OPT_NBLK          15
#define OPT_ERR           16 //get error
int RUDPGetSockOpt(RUDPSOCKET sock, int opt, void *optval, int *optlen);
int RUDPSetSockOpt(RUDPSOCKET sock, int opt, const void *optval, int optlen);

#define RUDPSELECT_READABLE	0x01
#define RUDPSELECT_WRITABLE	0x02
//#define RUDPSELECT_ERROR	0x04
int RUDPSelectSock(RUDPSOCKET sock, int chno, int flag/*one of RUDPSELECT_xxx*/, const struct timeval *timeout);
//return: >0 - condition is yes
//	  =0 - negative
//	  <0 - error

typedef
struct _tagSelectChn {
	RUDPSOCKET sock;	//NULL for system socket
	int	chno;		//system socket, or chno for rudp socket
} RUDPSOCKCHNO;
//All parameters except timeout are for INOUT
int RUDPSelect(RUDPSOCKCHNO *r_rcs, int *n_rrc, RUDPSOCKCHNO *w_rcs, int *n_wrc, RUDPSOCKCHNO *e_rcs, int *n_erc, const struct timeval *timeout);

#define RUDP_FD_SET(fd, prc, size) \
	do { prc[size].sock = NULL; prc[size++].chno = fd; }while(0)
#define RUDP_SET(s, chn, prc, size) \
	do { prc[size].sock = s; prc[size++].chno = chn; }while(0)
#define RUDP_CLR(s, chn, prc, size) \
	do { int i; for(i=0; i<size; i++) \
		if(prc[i].sock == s && prc[i].chno == chn) break; \
		if(i < size) { while(i<size-1) prc[i] = prc[i+1]; size--; } \
	} while(0)

int RUDP_FD_ISSET(int fd, const RUDPSOCKCHNO *prc, int size);
int RUDP_ISSET(RUDPSOCKET s, const RUDPSOCKCHNO *prc, int size);

int RUDPGetSockName(RUDPSOCKET sock, struct sockaddr *name);
int RUDPGetPeerName(RUDPSOCKET sock, struct sockaddr *name);

#ifdef __cplusplus
}
#endif

#endif

