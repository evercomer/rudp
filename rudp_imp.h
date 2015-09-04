#ifndef __rudp_imp_h__
#define __rudp_imp_h__

#include "basetype.h"
#include "linux_list.h"
#include "platform_adpt.h"


#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif

#define RUDP_HEADER_TAG		3
#define MAX_LOSS_REPORT		63	//6 bits
#define MAX_WINDOW		4095	//12 bits

//rudp packet header: 16 bytes
struct rudp_hdr {
	union {
		uint32_t u32_flags;
		/*
		struct {
			uint8_t	tag;
			uint8_t	flags;
			uint8_t	window;
			uint8_t	chno;
		} u8_flags;
		*/
		struct {
			//little endian
#if BYTE_ORDER == LITTLE_ENDIAN
			uint32_t	chno:8;
			uint32_t	window:12;
			uint32_t	n_loss:6;	//number of lost packets from "ackno". all packets before ackno are confirmed
			uint32_t	ack:1;		//0: no ack; 1: accumulated ack
			uint32_t	rst:1;
			uint32_t	fin:1;
			uint32_t	syn:1;
			uint32_t	rudp:2;		//RUDP_HEADER_TAG
#else
			uint32_t	rudp:2;
			uint32_t	syn:1;
			uint32_t	fin:1;
			uint32_t	rst:1;
			uint32_t	ack:1;
			uint32_t	n_loss:6;
			uint32_t	window:12;	//counted by packages
			uint32_t	chno:8;
#endif

		} flags;
	};

	uint32_t	seqno;		//
	uint32_t	ackno;		//Ackknowlage all packets BEFORE "ackno". !!!

	uint32_t	crc32;	//crc32 for header
};
#define PHY_HDR_CHN(hdr) (hdr.flags.chno&1)
#define PHY_CHN(chno) ((chno)&1)

//for 8bits window value
#define WINDOW_NTOH(window)	window
#define WINDOW_HTON(window)	window
//for 16bits window value
//#define WINDOW_NTOH(window)	ntohs(window)
//#define WINDOW_HTON(window)	htons(window)


//1456 = 1492(IEEE802.2) - 20(Ethernet) - 8(UDP) - 8(PPPoE) - ?
#define MAX_PACKET_SIZE 1448
#define MAX_DATA_SIZE	(MAX_PACKET_SIZE-sizeof(struct rudp_hdr))
struct rudp_pkt {
	struct rudp_pkt *next;

	//data for sending
	uint32_t  seqno;		//=ntohl(hdr.seqno), for quick reference
	unsigned int ts;	//timestamp when sending

	int	trans;		//transmission count
	int priority;

	int	len;		//length of data, exclude hdr
	unsigned char *pdata;	//=data by default, move ahead when RUDPRecv is called and partial data in this packet are read out
	//-----------------
	struct rudp_hdr hdr;
	unsigned char data[MAX_DATA_SIZE];
};


typedef enum {
	RS_DEAD	= -1,	//all resources except the pointer to rudp_socket itself are freed, only RUDPClose() is allowed
	RS_CLOSED = 0,	//initally created or accepted
	RS_LISTEN,
	RS_SYN_SENT,
	RS_SYN_RCVD,
	RS_ESTABLISHED,
	RS_CLOSE_WAIT,
	RS_FIN_QUEUED,
	RS_FIN_WAIT_1,
	RS_FIN_WAIT_2,
	RS_CLOSING,
	RS_TIME_WAIT
} RUDPSTATE;

//----------------------------------------------
//
// 500ms TIMERs Used in RUDP PCB
//  
// RT_xxx are for socket, RCT_xxx are for channel
//
#define RT_KEEP		0
#define RT_2MSL		1
#define RT_CNT	2

#define RCT_PERSIST	0
#define RCT_REXMT	1
#define RCT_CNT	2

#define RUDPT_RANGESET(tv, value, tvmin, tvmax) { \
	(tv) = (value); \
	if ((tv) < (tvmin)) \
		(tv) = (tvmin); \
	else if ((tv) > (tvmax)) \
		(tv) = (tvmax); \
}

#define RTV_KEEP_INIT	15
#define RTV_REXMTMIN	2
#define RTV_REXMTMAX	128
#define RTV_PERSMIN	10
#define RTV_PERSMAX	120
#define RTV_KEEP_CLOSE	5
//----------------------------------------------


#define SEQ_LT(a,b) ((int)((a) - (b)) < 0)
#define SEQ_LE(a,b) ((int)((a) - (b)) <= 0)
#define SEQ_GT(a,b) ((int)((a) - (b)) > 0)
#define SEQ_GE(a,b) ((int)((a) - (b)) >= 0)


/*  Buffer size of RUDP.
 *
 *  One have to respect to the UDP buffer 
 *  size (SO_SNDBUF, SO_RCVBUF)
 */
