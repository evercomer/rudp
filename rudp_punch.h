#ifndef __rudp_punch_h__
#define __rudp_punch_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "rudp.h"
#include "platform_adpt.h"

#define CONNSTATUS_CONNECTED	1	//connection is established
#define CONNSTATUS_ACCEPTED	2	//connection is accepted by server
#define CONNSTATUS_READABLE	3	//data arrived from peer

#define CHECKCONNECTION_OK		0
#define CHECKCONNECTION_CONTINUE	1
#define CHECKCONNECTION_FAKE		2
#define CHECKCONNECTION_RESETED		3
#define CHECKCONNECTION_
typedef int (*CHECKCONNECTIONCB)(RUDPSOCKET sock, int status, void *data);

/*
 * \param local_port 
 * 	Local port the socket will bind to(but not listen on).
 * \param listening_peer 
 * 	The address the other side listening on, may be NULL.
 * \param candidate_peers, n_peer
 * 	The possible addresses the other side can be reached by.
 */
//说明:
//	连接建立时，以 ACCEPTED(连接为对方Listening状态的套接字接受) 或 CONNECTED(Simultaneous connection成功) 
//	为 status 参数调用cb。 
//	对ACCEPTED的连接，cb应向对端发送事务标识，以区分此连接所属的事务（或会话）.当此连接绑定传话后，接下来进入连接确认阶段.
//	对CONNECTED的连接，要进行连接确认。
//
//	因为穿透过程会同时发起多个连接，而最终只会选择一个成功连接。所以连接建立后要经双方确认, 然后才会关闭其它连接（过程），穿透过程中止。
//
//	确认阶段，其中一方选择一个（如最先建立的）连接，向另一方发送确认命令，确认后，双方关闭其它连接，使用确认的连接通信
//
//	cb返回OK，则连接得到确认，函数返回该连接；返回CONTINUE，则继续确认过程；返回其他值该连接并关闭。
//	当连接双方需要交换一些信息确认有效性时，cb向对端发送一些数据并返回 CHECKCONNECTION_CONTINUE。当对端应答到来时，
//	用status=SOCK_STATUS_READABLE再次调用cb
//
RUDPSOCKET RUDPPunch(unsigned short local_port, 
		const struct sockaddr *listening_peer, 
		const struct sockaddr_in *candidate_peers, int n_peer, 
		CHECKCONNECTIONCB cb, void *cb_data);


#ifdef __cplusplus
}
#endif

#endif