#define DEFAULT_SNDBUF_SIZE	128
#define DEFAULT_RCVBUF_SIZE	128	
struct sndbuf {
	uint32_t 	seqno;
	int 	max_pkts;	//OPT_RCVBUF
	int 	n_pkt;		/*packets in queue*/
	int 	n_unacked;
	struct rudp_pkt *first, *last;
	struct rudp_pkt *not_sent;	//first unsent packet
	struct rudp_pkt *rexmt;	//fast retransmit


	int	rawnd;		//receiver's advertised window in ack
	int	rwnd;		//keeping counted receiver's window, updated as packet is sent or ack is received

	int	rlost;		//receiver reports how many packets are expected to be re-transmitted


	/* counter for continuously duplicated acks, 
	 * fast-retransmission starts when reach 3
	 */
	int	dup_ack;
	uint32_t	fastretr_end_seq;	//fast re-transmission stop untill this seqno

	// RTT measurement
	int		stop_rttm;		//stop rtt measurement
	struct rudp_pkt *pkt_rttm_start;	//rtt measurement resumes from pkt_rttm_start after any re-transmissions
};

struct rcvbuf {
	uint32_t	first_seq;	//first seq in queue. this seq is pointed by "head"
	uint32_t	expected_seqno;	//expected seqno
	uint32_t	acked_seqno;	//ACK is sent when delay-time(DELAYED_MS) passed, or at most N(=3?) packets(or N*MSS bytes) received.
	int	should_ack;	//new packet arrived, should send ack
	int	n_lost;		//size of first gap(probably lost)

	int 	q_size;	//OPT_SNBUF
	struct rudp_pkt **pkt_q;	//size = q_size. A cycle buffer traced by head/tail
	int	head;	//first packet or first lost packet in buffer
	int	loss;	//first lost packet, or "tail"
	int	tail;	//one after the packet with max seqno
	int	win;	//receiver's window (q_size + head - tail - 1) % q_size, send in every output packet and ACK

	/* queue for channels */
};

struct rudp_channel {
	int	timer[RCT_CNT];	
	int	congested;
	struct sndbuf 	sbuf;
	struct rcvbuf 	rbuf;
};


#define MAX_PHY_CHANNELS	2
#define MAX_LOG_CHANNELS	128
struct rudp_pcb {
	struct sockaddr_in	local;
	struct sockaddr_in	peer;

	/* Congestion Control */
	/*  ca_cnt: congestion avoidance counter. 
	 * 	   In the progress of congestion avoidance, plus 1 
	 *	   when each ACK arrives, when "ca_cnt" reach "cwnd", 
	 *   	   reset "ca_cnt" and increase "cwnd" by one. 
	 */
	int	ca_cnt;
	int	cwnd;		//congestion window
	int	ssthresh;	//slow start threshold


	/* RTO */
	int	srtt;		//smoothed RTT
	int	sdev;		//smoothed deviation, or "rttvar"
	int	rto;		//retransmission timeout


	int	rwin_size;	//receiver's win size. keep unchanged after connection is established
	int	rtw_size;	//receiver's realtime win size
	struct rudp_channel	channel[MAX_PHY_CHANNELS];


	int	retr_cnt;	// Count of SYN or SYN&ACK sent when connection establishment
};

struct rudp_socket;
typedef void (*_NONRUDPPACKETCB)(const uint8_t *buff, int len, void *p_user);

#define RUDP_SOCKET_TAG		0x50445552	//'RUDP'
#define RF_ADHOC	0x00000001
#define RF_NBLK		0x00000002
struct rudp_socket {
	unsigned int tag;	//'RUDP'

	/* rudp_socket created by RUDPSocket/RUDPBindUdpSocket are
	 * linked with the inst_list chain(pointed by a global list header)
	 */
	struct list_head inst_list;

	/* rudp_socket return by RUDPAccept are linked to the listening socket's accepted_list. 
	 *
	 * If the listening socket is closed, the first accepted socket take place of 
	 * the listening socket(move into the inst_list chain). 
	 *
	 * "udp_sock" is shared by the sockets in the accepted_list chain, it is closed 
	 * only when accepted_list is empty.
	 */
	struct list_head accepted_list;	

	/* sockets not accepted by RUDPAccept(...) are placed in a seperated chain,
	 * it's easy to be found when accepted and when the listening socket is closing
	 */
	struct list_head listen_queue; 


	int	udp_sock;
	BOOL	connected;	//called connect() on udp socket(client socket only)

	int	state;	//RUDPSTATE, refer to TCP's FSM
	int	timer[RT_CNT];

	struct rudp_pcb *pcb;		//for

	PA_MUTEX	mutex_r, mutex_w;
#ifdef WIN32
	HANDLE	event_r, event_w;
#else
	pthread_cond_t event_r, event_w;
#endif
	int	flags;

	int 	rcvbuf_sz; //size of recv buffer, change before connection. OPT_RUDP_RCVBUF


	int err;

	//------------
	_NONRUDPPACKETCB non_rudp_pkt_cb;
	void            *p_user;
};

#endif

