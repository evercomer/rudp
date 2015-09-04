#include <stdlib.h>
#include <stdio.h>
#include "rudp_imp.h"
#include "rudp.h"
#include "crc32.h"

#include <assert.h>

#ifndef min
#define min(x,y) ((x)<(y)?(x):(y))
#endif
#ifndef max
#define max(x,y) ((x)>(y)?(x):(y))
#endif

#ifndef offsetof
#define offsetof(s, m) ((int)(&((s*)0)->m))
#endif

#if defined(WIN32) || defined(ARM_UCOS_LWIP)
#define SETEVENT(event) PA_SetEvent(event)
#elif defined(__LINUX__) || defined(__ANDROID__)
#define SETEVENT(event) pthread_cond_signal(&event)
#endif
#define SAFE_FREE(p) if(p) { free(p); p = NULL; }
//---------------------------------------------------------
PA_MUTEX	mutex_sock_list;
LIST_HEAD(sock_list);

PA_HTHREAD hthd;
static volatile int run = 1;

static PA_MUTEX mutex_pkt_pool;
static int n_free_pkt = 0;
static struct rudp_pkt *free_pkt = NULL;

//static int sockw_r = -1, sockw_s;
struct output_notify {
	struct rudp_socket *s;
	int chno;
};

unsigned int rudp_now = 0;
//==========================================================


#define RO_NORMAL	0
#define RO_FORCE	1
#define RO_REXMT	2   //rexmt as RCT_REXMT timer
#define RO_ONLYONE	3
#define RO_REXMT_FAST 4	//rexmt as duplicated ack received

#define INITIAL_SEQ_NO	0

#define FASTRETRANS	0
#define FASTRETRANS2	1	//multiple packet lost
#define CONGESTED	2

//value for should_ack
#define ACKT_DELAYED	1
#define ACKT_OPENWND	2

#ifdef _DEBUG_RUDP
static char* IP2STR(unsigned int ip, char ips[16])
{
	int len = sprintf(ips, "%d.", ip&0xFF);
	len += sprintf(ips+len, "%d.", (ip>>8)&0xFF);
	len += sprintf(ips+len, "%d.", (ip>>16)&0xFF);
	len += sprintf(ips+len, "%d", (ip>>24)&0xFF);
	return ips;
}

static void _printTime()
{
	char sf[16];
#if defined(ARM_UCOS_LWIP)
#elif defined(WIN32)
	SYSTEMTIME t;
	GetLocalTime(&t);
	sprintf(sf, "%.06f", t.wMilliseconds/1000.0);
	PRINTF("%02d:%02d:%02d.%s  ", t.wHour, t.wMinute, t.wSecond, sf+2);
#else
	struct timeval tv;
	struct tm _tm;
	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &_tm);
	sprintf(sf, "%.06f", tv.tv_usec/1000000.0);
	PRINTF("%02d:%02d:%02d.%s  ", _tm.tm_hour, _tm.tm_min, _tm.tm_sec, sf+2);
#endif
}
#define PHF_FROM	0x10000000
#define PHF_DATA	0x20000000
static void __printHdr(const struct rudp_pcb *pcb, const struct rudp_hdr *phdr, const struct sockaddr_in *pa, int phf, int data_len)
{
	char ip[16];
	_printTime();
	if(phf & PHF_FROM)
	{
		PRINTF("%s.%d > ", IP2STR(pa->sin_addr.s_addr, ip), (int)ntohs(pa->sin_port));
		if(pcb) PRINTF("%s.%d ", IP2STR(pcb->local.sin_addr.s_addr, ip), (int)ntohs(pcb->local.sin_port));
	}
	else
	{
		if(pcb) PRINTF("%s.%d > ", IP2STR(pcb->local.sin_addr.s_addr, ip), (int)ntohs(pcb->local.sin_port));
		PRINTF("%s.%d ", IP2STR(pa->sin_addr.s_addr, ip), (int)ntohs(pa->sin_port));
	}
	PRINTF("c:%d ", phdr->flags.chno);

	if(data_len) 
	{
		PRINTF("P s:%u(%d) ", ntohl(phdr->seqno), data_len);
	}
	else printf(". ");
	if(phdr->flags.syn) PRINTF("syn ");
	if(phdr->flags.ack) PRINTF("ack %u(%d) ", ntohl(phdr->ackno), phdr->flags.n_loss);
	if(phdr->flags.rst) PRINTF("rst ");
	if(phdr->flags.fin) PRINTF("fin ");
	PRINTF("win %d ", (int)WINDOW_NTOH(phdr->flags.window));

	if(pcb)
	{
		PRINTF("[rwnd %d cwnd %d ssth %d", pcb->channel[phdr->flags.chno].sbuf.rwnd, pcb->cwnd, pcb->ssthresh);
		if(phdr->flags.ack && (phf&PHF_FROM))
		{
			PRINTF(" rto %d", pcb->rto);
			PRINTF(" rtw %d", pcb->rtw_size);
			if(phdr->flags.n_loss) PRINTF(" lost %d", phdr->flags.n_loss);
		}
		PRINTF(" una %d", pcb->channel[phdr->flags.chno].sbuf.n_unacked);
		PRINTF("]");
	}
	PRINTF("\n");
#if defined(WIN32)
	fflush(stdout);
#endif
}
static void _printHdr(const struct rudp_pcb *pcb, const struct rudp_hdr *phdr, const struct sockaddr_in *pa)
{
	__printHdr(pcb, phdr, pa, 0, 0);
}
static void _printPkt(const struct rudp_pcb *pcb, const struct rudp_pkt *pkt, int phf, const struct sockaddr_in *pa)
{
	if(pkt->len) { phf |= PHF_DATA; }
	__printHdr(pcb, &pkt->hdr, pa, phf, pkt->len);
}
#else
#define _printHdr(a,b,c)
#define _printPkt(a,b,c,d)
#define _printTime()
#endif

#define DELAY_ACK_MS	100
#define RTT_UINT	200			//accuracy of RTT, ms
#define RTT_MIN		(1000/RTT_UINT)		//count in 1 second.

#define MAX_REXMT_ATTEMPT	6	
static int rudp_backoff[MAX_REXMT_ATTEMPT+1] = { 1, 2, 4, 8, 16, 32, 32/*, 64, 64, 64, 64, 64*/ };
#define MAX_RECONN_ATTEMPT	5
static int conn_backoff[MAX_RECONN_ATTEMPT+1] = { RTT_MIN, 1*RTT_MIN, 2*RTT_MIN, 2*RTT_MIN, 2*RTT_MIN, 2*RTT_MIN };	//s

static struct rudp_pkt *_MBufGetPacket();
static void _MBufPutPacket(struct rudp_pkt *pkt);
static int _ProcessPacket(struct rudp_socket *s, struct rudp_pkt *pkt, const struct sockaddr *from, int from_len);
int _DispatchPacket(struct rudp_socket *s, struct rudp_pkt *pkt, const struct sockaddr *from, int from_len);
static INLINE void _sendPacket(struct rudp_socket *s, struct rudp_channel *pch, struct rudp_pkt *pkt, int opt);
static void _sendReset(struct rudp_socket *s, const struct sockaddr *to);
static /*INLINE */void _sendHeader(struct rudp_socket *s, struct rudp_hdr *phdr);
static void _sendSyn(struct rudp_socket *s);
static void _sendSynAck(struct rudp_socket *s);
static void _sendAck(struct rudp_socket *s, int chno);
static void _sendFin(struct rudp_socket *s);

typedef void (*TimerHandler)(struct rudp_socket *s);
static void _timerProc(TimerHandler handler);
static void _handleTimer500ms(struct rudp_socket *s);
static void _handleTimer200ms(struct rudp_socket *s);

static struct rudp_pkt *_MBufGetPacket()
{
	struct rudp_pkt *p;

	PA_MutexLock(mutex_pkt_pool);
	if(free_pkt)
	{
		p = free_pkt;
		free_pkt = free_pkt->next;
		n_free_pkt --;
	}
	else
	{
		p = (struct rudp_pkt*)malloc(sizeof(struct rudp_pkt));
		if(!p) {
			PA_MutexUnlock(mutex_pkt_pool);
			return NULL;
		}
	}
	p->next = NULL;
	p->hdr.u32_flags = 0;
	p->hdr.flags.rudp = RUDP_HEADER_TAG;
	p->len = 0;
	p->trans = 0;
	p->pdata = p->data;
	PA_MutexUnlock(mutex_pkt_pool);

	return p;
}
static void _MBufPutPacket(struct rudp_pkt *pkt)
{
	PA_MutexLock(mutex_pkt_pool);
	if(n_free_pkt > 256)
	{
		free(pkt);
	}
	else
	{
		pkt->next = free_pkt;
		free_pkt = pkt;
		n_free_pkt++;
	}
	PA_MutexUnlock(mutex_pkt_pool);
}

static struct rudp_socket *_AllocRudpSocket()
{
	struct rudp_socket *sock = (struct rudp_socket*)calloc(sizeof(struct rudp_socket), 1);
	sock->tag = RUDP_SOCKET_TAG;

	sock->state = RS_CLOSED;
	sock->rcvbuf_sz = DEFAULT_RCVBUF_SIZE;

	PA_MutexInit(sock->mutex_r);
	PA_MutexInit(sock->mutex_w);
#if defined(WIN32) || defined(ARM_UCOS_LWIP)
	PA_EventInit(sock->event_r);
	PA_EventInit(sock->event_w);
#else
	pthread_cond_init(&sock->event_r, NULL);
	pthread_cond_init(&sock->event_w, NULL);
#endif

	INIT_LIST_HEAD(&sock->inst_list);
	INIT_LIST_HEAD(&sock->listen_queue);
	INIT_LIST_HEAD(&sock->accepted_list);
	return sock;
}

static struct rudp_pcb *_AllocRudpPcb(uint32_t rcvbuf_size, uint32_t initial_seqno, uint32_t peer_initial_seqno, int rawnd)
{
	struct rudp_pcb *pcb;
	int i;
	pcb = (struct rudp_pcb*)calloc(sizeof(struct rudp_pcb), 1);
	for(i=0; i<MAX_PHY_CHANNELS; i++)
	{
		struct sndbuf *psbuf;
		struct rcvbuf *prbuf;
		
		psbuf = &pcb->channel[i].sbuf;
		psbuf->seqno = initial_seqno;
		psbuf->max_pkts = DEFAULT_SNDBUF_SIZE;
		psbuf->rwnd = psbuf->rawnd = rawnd;

		prbuf = &pcb->channel[i].rbuf;
		prbuf->expected_seqno = prbuf->first_seq = peer_initial_seqno;
		prbuf->q_size = rcvbuf_size;;
		prbuf->pkt_q = (struct rudp_pkt**)calloc(sizeof(void*), rcvbuf_size);
		prbuf->win = prbuf->q_size - 1;
	}
	pcb->cwnd = 2;
	pcb->rtw_size = rawnd/2;//8;
	pcb->rwin_size = rawnd;
	pcb->ssthresh = rawnd;

	pcb->srtt = 0;
	pcb->sdev = 3;
	pcb->rto = 6;	//As [Jacobson 1988] rto = srtt + 2*sdev, but it seems too large for us

	return pcb;
}

static void _terminateSocketInternally(struct rudp_socket *s, int err)
{
	if(s->state == RS_DEAD) return;

	PA_MutexLock(s->mutex_r);
	PA_MutexLock(s->mutex_w);

	s->state = RS_DEAD;
	s->err = err;

	if(s->pcb)
	{
		int i;
		for(i=0; i<MAX_PHY_CHANNELS; i++)
		{
			struct rudp_pkt *c;
			struct rcvbuf *prb;

			c = s->pcb->channel[i].sbuf.first;
			while(c)
			{
				struct rudp_pkt *p = c;
				c = c->next;
				_MBufPutPacket(p);
			}

			prb = &s->pcb->channel[i].rbuf;
			for(; prb->head != prb->tail; prb->head = (prb->head+1)%prb->q_size)
				if(prb->pkt_q[prb->head])
					_MBufPutPacket(prb->pkt_q[prb->head]);
			free(prb->pkt_q);
		}
		free(s->pcb);
		s->pcb = NULL;
	}

	SETEVENT(s->event_r);
	SETEVENT(s->event_w);

	PA_MutexUnlock(s->mutex_w);
	PA_MutexUnlock(s->mutex_r);

#if 0
	//Still in listening queue
	if(list_empty(&s->inst_list) && !list_empty(&s->listen_queue))
	{
		list_del(&s->listen_queue);
		free(s);
	}
#endif
	//dbg_msg("################## _terminateSocketInternally ##########\n");
}

/// \brief Should be called with "mutex_sock_list" hold
//  \param err error code for the reason to cleanup this socket
static void _CleanupSocket(struct rudp_socket *s, int err)
{
	if(s->state == RS_DEAD) return;

	_terminateSocketInternally(s, err);

	PA_MutexUninit(s->mutex_r);
	PA_MutexUninit(s->mutex_w);
#if defined(WIN32) || defined(ARM_UCOS_LWIP)
	PA_EventUninit(s->event_r);
	PA_EventUninit(s->event_w);
#else
	pthread_cond_destroy(&s->event_r);
	pthread_cond_destroy(&s->event_w);
#endif

	if(!list_empty(&s->listen_queue))
	{
		struct list_head *pp, *qq;
		list_for_each_safe(pp, qq, &s->listen_queue)
		{
			struct rudp_socket *sl = list_entry(pp, struct rudp_socket, listen_queue);
			_CleanupSocket(sl, 0);
			list_del(pp);
			free(sl);
		}
	}

	s->state = RS_DEAD;
	//s->tag = 0;
}

static void _CleanAndFreeSocket(struct rudp_socket *s)
{
	if(list_empty(&s->inst_list)) //accepted
	{
		list_del(&s->accepted_list);
	}
	else 
	{
		list_del(&s->inst_list);
		if(list_empty(&s->accepted_list))
			PA_SocketClose(s->udp_sock);
		else
		{
			struct rudp_socket *aa = list_entry(s->accepted_list.next, struct rudp_socket, accepted_list);
			INIT_LIST_HEAD(&aa->inst_list);
			list_add_tail(&aa->inst_list, &sock_list);
		}
	}

	if(s->state != RS_DEAD) _CleanupSocket(s, 0);

	free(s);
}

void _timerProc(TimerHandler handler)
{
	struct list_head *p, *q, *pp, *qq;
	//PA_MutexLock(mutex_sock_list);
	list_for_each_safe(p, q, &sock_list)
	{
		struct rudp_socket *s, *ss;
		s = list_entry(p, struct rudp_socket, inst_list);

		if(s->state == RS_DEAD) continue;

		list_for_each_safe(pp, qq, &s->accepted_list)
		{
			ss = list_entry(pp, struct rudp_socket, accepted_list);
			handler(ss);
		}
		list_for_each_safe(pp, qq, &s->listen_queue)	//a socket in listen_queue may be removed due to timeout
		{
			ss = list_entry(pp, struct rudp_socket, listen_queue);
			handler(ss);
		}

		handler(s);
	}
	//PA_MutexUnlock(mutex_sock_list);
}

static void _congestionAvoidance(struct rudp_socket *s)
{
	if(s->pcb->cwnd < s->pcb->ssthresh)
	{//slow start 
		s->pcb->cwnd++;
#if 1
		s->pcb->ca_cnt++;
		if(s->pcb->ca_cnt >= s->pcb->rwin_size)
#endif
		if(s->pcb->rtw_size < s->pcb->rwin_size)
			s->pcb->rtw_size++;
	}
	else
	{//congestion avoidance, increase cwnd slowly
		s->pcb->ca_cnt++;
		if(s->pcb->ca_cnt >= s->pcb->cwnd)
		{
			s->pcb->ca_cnt = 0;
			if(s->pcb->cwnd < s->pcb->rwin_size)
			{
				s->pcb->cwnd++;
				//s->pcb->ssthresh++; ???
			}
			if(s->pcb->rtw_size < s->pcb->rwin_size)
				s->pcb->rtw_size++;
		}
	}
}

static void _congestionDetected(struct rudp_socket *s, int chno, int what)
{
	//int rawnd, i;
	struct rudp_pcb *pcb;

	//for(i=rawnd=0; i<MAX_CHANNELS; i++) rawnd += pcb->channel[chno].sbuf.rawnd;

	pcb = s->pcb;
	pcb->ssthresh = min(pcb->channel[chno].sbuf.rawnd, pcb->cwnd)/2;
	if(pcb->ssthresh < 2) pcb->ssthresh = 2;

	if(what == CONGESTED) pcb->cwnd = 1; 
	else pcb->cwnd = pcb->ssthresh + 3;

	switch(what)
	{
	case CONGESTED:
	case FASTRETRANS2:
		pcb->rtw_size >>= 1; 
		if(pcb->rtw_size < 8) pcb->rtw_size = 8; 
		break;

	case FASTRETRANS:
		pcb->rtw_size -= pcb->rtw_size >> 2;
		if(pcb->rtw_size < 8) pcb->rtw_size = 8;
		break;
	}
}

static INLINE unsigned int _calcCurWnd(struct rudp_socket *s, struct sndbuf *psbuf)
{
	//if receiver's rawnd is 0, still send one more packet then transmission can be
	//started again by re-transfer timer, even when the receiver's OPENWND ACK(s) are lost
	return min(s->pcb->cwnd, psbuf->rwnd); 

	//return min(min(s->pcb->cwnd, psbuf->rwnd),psbuf->rawnd);
	//return psbuf->rwnd;
}

/** Output a packet
 *
 *  mutex_w shall be hold before call _RudpOutput
 *
 *  return: 1 - if a packet is sent; otherwise, no packet is sent
 */
static int _RudpOutput(struct rudp_socket *s, int chno, int opt)
{
	struct sndbuf *psbuf;
	struct rcvbuf *prbuf;
	struct rudp_channel *pch;

	if(s->tag != RUDP_SOCKET_TAG) return -1;
	if(s->state <= RS_CLOSED) return -1;

	pch = &s->pcb->channel[chno];
	psbuf = &pch->sbuf;
	prbuf = &pch->rbuf;

	//if(pch->congested && opt == RO_NORMAL) return;
	if(opt == RO_REXMT || opt == RO_REXMT_FAST)	//packets are queued before
	{
		struct rudp_pkt *pkt = opt == RO_REXMT_FAST ? psbuf->rexmt : psbuf->first;
		if(!pkt) return 0;
		if(prbuf->should_ack == ACKT_DELAYED)
		{
			prbuf->should_ack = 0;
			pkt->hdr.flags.ack = 1;
			pkt->hdr.ackno = ntohl(prbuf->expected_seqno);
			pkt->hdr.flags.n_loss = prbuf->n_lost;
			//if(psbuf->first->trans)	psbuf->first->hdr.crc32 = calc_crc32(0, (char*)&psbuf->first->hdr, offsetof(struct rudp_hdr, crc32));
		}
		_sendPacket(s, pch, pkt, opt);
		pkt->hdr.flags.ack = 0;
		return 1;
	}
	
	if(((prbuf->should_ack == ACKT_DELAYED) && !psbuf->first) || prbuf->should_ack == ACKT_OPENWND)
	{
		_sendAck(s, chno);
		prbuf->should_ack = 0;

		return 0;
	}

	if(opt == 0 && pch->sbuf.rawnd == 0)
	{
		if(pch->timer[RCT_REXMT] == 0)
			pch->timer[RCT_PERSIST] = s->pcb->rto * rudp_backoff[pch->sbuf.first?pch->sbuf.first->trans:0];
		//signalOutput(s, chno);
	}
	else
	{
		struct rudp_pkt *p;

		p = psbuf->not_sent;
		pch->timer[RCT_PERSIST] = 0;
		if(p && (opt == RO_FORCE /*|| prbuf->should_ack == ACKT_DELAYED */
					|| _calcCurWnd(s, psbuf) > psbuf->n_unacked
						/* && psbuf->n_unacked < s->pcb->rtw_size*/))
			//psbuf->n_unacked < psbuf->rawnd) )
		{
			if(prbuf->should_ack == ACKT_DELAYED)
			{
				prbuf->should_ack = 0;
				p->hdr.flags.ack = 1;
				p->hdr.ackno = ntohl(prbuf->expected_seqno);
				p->hdr.flags.n_loss = prbuf->n_lost;
				//if(p->trans) p->hdr.crc32 = calc_crc32(0, (char*)&p->hdr, offsetof(struct rudp_hdr, crc32));
			}
			//psbuf->n_unacked++;
			_sendPacket(s, pch, p, opt);
			p->hdr.flags.ack = 0;
			p = p->next;
			//psbuf->not_sent = p = p->next;
			if(p && p->seqno < psbuf->not_sent->seqno)
			{
				dbg_msg("#####################################################\n");
			}
			psbuf->not_sent = p;

			//if(psbuf->rwnd > 0) 
			psbuf->rwnd--;
			return 1;
			if(opt == RO_ONLYONE) return 1;

			if(_calcCurWnd(s, psbuf) && p)
			{
				//signalOutput(s, chno);
				//struct output_notify notify = { s, chno };
				//PA_Send(sockw_s, &notify, sizeof(notify), 0);
			}
		}
	}

	return 0;
}

void _handleTimer500ms(struct rudp_socket *s)
{
	int i, j;
	struct rudp_pcb *pcb;

	if(s->state == RS_LISTEN || s->state == RS_DEAD) return;
	pcb = s->pcb;
	if(pcb)
	{
		int sbuf_is_empty = 1;
		//rudp_now ++;

		PA_MutexLock(s->mutex_w);
		for(i=0; i<MAX_PHY_CHANNELS; i++)
		{
			struct rudp_channel *pch = &pcb->channel[i];
			if(pch->sbuf.first) sbuf_is_empty = 0;
			for(j=0; j<RCT_CNT; j++)
			{
				if(pch->timer[j] == 0) continue;
				pch->timer[j] --;
				if(pch->timer[j] == 0)
				{
					switch(j)
					{
					case RCT_PERSIST:
						dbg_msg("Persist timeout.\n");
						_RudpOutput(s, i, RO_FORCE);
						break;
					case RCT_REXMT:
						if(pch->sbuf.first)
						{
							if(pch->sbuf.first->trans >= MAX_REXMT_ATTEMPT)
							{
								PA_MutexUnlock(s->mutex_w);
								_CleanupSocket(s, ERUDP_TIMEOUTED);
								s->state = RS_DEAD;
								s->err = ERUDP_TIMEOUTED;
								return;
							}
							else if(pch->sbuf.first->trans)
							{
								_congestionDetected(s, i, CONGESTED);
								_printTime(); dbg_msg("congested: cwnd=%d, ssthresh=%d\n", pcb->cwnd, pcb->ssthresh);
								_congestionAvoidance(s);
								_RudpOutput(s, i, RO_REXMT);
								pch->congested = 1;
								pch->sbuf.pkt_rttm_start = pch->sbuf.not_sent;
							}
							else
								_RudpOutput(s, i, 0);
						}
						break;
					}
				}
			}
		}
		if(sbuf_is_empty && s->state == RS_FIN_QUEUED)
		{
			_sendFin(s);
			s->state = RS_FIN_WAIT_1;
			s->timer[RT_KEEP] = RTT_MIN * RTV_KEEP_CLOSE;
		}
		PA_MutexUnlock(s->mutex_w);
	}

	for(i=0; i<RT_CNT; i++)
	{
		if(s->timer[i] == 0) continue;

		s->timer[i]--;
		if(s->timer[i] == 0)
		{
			switch(i)
			{
			case RT_KEEP:	//for connecting timeout
				if(s->state == RS_SYN_RCVD || s->state == RS_SYN_SENT)
				{
					if(s->pcb->retr_cnt >= MAX_RECONN_ATTEMPT)
					{
						if(list_empty(&s->inst_list))
						{
							list_del(&s->listen_queue);
							INIT_LIST_HEAD(&s->listen_queue);
							_CleanupSocket(s, ERUDP_TIMEOUTED);
							free(s);
						}
						else
						{
							//_CleanupSocket(s, ERUDP_TIMEOUTED);
							s->err = ERUDP_TIMEOUTED;
							s->state = RS_CLOSED;
							SETEVENT(s->event_w);
						}
						return;
					}
					else
					{
						s->timer[RT_KEEP] = conn_backoff[++s->pcb->retr_cnt];
						if(s->state == RS_SYN_SENT)
							_sendSyn(s);
						else
							_sendSynAck(s);
					}
				}
				else if(s->state >= RS_FIN_QUEUED)
				{
					dbg_msg("clean and free %p\n", s);
					_CleanAndFreeSocket(s);
				}
				break;
			case RT_2MSL:
				break;
			}
		}
	}
}

void _handleTimer200ms(struct rudp_socket *s)
{
	if(s->pcb)// && (s->pcb->r_flags & RUDPF_DELAYACK))
	{
		int i;
		for(i=0; i<MAX_PHY_CHANNELS; i++)
		{
			struct rcvbuf *prb = &s->pcb->channel[i].rbuf;
			if(prb->should_ack)
			{
				//_printTime(); dbg_msg("delayed ack.\n");
				PA_MutexLock(s->mutex_w);
				_RudpOutput(s, i, 0);
				//signalOutput(s, i);
				PA_MutexUnlock(s->mutex_w);
			}
		}
	}
}
void _sendHeader(struct rudp_socket *s, struct rudp_hdr *phdr)
{
	_printHdr(s->pcb, phdr, &s->pcb->peer);
	phdr->crc32 = calc_crc32(0, (char*)phdr, offsetof(struct rudp_hdr, crc32));
 	PA_SendTo(s->udp_sock, phdr, sizeof(struct rudp_hdr), 0, 
			s->connected?NULL:(struct sockaddr*)&s->pcb->peer, 
			sizeof(struct sockaddr));
}

void _sendPacket(struct rudp_socket *s, struct rudp_channel *pch, struct rudp_pkt *pkt, int opt)
{
	if(pch->timer[RCT_REXMT] == 0)
		pch->timer[RCT_REXMT] = s->pcb->rto * rudp_backoff[pkt->trans];
	pkt->ts = rudp_now;
	//dbg_msg("............. seq %u, ts %d...........", pkt->seqno, rudp_now);
	pkt->hdr.flags.window = WINDOW_HTON(pch->rbuf.win);
	if(opt != RO_REXMT_FAST)
	{
		if(!pkt->trans) pch->sbuf.n_unacked++;
		pkt->trans ++; 
		if(pkt->trans >= MAX_REXMT_ATTEMPT) pkt->trans = MAX_REXMT_ATTEMPT;
	}
	pkt->hdr.crc32 = calc_crc32(0, (char*)&pkt->hdr, offsetof(struct rudp_hdr, crc32));

	_printPkt(s->pcb, pkt, PHF_DATA|pch->sbuf.rwnd, &s->pcb->peer);
	PA_SendTo(s->udp_sock, &pkt->hdr, sizeof(struct rudp_hdr) + pkt->len, 0, 
			s->connected?NULL:(struct sockaddr*)&s->pcb->peer, 
			sizeof(struct sockaddr));
}
void _sendSyn(struct rudp_socket *s)
{
	struct rudp_hdr hdr;
	hdr.u32_flags = 0;
	hdr.flags.rudp = RUDP_HEADER_TAG;
	hdr.flags.syn = 1;
	hdr.seqno = htonl(s->pcb->channel[0].sbuf.seqno);
	hdr.ackno = 0;
	hdr.flags.window = WINDOW_HTON(s->pcb->channel[0].rbuf.win);
	_sendHeader(s, &hdr);
}
void _sendSynAck(struct rudp_socket *s)
{
	struct rudp_hdr hdr;
	hdr.u32_flags = 0;
	hdr.flags.rudp = RUDP_HEADER_TAG;
	hdr.flags.syn = 1;
	hdr.seqno = htonl(s->pcb->channel[0].sbuf.seqno);
	hdr.flags.ack = 1;
	hdr.ackno = htonl(s->pcb->channel[0].rbuf.expected_seqno);
	hdr.flags.window = WINDOW_HTON(s->pcb->channel[0].rbuf.win);
	_sendHeader(s, &hdr);
}
//only ack flag
void _sendEmptyAck(struct rudp_socket *s, int chno)
{
	struct rudp_hdr hdr;
	hdr.u32_flags = 0;
	hdr.flags.rudp = RUDP_HEADER_TAG;
	hdr.seqno = 0;
	hdr.flags.chno = chno;
	hdr.flags.ack = 1;
	hdr.ackno = 0;
	hdr.flags.window = 0;
	_sendHeader(s, &hdr);
}

//ack without data
void _sendAck(struct rudp_socket *s, int chno)
{
	struct rudp_hdr hdr;
	struct rcvbuf *pr = &s->pcb->channel[chno].rbuf;

	hdr.u32_flags = 0;
	hdr.flags.rudp = RUDP_HEADER_TAG;
	hdr.flags.ack = 1;
	hdr.flags.chno = chno;
	hdr.seqno = htonl(s->pcb->channel[chno].sbuf.seqno);
	hdr.ackno = htonl(pr->expected_seqno);
	hdr.flags.n_loss = pr->n_lost;
	hdr.flags.window = WINDOW_HTON(pr->win);
	_sendHeader(s, &hdr);

	pr->acked_seqno = pr->expected_seqno;
	pr->should_ack = 0;
}
void _sendFin(struct rudp_socket *s)
{
	struct rudp_hdr hdr;
	hdr.u32_flags = 0;
	hdr.flags.rudp = RUDP_HEADER_TAG;
	hdr.flags.fin = 1;
	hdr.seqno = 0;
	hdr.ackno = 0;
	hdr.flags.window = 0;
	_sendHeader(s, &hdr);
}


void _updateRTO(struct rudp_socket *s, int rtt)
{
	int rto0 = s->pcb->rto;
#if 0
	/* [Jacobson 1988],  refresh rto */
	int drtt = rtt - s->pcb->srtt;
	if(drtt > 0)
	{
		s->pcb->srtt += drtt >> 3;
		if(drtt > s->pcb->sdev)
			s->pcb->sdev += (drtt - s->pcb->sdev) >> 2;
		else
			s->pcb->sdev -= (s->pcb->sdev - drtt) >> 2;
	}
	else
	{
		drtt = -drtt;
		s->pcb->srtt -= drtt >> 3;
		if(drtt > s->pcb->sdev)
			s->pcb->sdev += (drtt - s->pcb->sdev) >> 2;
		else
			s->pcb->sdev -= (s->pcb->sdev - drtt) >> 2;
	}
	s->pcb->rto = s->pcb->srtt + (s->pcb->sdev > 0) ?(s->pcb->sdev << 2):(-s->pcb->sdev << 2);
	if(s->pcb->rto < 2) s->pcb->rto = 2;
	dbg_msg("rtt = %d, rto = %d\n", rtt, s->pcb->rto);
#else
	if(rtt < RTT_MIN) rtt = RTT_MIN;
	//if(rtt < 2) rtt = 2;
	s->pcb->rto = rtt;
#endif
	if(s->pcb->rto - rto0 > 0)
	{
		s->pcb->cwnd -= s->pcb->cwnd >> 2;
		//s->pcb->rtw_size -= s->pcb->rtw_size >> 2;
	}
}

INLINE BOOL _isPacketValid(struct rudp_pkt *pkt)
{
#ifdef _DEBUG
	return (calc_crc32(0, (char*)&pkt->hdr, offsetof(struct rudp_hdr, crc32)) == pkt->hdr.crc32)?TRUE:(printf("Invalid packet!\n"),FALSE);
#else
	return calc_crc32(0, (char*)&pkt->hdr, offsetof(struct rudp_hdr, crc32)) == pkt->hdr.crc32;
#endif
}

int _DispatchPacket(struct rudp_socket *s, struct rudp_pkt *pkt, const struct sockaddr *from, int from_len)
{
	struct list_head *pp, *qq;
	struct rudp_socket *sa;
	struct sockaddr_in *sp, *sf;

	sf = (struct sockaddr_in*)from;

	/* We must search each queue to make sure there is no duplicated connection for a listening socket */

	if(s->state == RS_LISTEN)
	{
		list_for_each_safe(pp, qq, &s->listen_queue)
		{
			sa = list_entry(pp, struct rudp_socket, listen_queue);
			if(!sa->pcb) continue;
			sp = (struct sockaddr_in*)&sa->pcb->peer;
			if(sp->sin_addr.s_addr == sf->sin_addr.s_addr && sp->sin_port == sf->sin_port)
			{
				int check_again;
				if(pkt->hdr.flags.rst)
				{
					list_del(&sa->listen_queue);
					INIT_LIST_HEAD(&sa->listen_queue);
					_CleanupSocket(sa, ERUDP_RESETED);
					free(sa);
					return 0;
				}

				check_again = 0;
				if(pkt->hdr.flags.ack && sa->state == RS_SYN_RCVD)
					check_again = 1;
				if(_ProcessPacket(sa, pkt, from, from_len) < 0)
					_MBufPutPacket(pkt);
				if(check_again && sa->state == RS_ESTABLISHED)
					SETEVENT(s->event_r);
				return 0;
			}
		}
	}

	list_for_each(pp, &s->accepted_list)
	{
		sa = list_entry(pp, struct rudp_socket, accepted_list);
		if(sa->state >= RS_ESTABLISHED)
		{
			sp = (struct sockaddr_in*)&sa->pcb->peer;
			if(sp->sin_addr.s_addr == sf->sin_addr.s_addr && sp->sin_port == sf->sin_port)
			{
				if(_ProcessPacket(sa, pkt, from, from_len) < 0)
					_MBufPutPacket(pkt);
				return 0;
			}
		}
	}

	/* It's time to Me */
	if(s->state == RS_LISTEN/*listening socket*/ ||
			( s->pcb && (s->connected/*client*/
					|| (((sp = (struct sockaddr_in*)&s->pcb->peer), sp->sin_addr.s_addr == sf->sin_addr.s_addr)
						&& (sp->sin_port == sf->sin_port)/*accepted(listening socket is closed)*/) )
				  )
	  )
	{
		if(_ProcessPacket(s, pkt, from, from_len) < 0)
			_MBufPutPacket(pkt);
		return 0;
	}

	/* Nobody want this packet */
	if(!(pkt->hdr.flags.rst && s->state <= 0)) 
		_sendReset(s, from);
	_MBufPutPacket(pkt);
	return 0;
}

static int _PPState_Established(struct rudp_socket *s, struct rudp_pkt *pkt, const struct sockaddr *from, int from_len)
{
	struct rudp_channel *pch;
	struct sndbuf *psbuf;
	struct rcvbuf *prbuf;
	int old_rawnd;
	int chno;

	if(pkt->hdr.flags.syn) 
	{ 
		if(pkt->hdr.flags.ack)
			_sendAck(s, 0);
		else
			_sendReset(s, from); 
		return -1; 
	}
	if(pkt->hdr.flags.fin)
	{
		int i, ii;
		PA_MutexLock(s->mutex_w);
		s->state = RS_CLOSE_WAIT;
		_sendAck(s, 0);
		s->timer[RT_KEEP] = RTT_MIN * RTV_KEEP_CLOSE;

		//
		// Cleanup sndbuf
		//
		for(i=0; i<MAX_PHY_CHANNELS; i++)
		{
			struct sndbuf *psb = &s->pcb->channel[i].sbuf;
			struct rudp_pkt *c = psb->first;
			while(c)
			{
				struct rudp_pkt *p = c;
				c = c->next;
				_MBufPutPacket(p);
			}
			memset(psb, 0, sizeof(struct sndbuf));
			for(ii=0; ii<RCT_CNT; ii++) s->pcb->channel[i].timer[ii] = 0;
		}
		for(i=0; i<RT_CNT; i++) s->timer[i] = 0;

		PA_MutexUnlock(s->mutex_w);
		goto _checkdata;
	}

	chno = pkt->hdr.flags.chno;
	pch = &s->pcb->channel[chno];
	psbuf = &pch->sbuf;
	old_rawnd = psbuf->rawnd;
	psbuf->rawnd = WINDOW_NTOH(pkt->hdr.flags.window);
	//psbuf->rwnd += old_rawnd - psbuf->rawnd;
	psbuf->rwnd = psbuf->rawnd - psbuf->n_unacked;
	if(psbuf->rwnd < 0) psbuf->rwnd = 0;


	if(pkt->hdr.flags.ack && psbuf->first)
	{
		uint32_t ackno = ntohl(pkt->hdr.ackno);

		PA_MutexLock(s->mutex_w);
		if(SEQ_LE(ackno, psbuf->first->seqno)/* && !pch->congested*/)	//duplicated ACK
		//if(ackno == psbuf->first->seqno) //duplicated ACK
		{
			if(old_rawnd == 0 && psbuf->rawnd > 0)	//Recevier's window opened
			{
				_RudpOutput(s, pkt->hdr.flags.chno, RO_FORCE);
				PA_MutexUnlock(s->mutex_w);
				goto _checkdata;
			}

			psbuf->dup_ack++;
			if(psbuf->dup_ack >= 3)
			{
				if(psbuf->dup_ack == 3)
				{
					psbuf->rlost = pkt->hdr.flags.n_loss;
					if(psbuf->rlost == 0) psbuf->rlost = 1;
					psbuf->rexmt = psbuf->first;
					_congestionDetected(s, chno, psbuf->rlost>1?FASTRETRANS2:FASTRETRANS);
					_printTime(); dbg_msg("duplicated ACKs(%d): rlost=%d, cwnd=%d, ssthresh=%d\n", 
							ackno, psbuf->rlost, s->pcb->cwnd, s->pcb->ssthresh);

					pch->timer[RCT_REXMT] = s->pcb->rto * rudp_backoff[0] + 1;

					psbuf->pkt_rttm_start = psbuf->not_sent;
					psbuf->fastretr_end_seq = psbuf->first->seqno + psbuf->n_unacked - 1;
				}

				if(psbuf->rlost && psbuf->rexmt)
				{
					_RudpOutput(s, pkt->hdr.flags.chno, RO_REXMT_FAST);
					psbuf->rexmt = psbuf->rexmt->next;
					psbuf->rlost --; //if(psbuf->rlsot == 0) psbuf->rexmt = NULL;
				}
				else if(psbuf->dup_ack > 3)
				{
					s->pcb->cwnd ++;
					// Send one new packet every two duplicated acks.
					// Because each ack means a packet(very possible being valid) is received by peer,
					// so we can inject new packet into network
					if(psbuf->dup_ack & 1) _RudpOutput(s, pkt->hdr.flags.chno, RO_FORCE);

					if(s->pcb->cwnd > s->pcb->ssthresh)
						s->pcb->cwnd = s->pcb->ssthresh;
					//psbuf->dup_ack = 0;
				}
			}
			else
				_RudpOutput(s, pkt->hdr.flags.chno, RO_ONLYONE);
		}
		else// if(SEQ_GT(ackno, psbuf->first->seqno))
		{
			struct rudp_pkt *p, *p2;
			int fast_rxmt_end = 0;	//fast retransmission

			p = psbuf->first;
			//while(p && p->seqno != ackno && psbuf->n_unacked)
			while(p && p->seqno < ackno)
			{//remove acked packets
				if(psbuf->pkt_rttm_start == p)
					psbuf->pkt_rttm_start = NULL;
				if(p->trans == 1 && psbuf->pkt_rttm_start == NULL)
					_updateRTO(s, rudp_now - p->ts);

				if(p->seqno == psbuf->fastretr_end_seq && psbuf->dup_ack >= 3)
					fast_rxmt_end = 1;
				p2 = p;
				p = p->next;
				_MBufPutPacket(p2);
				psbuf->n_unacked --;
				assert(psbuf->n_unacked>=0);
				psbuf->n_pkt --;
				psbuf->rwnd ++;

				// Slow start && congestion avoidance
				_congestionAvoidance(s);

				// If recovery from congestion, or, after a fast retransmission,
				// there's another hole in the unacked queue, continue performing
				// fast retransmission.
				// Otherwise, stop fast recovery
				if(psbuf->rlost && psbuf->rexmt)
				{
					_RudpOutput(s, pkt->hdr.flags.chno, RO_REXMT_FAST);
					psbuf->rexmt = psbuf->rexmt->next;
					psbuf->rlost --; //if(psbuf->rlsot == 0) psbuf->rexmt = NULL;
				}
				else if(pch->congested)
				{
					psbuf->dup_ack ++;
					if(psbuf->dup_ack & 1) _RudpOutput(s, pkt->hdr.flags.chno, RO_FORCE);

					if(s->pcb->cwnd > s->pcb->ssthresh)
					{
						s->pcb->cwnd = s->pcb->ssthresh;
						pch->congested = 0;
					}
				}
				else
				{
					psbuf->dup_ack = 0;
					if(old_rawnd == 0 && psbuf->rawnd > 0)	//Recevier's window opened
						_RudpOutput(s, pkt->hdr.flags.chno, RO_FORCE);
					else
						while(_RudpOutput(s, pkt->hdr.flags.chno, 0));
				}
			}
			psbuf->first = p;

			if(!p)
			{
				psbuf->pkt_rttm_start = psbuf->not_sent = psbuf->last = NULL; 	//enable rtt measurement
				pch->timer[RCT_REXMT] = 0;

				assert(psbuf->n_pkt==0);
			}
			else
				pch->timer[RCT_REXMT] = s->pcb->rto * rudp_backoff[pkt->trans];

			//signalOutput(s, pkt->hdr.flags.chno);
			SETEVENT(s->event_w);
		}
		PA_MutexUnlock(s->mutex_w);
	}

_checkdata:
	if(pkt->len)	//length of data > 0
	{
		uint32_t pos, seqno;
		int delta;

		PA_MutexLock(s->mutex_r);
		seqno = ntohl(pkt->hdr.seqno);
		prbuf = &pch->rbuf;

		delta = (int)(seqno - prbuf->first_seq);
		if(delta < 0) //old packet, just ignore it
		{ 
			//PA_MutexUnlock(s->mutex_r);
			dbg_msg("packet %d is ignored, expected_seqno=%d\n", pkt->seqno, prbuf->expected_seqno);
			goto _sendack_and_discard_pkt;
		}
		if(delta < prbuf->q_size-1) //in the buffer
		{
			pos = (prbuf->head + delta)%prbuf->q_size;
			if(prbuf->pkt_q[pos])	//duplicated
			{
				dbg_msg("packet %d is duplicated.\n", pkt->seqno);
				goto _sendack_and_discard_pkt;
			}

			prbuf->pkt_q[pos] = pkt;

			//update pointers & window
			if((prbuf->tail + prbuf->q_size - prbuf->head)%prbuf->q_size <= 
					(pos + prbuf->q_size - prbuf->head)%prbuf->q_size)
				prbuf->tail = (pos+1)%prbuf->q_size;
			prbuf->win = (prbuf->q_size + prbuf->head - prbuf->tail - 1)%prbuf->q_size;

			if(prbuf->loss == pos) 	//just the one we expected
			{
				//move "loss" to next empty slot, if any
				while(prbuf->loss != prbuf->tail && prbuf->pkt_q[prbuf->loss])
				{
					prbuf->loss = (prbuf->loss + 1) % prbuf->q_size;
					prbuf->expected_seqno++;
				}
				prbuf->should_ack = ACKT_DELAYED;
				SETEVENT(s->event_r);
			}

			//(re-)calculate the size of (next, or current) hole.
			{
				int loss = prbuf->loss, n_lost=0;
				while(loss != prbuf->tail && !prbuf->pkt_q[loss] && n_lost < MAX_LOSS_REPORT)
				{
					loss = (loss + 1) % prbuf->q_size;
					n_lost++;
				}
				prbuf->n_lost = n_lost;
				if(n_lost) prbuf->should_ack = 0;
			}

			if(!prbuf->should_ack || 
					((prbuf->should_ack == ACKT_DELAYED) && ((prbuf->expected_seqno - prbuf->acked_seqno) >= 3))
				)
			{
				_sendAck(s, pkt->hdr.flags.chno);
				prbuf->should_ack = 0;
			}

			PA_MutexUnlock(s->mutex_r);
			return 0;
		}
		else
			dbg_msg("exceeds receiver's buffer: %d\n", pkt->seqno);
_sendack_and_discard_pkt:
		PA_MutexUnlock(s->mutex_r);
		_sendAck(s, pkt->hdr.flags.chno);
		return -1;
	}
	else if(!pkt->hdr.flags.ack)	//What's this? fin?
	{
		if(s->state == RS_CLOSE_WAIT)
		{
			PA_MutexLock(s->mutex_r);
			s->err = ERUDP_PEER_CLOSED;
			SETEVENT(s->event_r);
			PA_MutexUnlock(s->mutex_r);
		}
		else
		{
			PA_MutexLock(s->mutex_w);
			_RudpOutput(s, pkt->hdr.flags.chno, 0);
			PA_MutexUnlock(s->mutex_w);
		}
		return -1;
	}

	return -1;
}


/// \brief Process packet
//  @return -1 -- if the packet will be released
int _ProcessPacket(struct rudp_socket *s, struct rudp_pkt *pkt, const struct sockaddr *from, int from_len)
{
	struct rudp_channel *pch;
	struct sndbuf *psbuf;

	if((pkt->hdr.flags.rst) && s->state != RS_LISTEN)
	{
		if(s->state == RS_SYN_SENT) 
		{//To support simultanous connection, ignore it
			//s->state = RS_CLOSED;
			//s->err = ERUDP_RESETED;
		}
		else
		{
			_terminateSocketInternally(s, ERUDP_RESETED);
			return -1;
		}
	}

	switch(s->state)
	{
		case RS_CLOSED:
			if(s->err) return -1;
			if((s->flags & RF_ADHOC) && !s->pcb && pkt->hdr.flags.fin)
			{
				return -1;
			}
			break;

		case RS_LISTEN:
			if(pkt->hdr.flags.syn && !pkt->hdr.flags.ack)
			{
				struct rudp_socket *ss;
				int sa_len;

				ss = _AllocRudpSocket();
				ss->rcvbuf_sz = s->rcvbuf_sz;
				ss->udp_sock = s->udp_sock;
				ss->pcb = _AllocRudpPcb(ss->rcvbuf_sz, INITIAL_SEQ_NO, ntohl(pkt->hdr.seqno), WINDOW_NTOH(pkt->hdr.flags.window));
				memcpy(&ss->pcb->peer, from, from_len);
				sa_len = sizeof(struct sockaddr_in);
				PA_GetSockName(s->udp_sock, (struct sockaddr*)&ss->pcb->local, &sa_len);

				ss->state = RS_SYN_RCVD;
				ss->timer[RT_KEEP] = conn_backoff[0];
				list_add_tail(&ss->listen_queue, &s->listen_queue);

				_sendSynAck(ss);

				return -1;
			}
			break;

		case RS_SYN_SENT:
			if(pkt->hdr.flags.syn)
			{
				int i;
				struct rudp_hdr hdr;

				pch = &s->pcb->channel[0];
				psbuf = &pch->sbuf;

				for(i=0; i<MAX_PHY_CHANNELS; i++)
				{
					pch[i].rbuf.expected_seqno = pch[i].rbuf.first_seq = ntohl(pkt->hdr.seqno);
					s->pcb->rwin_size = pch[i].sbuf.rwnd = pch[i].sbuf.rawnd = WINDOW_NTOH(pkt->hdr.flags.window);
					s->pcb->rtw_size = s->pcb->rwin_size/2;//8;
				}
				s->pcb->ssthresh = s->pcb->rwin_size; //s->pcb->rtw_size;

				/* Send ACK for normal connection, finish handshake.
				 *   or
				 * Send SYN & ACK for simultaneous open.
				 */
				memset(&hdr, 0, sizeof(hdr));
				hdr.u32_flags = 0;
				hdr.flags.rudp = RUDP_HEADER_TAG;
				hdr.flags.ack = 1;
				hdr.ackno = pkt->hdr.seqno;
				hdr.flags.window = WINDOW_HTON(s->pcb->channel[0].rbuf.win);

				PA_MutexLock(s->mutex_w);

				if(pkt->hdr.flags.ack)	//Normal open
				{
					_sendHeader(s, &hdr);
					s->state = RS_ESTABLISHED;
					SETEVENT(s->event_w);
				}
				else // Simultaneous open
				{
					//re-use packet
					hdr.flags.syn = 1;
					hdr.seqno = htonl(psbuf->seqno);
					_sendHeader(s, &hdr);

					s->state = RS_SYN_RCVD;
				}
				PA_MutexUnlock(s->mutex_w);
				return -1;
			}
			break;

		case RS_SYN_RCVD:
			if(pkt->hdr.flags.ack)// && pkt->hdr.ackno == s->pcb->channel[0].sbuf.seqno)
			{
				pch = &s->pcb->channel[0];
				psbuf = &pch->sbuf;

				/* Simultaneous open, wakeup thread wait on RUDPConnect(...) */
				if(list_empty(&s->listen_queue))
				{
					PA_MutexLock(s->mutex_w);
					s->state = RS_ESTABLISHED;
					SETEVENT(s->event_w);
					PA_MutexUnlock(s->mutex_w);
				}
				/* Accepted, wakeup thread wait on RUDPAccept(...) */
				else
				{
					//waiting thread is wakedup in _DispatchPacket...
					//since s is not returned by Accept yet, it's safe without lock
					s->state = RS_ESTABLISHED;
				}
				return -1;
			}
			else if(pkt->hdr.flags.syn)
			{
				_sendSynAck(s);
				return -1;
			}
			break;

		case RS_ESTABLISHED:
			return _PPState_Established(s, pkt, from, from_len);

		case RS_FIN_QUEUED:
		case RS_FIN_WAIT_1:
			if(pkt->hdr.flags.ack)
			{
				if(pkt->hdr.flags.fin)
				{
					_sendEmptyAck(s, 0);
					s->state = RS_TIME_WAIT;
				}
				else
					s->state = RS_FIN_WAIT_2;
			}
			else if(pkt->hdr.flags.fin)
			{
				_sendEmptyAck(s, 0);
				s->state = RS_CLOSING;
			}
			s->timer[RT_KEEP] = RTT_MIN * RTV_KEEP_CLOSE;
			return -1;

		case RS_FIN_WAIT_2:
			if(pkt->hdr.flags.fin)
			{
				_sendAck(s, 0);
				s->state = RS_TIME_WAIT;
			}
			return -1;

		case RS_CLOSING:
		case RS_TIME_WAIT:
		case RS_CLOSE_WAIT:
			break;
	}
	_sendReset(s, from);
	return -1;
}

/* pcb might not be allocated, cann't replaced by _sendHeader */
void _sendReset(struct rudp_socket *s, const struct sockaddr *to)
{
	struct rudp_hdr hdr;

	memset(&hdr, 0, sizeof(hdr));
	hdr.u32_flags = 0;
	hdr.flags.rudp = RUDP_HEADER_TAG;
	hdr.flags.rst = 1;
	hdr.crc32 = calc_crc32(0, (char*)&hdr, offsetof(struct rudp_hdr, crc32));
	_printHdr(s->pcb, &hdr, (struct sockaddr_in*)to);
	PA_SendTo(s->udp_sock, &hdr, sizeof(struct rudp_hdr), 0, 
			s->connected?NULL:to,
			sizeof(struct sockaddr));
}

PA_THREAD_RETTYPE __STDCALL _RUDPServiceThread(void *pdata)
{
	fd_set rfds;
	struct timeval tv;
	struct list_head *p;
	int max_fd;
	unsigned long t200, t500, tnow;

	t200 = t500 = PA_GetTickCount();
	while(run)
	{
		FD_ZERO(&rfds);
		tv.tv_sec = 0; tv.tv_usec = 25*1000;
		//FD_SET(sockw_r, &rfds);
		//max_fd = sockw_r;
		max_fd = -1;

		PA_MutexLock(mutex_sock_list);
		list_for_each(p, &sock_list)
		{
			struct rudp_socket *s = list_entry(p, struct rudp_socket, inst_list);
			//accepted socket's peer reseted ???
			if(1)//s->state != RS_DEAD || !list_empty(&s->accepted_list))
			{
				FD_SET(s->udp_sock, &rfds);
				max_fd = max(max_fd, s->udp_sock);
			}
		}
		PA_MutexUnlock(mutex_sock_list);

		if(select(max_fd+1, &rfds, NULL, NULL, &tv) > 0)
		{
			PA_MutexLock(mutex_sock_list);
			list_for_each(p, &sock_list)
			{
				struct rudp_socket *s = list_entry(p, struct rudp_socket, inst_list);
				if(FD_ISSET(s->udp_sock, &rfds))
				{
					struct sockaddr from;
					int len, from_len;
					struct rudp_pkt *pkt;

					while(1)
					{
						pkt = _MBufGetPacket();
						from_len = sizeof(from);
						if(pkt)
						{
							len = PA_RecvFrom(s->udp_sock, &pkt->hdr, MAX_PACKET_SIZE, 0, &from, &from_len);
							if(len < 0 || len < sizeof(struct rudp_hdr) || pkt->hdr.flags.rudp != RUDP_HEADER_TAG ||
									!_isPacketValid(pkt)) 
							{ 
								if(len < 0)
								{
#ifdef _DEBUG
#if defined(WIN32)
									int err = WSAGetLastError();
									if(err != WSAEWOULDBLOCK)
										dbg_msg("recvfrom error: %d\n", err);
#elif defined(ARM_UCOS_LWIP)
									int err = lwip_get_error(s->udp_sock);
								   	if(err != EWOULDBLOCK)
										dbg_msg("recvfrom: %s\n", lwip_strerr(err));
#else
									if(errno != EWOULDBLOCK)
										dbg_msg("recvfrom: %s\n", strerror(errno)); 
#endif
#endif
									//_terminateSocketInternally(s, ERUDP_RESETED); 
								}
								else if(s->non_rudp_pkt_cb)
									s->non_rudp_pkt_cb((const uint8_t*)&pkt->hdr, len, s->p_user);
								_MBufPutPacket(pkt);
								break;
							}

							pkt->len = len - sizeof(struct rudp_hdr);
							pkt->seqno = ntohl(pkt->hdr.seqno);
							_printPkt(s->pcb, pkt, PHF_FROM, (struct sockaddr_in*)&from);
							_DispatchPacket(s, pkt, &from, from_len);
						}
						else
							break;
					}
				}
			}
			PA_MutexUnlock(mutex_sock_list);
		}

		tnow = PA_GetTickCount();
		PA_MutexLock(mutex_sock_list);
		if(tnow - t200 >= DELAY_ACK_MS-4)
		{
			//t200 += DELAY_ACK_MS;
			t200 = tnow;
			_timerProc(_handleTimer200ms);
		}
		if(tnow - t500 >= RTT_UINT-4)
		{
			//t500 += RTT_UINT;
			t500 = tnow;
			rudp_now ++;
			_timerProc(_handleTimer500ms);
		}
		PA_MutexUnlock(mutex_sock_list);
	}


	return (PA_THREAD_RETTYPE)(0);
}


////////////////////////////////////////////////////////////////////////////////////////////
extern void initRudpTimer();
extern void uninitRudpTimer();

int RUDPStart()
{
/*
	struct sockaddr_in sai;
	int salen;

	sockw_r = socket(AF_INET, SOCK_DGRAM, 0);
	sockw_s = socket(AF_INET, SOCK_DGRAM, 0);

	memset(&sai, 0, sizeof(sai));
	sai.sin_family = AF_INET;
	sai.sin_addr.s_addr = inet_addr("127.0.0.1");
	bind(sockw_r, (struct sockaddr*)&sai, sizeof(sai));
	salen = sizeof(sai);
	PA_GetSockName(sockw_r, (struct sockaddr*)&sai, &salen);

	if(connect(sockw_s, (struct sockaddr*)&sai, sizeof(sai)) < 0)
	{
		perror("connect to output notification slot");
		exit(-1);
	}
*/
	PA_MutexInit(mutex_sock_list);
	PA_MutexInit(mutex_pkt_pool);
	free_pkt = NULL;

	hthd = PA_ThreadCreate(_RUDPServiceThread, NULL);
	//initRudpTimer();

	return 0;
}

int RUDPCleanup()
{
	struct list_head *p, *q;

	//uninitRudpTimer();

	run = 0;
	PA_ThreadWaitUntilTerminate(hthd);
	PA_ThreadCloseHandle(hthd);
	//PA_SocketClose(sockw_r);
	//PA_SocketClose(sockw_s);

	PA_MutexLock(mutex_sock_list);
	list_for_each_safe(p, q, &sock_list)
	{
		struct rudp_socket *s = list_entry(p, struct rudp_socket, inst_list);
		if(!list_empty(&s->listen_queue))
		{
			struct list_head *pp, *qq;
			list_for_each_safe(pp, qq, &s->listen_queue)
			{
				struct rudp_socket *ss = list_entry(pp, struct rudp_socket, accepted_list);
				_sendReset(ss, (struct sockaddr*)&ss->pcb->peer);
				_CleanupSocket(ss, 0);
				list_del(pp);
				free(ss);
			}
		}
		if(s->state == RS_ESTABLISHED)
			_sendReset(s, (struct sockaddr*)&s->pcb->peer);
		_CleanupSocket(s, 0);
		list_del(p);
		PA_SocketClose(s->udp_sock);
		free(s);
	}
	PA_MutexUnlock(mutex_sock_list);

	PA_MutexUninit(mutex_pkt_pool);
	while(free_pkt)
	{
		struct rudp_pkt *p = free_pkt;
		free_pkt = free_pkt->next;
		free(p);
	}

	PA_MutexUninit(mutex_sock_list);

	return 0;
}

RUDPSOCKET RUDPSocket()
{
	struct rudp_socket *sock = _AllocRudpSocket();
	sock->udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
	PA_SocketSetNBlk(sock->udp_sock, 1);
	PA_MutexLock(mutex_sock_list);
	list_add_tail(&sock->inst_list, &sock_list);
	PA_MutexUnlock(mutex_sock_list);

	return (RUDPSOCKET)sock;
}

RUDPSOCKET RUDPSocketFromUdp(int udpsock)
{
	struct rudp_socket *s = _AllocRudpSocket();
	s->udp_sock = udpsock;
	PA_SocketSetNBlk(udpsock, 1);
	PA_MutexLock(mutex_sock_list);
	list_add_tail(&s->inst_list, &sock_list);
	PA_MutexUnlock(mutex_sock_list);

	return (RUDPSOCKET)s;
}

int RUDPSetInvalidPacketCB(RUDPSOCKET sock, NONRUDPPACKETCB pkt_cb, void *p_user)
{
	struct rudp_socket *s = (struct rudp_socket*)sock;
	if(s->tag != RUDP_SOCKET_TAG) return ERUDP_NOT_SOCKET;
	s->non_rudp_pkt_cb = (_NONRUDPPACKETCB)pkt_cb;
	s->p_user = p_user;
	return 0;
}

int RUDPClose(RUDPSOCKET sock)
{
	struct rudp_socket *s = (struct rudp_socket*)sock;
	if(s->tag != RUDP_SOCKET_TAG) return ERUDP_NOT_SOCKET;

	switch(s->state)
	{
		case RS_CLOSED:
			break;
		case RS_LISTEN:
			{
				struct list_head *p, *q;
				PA_MutexLock(mutex_sock_list);
				list_for_each_safe(p, q, &s->listen_queue)
				{
					struct rudp_socket *aa = list_entry(p, struct rudp_socket, accepted_list);
					_sendReset(aa, (struct sockaddr*)&aa->pcb->peer);
					_CleanupSocket(aa, 0);
					list_del(p);
					free(aa);
				}
				PA_MutexUnlock(mutex_sock_list);
			}
			break;
		case RS_SYN_SENT:
		case RS_SYN_RCVD:
			_sendReset(s, (struct sockaddr*)&s->pcb->peer);
			break;
		case RS_ESTABLISHED:
			{
				int i, sb_is_empty = 1;
				PA_MutexLock(s->mutex_w);
				for(i=0; i<MAX_PHY_CHANNELS; i++)
				{
					struct sndbuf *psb = &s->pcb->channel[i].sbuf;
					struct rcvbuf *prb = &s->pcb->channel[i].rbuf;
					if(psb->first != NULL) { sb_is_empty = 0; }

					for(; prb->head != prb->tail; prb->head = (prb->head+1)%prb->q_size)
						if(prb->pkt_q[prb->head])
						{
							_MBufPutPacket(prb->pkt_q[prb->head]);
							prb->pkt_q[prb->head] = NULL;
						}
				}
				if(sb_is_empty)
				{
					_sendFin(s);
					s->state = RS_FIN_WAIT_1;
					s->timer[RT_KEEP] = RTT_MIN * RTV_KEEP_CLOSE;
				}
				else
				{
					s->state = RS_FIN_QUEUED;
				}
				PA_MutexUnlock(s->mutex_w);
			}
			break;
		case RS_CLOSE_WAIT:
			break;
		case RS_FIN_QUEUED:
		case RS_FIN_WAIT_1:
		case RS_FIN_WAIT_2:
		case RS_CLOSING:
		case RS_TIME_WAIT:
			return ERUDP_NOT_ALLOWED;
	}

	/* NOTE: 
	 * 	Remove from inst_list first, then cleanup the resources.
	 * 	If we are managed to maintain the closing states in future, cleanup
	 * 	should be executed in timerProc()
	 */

	if(1)//s->state < RS_ESTABLISHED)
	{
		PA_MutexLock(mutex_sock_list);
		_CleanAndFreeSocket(s);
		PA_MutexUnlock(mutex_sock_list);
	}

	return 0;
}

int RUDPListen(RUDPSOCKET sock, int n)
{
	struct rudp_socket *s = (struct rudp_socket*)sock;
	if(s->tag != RUDP_SOCKET_TAG) return ERUDP_NOT_SOCKET;

	if(s->state != RS_CLOSED) return ERUDP_NOT_ALLOWED;
	INIT_LIST_HEAD(&s->listen_queue);
	s->state = RS_LISTEN;

#if defined(WIN32) || defined(ARM_UCOS_LWIP)
	{
	int opt;
	opt = 100*1024;//1500*s->pcb->channel[0].sbuf.rwin_size/2;
	setsockopt(s->udp_sock, SOL_SOCKET, SO_RCVBUF, (const char*)&opt, sizeof(int));
	//setsockopt(s->udp_sock, SOL_SOCKET, SO_SNDBUF, &opt, sizeof(int));
	}
#endif

	return 0;
}

int RUDPAccept(RUDPSOCKET sock, RUDPSOCKET *accepted, struct sockaddr *addr, int *addrlen)
{
	struct rudp_socket *s, *a;
	struct list_head *p, *q;

	s = (struct rudp_socket*)sock;
	if(s->tag != RUDP_SOCKET_TAG) return ERUDP_NOT_SOCKET;

	*accepted = NULL;
wait:
	PA_MutexLock(s->mutex_r);
#if defined(WIN32) || defined(ARM_UCOS_LWIP)
	if(list_empty(&s->listen_queue))
	{
		PA_MutexUnlock(s->mutex_r);
		PA_EventWait(s->event_r);
		PA_MutexLock(s->mutex_r);
	}
#else
	while(list_empty(&s->listen_queue))
		pthread_cond_wait(&s->event_r, &s->mutex_r);
#endif
	PA_MutexUnlock(s->mutex_r);


	PA_MutexLock(mutex_sock_list);

	list_for_each_safe(p, q, &s->listen_queue)
	{
		a = list_entry(p, struct rudp_socket, listen_queue);
		if(a->state == RS_ESTABLISHED)
		{
			list_del(p);

			INIT_LIST_HEAD(&a->accepted_list);
			list_add_tail(&a->accepted_list, &s->accepted_list);

			INIT_LIST_HEAD(&a->listen_queue);
			*accepted = (RUDPSOCKET)a;
			memcpy(addr, &a->pcb->peer, sizeof(struct sockaddr));
			*addrlen = sizeof(struct sockaddr);

			break;
		}
	}

	PA_MutexUnlock(mutex_sock_list);

	if(!*accepted)
	{
		if(s->flags & RF_NBLK)
			return ERUDP_AGAIN;
		else goto wait;
	}

	return 0;
}

int RUDPBind(RUDPSOCKET sock, const struct sockaddr *addr, int addrlen)
{
	struct rudp_socket *s = (struct rudp_socket*)sock;
	if(s->tag != RUDP_SOCKET_TAG) return ERUDP_NOT_SOCKET;

	if(bind(s->udp_sock, addr, addrlen) == 0) return 0;
	return ERUDP_BIND;
}


/* For simultaneous open:
 * 	1. Call RUDPBind(...) to bind to a local port, call RUDPAccept(...) on NEITHER sides
 * 	2. Call RUDPConnect(...) on both sides
 */
int RUDPConnect(RUDPSOCKET sock, const struct sockaddr* addr, int addr_len)
{
	struct rudp_socket *s;
	int sa_len;

	s = (struct rudp_socket*)sock;
	if(s->tag != RUDP_SOCKET_TAG) return ERUDP_NOT_SOCKET;
	if(s->state == RS_ESTABLISHED) return ERUDP_CONNECTED;
	if(s->state == RS_SYN_SENT) return ERUDP_IN_PROGRESS;
	if(s->state != RS_CLOSED/* || s->pcb*/) return ERUDP_NOT_ALLOWED;


	s->err = 0;
	if(!s->pcb) s->pcb = _AllocRudpPcb(s->rcvbuf_sz, INITIAL_SEQ_NO, 0, 1);
	memcpy(&s->pcb->peer, addr, sizeof(struct sockaddr));
#if 0
	if(connect(s->udp_sock, addr, addr_len) == 0)
		s->connected = TRUE;
	else
		dbg_msg("call connect() failed.\n");
#endif
	sa_len = sizeof(struct sockaddr_in);
	PA_GetSockName(s->udp_sock, (struct sockaddr*)&s->pcb->local, &sa_len);
	s->state = RS_SYN_SENT;
	s->timer[RT_KEEP] = conn_backoff[0];

	//send initial SYN
	_sendSyn(s);
	s->pcb->retr_cnt = 0;

	if(s->flags & RF_NBLK)
		return ERUDP_AGAIN;

#if defined(WIN32) || defined(ARM_UCOS_LWIP)
	PA_EventWait(s->event_w);
#else
	PA_MutexLock(s->mutex_w);
	while(s->state == RS_SYN_SENT)
		pthread_cond_wait(&s->event_w, &s->mutex_w);
	PA_MutexUnlock(s->mutex_w);
#endif	

	if(s->state == RS_ESTABLISHED)
	{
#if 1
		int opt = 0;
		opt = 1500*128;
		setsockopt(s->udp_sock, SOL_SOCKET, SO_RCVBUF, (const char*)&opt, sizeof(int));
#endif

		return 0;
	}
	else
		return ERUDP_CONN_FAILED;
}

/** Set rudp socket to connected state(RS_ESTABLSHED) with default setting(receiver's buffer, ...)
 */
int RUDPConnected(RUDPSOCKET sock, const struct sockaddr* addr, int peer_rbuf_sz)
{
	struct rudp_socket *s;
	int sa_len, opt;

	s = (struct rudp_socket*)sock;
	if(s->tag != RUDP_SOCKET_TAG) return ERUDP_NOT_SOCKET;
	if(s->state == RS_ESTABLISHED) return ERUDP_CONNECTED;
	if(s->state == RS_SYN_SENT) return ERUDP_IN_PROGRESS;
	if(s->state != RS_CLOSED/* || s->pcb*/) return ERUDP_NOT_ALLOWED;


	s->err = 0;
	if(peer_rbuf_sz) s->rcvbuf_sz = peer_rbuf_sz;
	if(!s->pcb) s->pcb = _AllocRudpPcb(s->rcvbuf_sz, INITIAL_SEQ_NO, 0, 1);
	memcpy(&s->pcb->peer, addr, sizeof(struct sockaddr));
#if 0
	if(connect(s->udp_sock, addr, sizeof(struct sockaddr)) == 0)
		s->connected = TRUE;
	else
		dbg_msg("call connect() failed.\n");
#endif
	sa_len = sizeof(struct sockaddr_in);
	PA_GetSockName(s->udp_sock, (struct sockaddr*)&s->pcb->local, &sa_len);
	//PA_MutexLock(s->mutex_w);
	s->state = RS_ESTABLISHED;
	SETEVENT(s->event_w);
	//PA_MutexUnlock(s->mutex_w);
#if 1
	opt = 1500*128;
	setsockopt(s->udp_sock, SOL_SOCKET, SO_RCVBUF, (const char*)&opt, sizeof(int));
#endif
	return 0;
}

//! \retval >=0 bytes sent
//! \retval <0 error code
int RUDPSendV(RUDPSOCKET sock, int chno, const PA_IOVEC *v, unsigned int size, int flags)
{
	struct rudp_socket *s;
	struct sndbuf *ps;
	unsigned int i, len, byt_sent;
		
	len = byt_sent = 0;
	for(i=0; i<size; i++) len += PA_IoVecGetLen(&v[i]);
	if(len == 0) return 0;

	s = (struct rudp_socket*)sock;
	if(s->tag != RUDP_SOCKET_TAG) return ERUDP_NOT_SOCKET;
	if(s->err) return s->err;
	if(s->state != RS_ESTABLISHED) return ERUDP_NO_CONN;


	ps = &s->pcb->channel[PHY_CHN(chno)].sbuf;
	PA_MutexLock(s->mutex_w);
	if(s->state == RS_DEAD) 
	{
		PA_MutexLock(s->mutex_w);
		return s->err;
	}
	

#if defined(WIN32) || defined(ARM_UCOS_LWIP)
	if(ps->n_pkt >= ps->max_pkts && s->state != RS_DEAD)
	{
		if((flags & RUDPMSG_DONTWAIT) || (s->flags & RF_NBLK))
		{
			PA_MutexUnlock(s->mutex_w);
			return ERUDP_AGAIN;
		}
		PA_MutexUnlock(s->mutex_w);
		PA_EventWait(s->event_w);
		PA_MutexLock(s->mutex_w);
	}
#else
	while(ps->n_pkt >= ps->max_pkts && s->state != RS_DEAD)
	{
		if((flags & RUDPMSG_DONTWAIT) || (s->flags & RF_NBLK))
		{
			PA_MutexUnlock(s->mutex_w);
			return ERUDP_AGAIN;
		}
		pthread_cond_wait(&s->event_w, &s->mutex_w);
	}
#endif	
	if(s->err == 0)
	{
		unsigned int t, copied = 0/*byte copied in an IOVEC*/;
		struct rudp_pkt *last = ps->last;
		i = 0;
#if 1
		//if last packet has the same chno, fill it
		//   <<<-- always has the same chno since we have sending-queue for each chno(20150203)
		if(last && last->trans == 0 && last->hdr.flags.chno == chno)
		{
			for(; i<size && last->len < MAX_DATA_SIZE; i++)
			{
				t = min(PA_IoVecGetLen(&v[i]), MAX_DATA_SIZE - last->len);
				memcpy(last->data + last->len, PA_IoVecGetPtr(&v[i]), t);
				last->len += t;
				byt_sent += t;
				if(PA_IoVecGetLen(&v[i]) != t)	//last packet is full, but there still are data in v[i]
				{
					copied = t;
					break;
				}
			}
		}
#endif
		while(i < size)
		{
			struct rudp_pkt *pkt;

			pkt = _MBufGetPacket();
			if(!pkt) break;

			pkt->hdr.flags.chno = chno;

			for(; i<size && pkt->len < MAX_DATA_SIZE; i++, copied = 0)
			{
				t = min(PA_IoVecGetLen(&v[i]) - copied, MAX_DATA_SIZE - pkt->len);
				memcpy(pkt->data + pkt->len, (char*)PA_IoVecGetPtr(&v[i]) + copied, t);
				pkt->len += t;
				byt_sent += t;
				if(PA_IoVecGetLen(&v[i]) - copied != t)	//pkt is full
				{
					copied += t;
					break;
				}
			}
			if(pkt->len == 0)
			{
				_MBufPutPacket(pkt);
				continue;
			}

			pkt->seqno = s->pcb->channel[PHY_CHN(chno)].sbuf.seqno++;
			pkt->hdr.seqno = htonl(pkt->seqno);
			if(ps->first)
			{
				ps->last->next = pkt;
				ps->last = pkt;
				//ps->last = ps->last->next = pkt;
				if(!ps->not_sent) ps->not_sent = ps->last;
			}
			else
				ps->first = ps->last = ps->not_sent = pkt;
			pkt->next = NULL;

			ps->n_pkt++;
		}
		//signalOutput(s, PHY_CHN(chno));
		//_RudpOutput(s, PHY_CHN(chno), 0);
		while(_RudpOutput(s, PHY_CHN(chno), 0) > 0);
	}
	else
		byt_sent = s->err;

	PA_MutexUnlock(s->mutex_w);

	return byt_sent;
}

//! \param priority 0~15. Low value has a higher priority
//! \retval >=0 bytes sent
//! \retval <0 error code
int RUDPSendVEx(RUDPSOCKET sock, int chno, int priority, const PA_IOVEC *v, unsigned int size, int flags)
{
	struct rudp_socket *s;
	struct sndbuf *ps;
	unsigned int i, len, byt_sent;
		
	len = byt_sent = 0;
	for(i=0; i<size; i++) len += PA_IoVecGetLen(&v[i]);
	if(len == 0) return 0;

	s = (struct rudp_socket*)sock;
	if(s->tag != RUDP_SOCKET_TAG) return ERUDP_NOT_SOCKET;
	if(s->err) return s->err;
	if(s->state != RS_ESTABLISHED) return ERUDP_NO_CONN;


	ps = &s->pcb->channel[PHY_CHN(chno)].sbuf;
	PA_MutexLock(s->mutex_w);
	if(s->state == RS_DEAD) 
	{
		PA_MutexLock(s->mutex_w);
		return s->err;
	}
	

#if defined(WIN32) || defined(ARM_UCOS_LWIP)
	if(ps->n_pkt >= ps->max_pkts && s->state != RS_DEAD)
	{
		if((flags & RUDPMSG_DONTWAIT) || (s->flags & RF_NBLK))
		{
			PA_MutexUnlock(s->mutex_w);
			return ERUDP_AGAIN;
		}
		PA_MutexUnlock(s->mutex_w);
		PA_EventWait(s->event_w);
		PA_MutexLock(s->mutex_w);
	}
#else
	while(ps->n_pkt >= ps->max_pkts && s->state != RS_DEAD)
	{
		if((flags & RUDPMSG_DONTWAIT) || (s->flags & RF_NBLK))
		{
			PA_MutexUnlock(s->mutex_w);
			return ERUDP_AGAIN;
		}
		pthread_cond_wait(&s->event_w, &s->mutex_w);
	}
#endif	
	if(s->err == 0)
	{
		unsigned int t, copied = 0/*byte copied in an IOVEC*/;
		struct rudp_pkt *next_sav, *pos = ps->not_sent;

		if(pos == NULL) pos = ps->last;
		else
		while(pos != ps->last && pos->priority <= priority)
			pos = pos->next;

		i = 0;
		next_sav = pos->next;

		//if the packet has the same priority, fill it.  
		if(pos && pos->trans == 0 && pos->hdr.flags.chno == chno && pos->priority == priority)
		{
			for(; i<size && pos->len < MAX_DATA_SIZE; i++)
			{
				t = min(PA_IoVecGetLen(&v[i]), MAX_DATA_SIZE - pos->len);
				memcpy(pos->data + pos->len, PA_IoVecGetPtr(&v[i]), t);
				pos->len += t;
				byt_sent += t;
				if(PA_IoVecGetLen(&v[i]) != t)	//packet is full, but there still are data in v[i]
				{
					copied = t;
					break;
				}
			}
		}

		while(i < size)
		{
			struct rudp_pkt *pkt;

			pkt = _MBufGetPacket();
			if(!pkt) break;

			pkt->hdr.flags.chno = chno;

			for(; i<size && pkt->len < MAX_DATA_SIZE; i++, copied = 0)
			{
				t = min(PA_IoVecGetLen(&v[i]) - copied, MAX_DATA_SIZE - pkt->len);
				memcpy(pkt->data + pkt->len, (char*)PA_IoVecGetPtr(&v[i]) + copied, t);
				pkt->len += t;
				byt_sent += t;
				if(PA_IoVecGetLen(&v[i]) - copied != t)	//pkt is full
				{
					copied += t;
					break;
				}
			}
			if(pkt->len == 0)
			{
				_MBufPutPacket(pkt);
				continue;
			}

			pkt->seqno = s->pcb->channel[PHY_CHN(chno)].sbuf.seqno++;
			pkt->hdr.seqno = htonl(pkt->seqno);
			if(ps->first)
			{
				pos->next = pkt;
				pos = pkt;
				if(!ps->not_sent) ps->not_sent = pos;
			}
			else
				ps->first = ps->last = ps->not_sent = pkt;
			pkt->next = NULL;

			ps->n_pkt++;
		}
		pos->next = next_sav;

		//signalOutput(s, PHY_CHN(chno));
		//_RudpOutput(s, PHY_CHN(chno), 0);
		while(_RudpOutput(s, PHY_CHN(chno), 0) > 0);
	}
	else
		byt_sent = s->err;

	PA_MutexUnlock(s->mutex_w);

	return byt_sent;
}

int RUDPSend(RUDPSOCKET sock, int chno, const void *ptr, int len, int flags)
{
	struct rudp_socket *s;
	struct sndbuf *ps;
	int byt_sent = 0;
		
	if(len == 0) return 0;
	s = (struct rudp_socket*)sock;
	if(s->tag != RUDP_SOCKET_TAG) return ERUDP_NOT_SOCKET;
	if(s->err) return s->err;
	if(s->state != RS_ESTABLISHED) return ERUDP_NO_CONN;


	ps = &s->pcb->channel[PHY_CHN(chno)].sbuf;
	PA_MutexLock(s->mutex_w);
	if(s->state == RS_DEAD) 
	{
		PA_MutexLock(s->mutex_w);
		return s->err;
	}
	

	/* Put a packet on the end of sending-queue then return.
	 * If the sending-queue is full, wait on this queue.
	 * The actual sending is done in the service thread.
	 */
#if defined(WIN32) || defined(ARM_UCOS_LWIP)
	while(ps->n_pkt >= ps->max_pkts && s->state != RS_DEAD)
	{
		if((flags & RUDPMSG_DONTWAIT) || (s->flags & RF_NBLK))
		{
			PA_MutexUnlock(s->mutex_w);
			return ERUDP_AGAIN;
		}
		PA_MutexUnlock(s->mutex_w);
		PA_EventWait(s->event_w);
		PA_MutexLock(s->mutex_w);
	}
#else
	while(ps->n_pkt >= ps->max_pkts && s->state != RS_DEAD)
	{
		if((flags & RUDPMSG_DONTWAIT) || (s->flags & RF_NBLK))
		{
			PA_MutexUnlock(s->mutex_w);
			return ERUDP_AGAIN;
		}
		pthread_cond_wait(&s->event_w, &s->mutex_w);
	}
#endif	

	if(s->err == 0)
	{
		int t;
		char *data;
		struct rudp_pkt *last, *pkt;

		data = (char*)ptr;
		last = ps->last;

		//Merge small packets with the same chno
		if(last && last->trans == 0 && last->hdr.flags.chno == chno && last->len < MAX_DATA_SIZE)
		{
			t = min(len, MAX_DATA_SIZE - last->len);
			memcpy(last->data + last->len, data, t);
			last->len += t;
			byt_sent += t;
			data += t;
			len -= t;
		}

		while(len)
		{
			pkt = _MBufGetPacket();
			if(!pkt) break;

			pkt->hdr.flags.chno = chno;

			t = min(len, MAX_DATA_SIZE);
			memcpy(pkt->data, data, t);
			pkt->len = t;
			pkt->trans = 0;
			pkt->seqno = ps->seqno++;
			pkt->hdr.seqno = htonl(pkt->seqno);
			if(ps->first)
			{
				ps->last->next = pkt;
				ps->last = pkt;
				//ps->last = ps->last->next = pkt;
				if(!ps->not_sent) ps->not_sent = ps->last;
			}
			else
				ps->first = ps->last = ps->not_sent = pkt;
			pkt->next = NULL;

			ps->n_pkt++;
			byt_sent += t;
			len -= t;
		}

		//signalOutput(s, PHY_CHN(chno));
		//_RudpOutput(s, PHY_CHN(chno), 0);
		while(_RudpOutput(s, PHY_CHN(chno), 0) > 0);
	}
	else
		byt_sent = s->err;

	PA_MutexUnlock(s->mutex_w);

	//_sendReset(s, &s->pcb->peer);
	return byt_sent;
}

int RUDPSendEx(RUDPSOCKET sock, int chno, int priority, const void *ptr, int len, int flags)
{
	struct rudp_socket *s;
	struct sndbuf *ps;
	int byt_sent = 0;
		
	if(len == 0) return 0;
	s = (struct rudp_socket*)sock;
	if(s->tag != RUDP_SOCKET_TAG) return ERUDP_NOT_SOCKET;
	if(s->err) return s->err;
	if(s->state != RS_ESTABLISHED) return ERUDP_NO_CONN;


	ps = &s->pcb->channel[PHY_CHN(chno)].sbuf;
	PA_MutexLock(s->mutex_w);
	if(s->state == RS_DEAD) 
	{
		PA_MutexLock(s->mutex_w);
		return s->err;
	}
	

	/* Put a packet on the end of sending-queue then return.
	 * If the sending-queue is full, wait on this queue.
	 * The actual sending is done in the service thread.
	 */
#if defined(WIN32) || defined(ARM_UCOS_LWIP)
	while(ps->n_pkt >= ps->max_pkts && s->state != RS_DEAD)
	{
		if((flags & RUDPMSG_DONTWAIT) || (s->flags & RF_NBLK))
		{
			PA_MutexUnlock(s->mutex_w);
			return ERUDP_AGAIN;
		}
		PA_MutexUnlock(s->mutex_w);
		PA_EventWait(s->event_w);
		PA_MutexLock(s->mutex_w);
	}
#else
	while(ps->n_pkt >= ps->max_pkts && s->state != RS_DEAD)
	{
		if((flags & RUDPMSG_DONTWAIT) || (s->flags & RF_NBLK))
		{
			PA_MutexUnlock(s->mutex_w);
			return ERUDP_AGAIN;
		}
		pthread_cond_wait(&s->event_w, &s->mutex_w);
	}
#endif	

	if(s->err == 0)
	{
		int t;
		char *data;
		struct rudp_pkt *pos, *pkt, *next_sav;

		data = (char*)ptr;
		pos = ps->not_sent;
		while(pos && pos != ps->last && pos->priority <= priority)
			pos = pos->next;

		next_sav = pos->next;

		//Merge small packets with the same chno
		if(pos && pos->trans == 0 && pos->hdr.flags.chno == chno && pos->len < MAX_DATA_SIZE)
		{
			t = min(len, MAX_DATA_SIZE - pos->len);
			memcpy(pos->data + pos->len, data, t);
			pos->len += t;
			byt_sent += t;
			data += t;
		}

		while(byt_sent < len)
		{
			pkt = _MBufGetPacket();
			if(!pkt) break;

			pkt->hdr.flags.chno = chno;

			t = min(len, MAX_DATA_SIZE);
			memcpy(pkt->data, data, t);
			pkt->len = t;
			pkt->trans = 0;
			pkt->seqno = ps->seqno++;
			pkt->hdr.seqno = htonl(pkt->seqno);
			if(ps->first)
			{
				pos->next = pkt;
				pos = pkt;
				if(!ps->not_sent) ps->not_sent = pos;
			}
			else
				ps->first = ps->last = ps->not_sent = pkt;
			pkt->next = NULL;

			ps->n_pkt++;
			byt_sent += t;
		}

		//signalOutput(s, PHY_CHN(chno));
		//_RudpOutput(s, PHY_CHN(chno), 0);
		while(_RudpOutput(s, PHY_CHN(chno), 0) > 0);
	}
	else
		byt_sent = s->err;

	PA_MutexUnlock(s->mutex_w);

	//_sendReset(s, &s->pcb->peer);
	return byt_sent;
}
/** Receive a packet
    \param chno channel of packet[out]
    \param flags ---
    \return length of data received
*/
int RUDPRecv(RUDPSOCKET sock, int *chno, void *ptr, int len, int flags)
{
	struct rudp_socket *s;
	struct rudp_pcb *pcb;
	struct rcvbuf *prb;
	int i, no_data;
	int rlen;

	s = (struct rudp_socket*)sock;
	if(s->tag != RUDP_SOCKET_TAG) return ERUDP_NOT_SOCKET;
	if(s->err && s->err != ERUDP_PEER_CLOSED) return s->err;
	if(s->state != RS_ESTABLISHED && s->state != RS_CLOSE_WAIT) return ERUDP_NO_CONN;

	pcb = s->pcb;
	PA_MutexLock(s->mutex_r);
	if(s->state == RS_DEAD)
	{
		PA_MutexUnlock(s->mutex_r);
		return s->err;
	}
wait_data:
	no_data=1;
#if defined(WIN32) || defined(ARM_UCOS_LWIP)
	for(i=0; i<MAX_PHY_CHANNELS; i++)
	{
		prb = &pcb->channel[i].rbuf;
		if(prb->pkt_q[prb->head]) { no_data = 0; break; }
	}
	if(no_data)
	{
		PA_MutexUnlock(s->mutex_r);
		if(s->state == RS_CLOSE_WAIT) return 0;
		if((flags & RUDPMSG_DONTWAIT) || (s->flags & RF_NBLK))
			return ERUDP_AGAIN;
		PA_EventWait(s->event_r);
		PA_MutexLock(s->mutex_r);
	}
#else
	while(s->state == RS_ESTABLISHED)
	{
		for(i=0; i<MAX_PHY_CHANNELS; i++)
		{
			prb = &pcb->channel[i].rbuf;
			if(prb->pkt_q[prb->head]) { no_data = 0; break; }
		}
		if(!no_data) break;
		if(s->state == RS_CLOSE_WAIT) 
		{
			PA_MutexUnlock(s->mutex_r);
			return 0;
		}
		if((flags & RUDPMSG_DONTWAIT) || (s->flags & RF_NBLK))
		{
			PA_MutexUnlock(s->mutex_r);
			return ERUDP_AGAIN;
		}
		pthread_cond_wait(&s->event_r, &s->mutex_r);
	}
#endif	
	if(s->err == 0)
	{
		rlen = 0;
		for(i=0; i<MAX_PHY_CHANNELS; i++)
		{
			struct rudp_pkt *pkt;

			prb = &pcb->channel[i].rbuf;
			pkt = prb->pkt_q[prb->head];
			if(pkt)
			{
				if(pkt->len > len)
				{
					rlen = len;
					memcpy(ptr, pkt->pdata, len);
					pkt->len -= len;
					pkt->pdata += len;
				}
				else
				{
					rlen = pkt->len;
					memcpy(ptr, pkt->pdata, rlen);
					*chno = pkt->hdr.flags.chno;

					_MBufPutPacket(pkt);

					prb->pkt_q[prb->head] = NULL;
					/* "first_seq" always updates with "head",
					 * and is used to calculate the inserted posistion
					 * when a packet is received
					 */
					prb->head = (prb->head+1)%prb->q_size;
					prb->first_seq++;

					prb->win++;	/**/

					if(prb->win == 1 && !prb->should_ack)
					{
						prb->should_ack = ACKT_OPENWND;
						_RudpOutput(s, PHY_CHN(*chno), 0);
						//signalOutput(s, *chno);
					}
				}
				break;
			}
		}
		if(rlen == 0)
		{
			if(s->flags & RF_NBLK)
				rlen = -ERUDP_AGAIN;
			else
				goto wait_data;
		}
	}
	else if(s->err == ERUDP_PEER_CLOSED)
		rlen = 0;
	else
		rlen = s->err;
	PA_MutexUnlock(s->mutex_r);

	return rlen;
}

/** Receive a packet
    \param chno channel of packet[out]
    \param flags ---
    \return length of data received
*/
int RUDPRecvChn(RUDPSOCKET sock, int *chno, void *ptr, int len, int flags)
{
	struct rudp_socket *s;
	struct rudp_pcb *pcb;
	struct rcvbuf *prb;
	int rlen;

	s = (struct rudp_socket*)sock;
	if(s->tag != RUDP_SOCKET_TAG) return ERUDP_NOT_SOCKET;
	if(s->err && s->err != ERUDP_PEER_CLOSED) return s->err;
	if(s->state != RS_ESTABLISHED && s->state != RS_CLOSE_WAIT) return ERUDP_NO_CONN;

	pcb = s->pcb;
	PA_MutexLock(s->mutex_r);
	if(s->state == RS_DEAD)
	{
		PA_MutexUnlock(s->mutex_r);
		return s->err;
	}
	prb = &pcb->channel[PHY_CHN(*chno)].rbuf;

wait_data:
#if defined(WIN32) || defined(ARM_UCOS_LWIP)
	if(!prb->pkt_q[prb->head])
	{
		PA_MutexUnlock(s->mutex_r);
		if(s->state == RS_CLOSE_WAIT) return 0;
		if(s->flags & RF_NBLK) return ERUDP_AGAIN;
		PA_EventWait(s->event_r);
		PA_MutexLock(s->mutex_r);
	}
#else
	while(s->state == RS_ESTABLISHED)
	{
		if(prb->pkt_q[prb->head]) break;

		if(s->state == RS_CLOSE_WAIT) 
		{
			PA_MutexUnlock(s->mutex_r);
			return 0;
		}
		if(s->flags & RF_NBLK)
		{
			PA_MutexUnlock(s->mutex_r);
			return ERUDP_AGAIN;
		}
		pthread_cond_wait(&s->event_r, &s->mutex_r);
	}
#endif	
	if(s->err == 0)
	{
		struct rudp_pkt *pkt;
		rlen = 0;
		pkt = prb->pkt_q[prb->head];
		if(pkt)
		{
			if(pkt->len > len)
			{
				rlen = len;
				memcpy(ptr, pkt->pdata, len);
				*chno = pkt->hdr.flags.chno;

				pkt->len -= len;
				pkt->pdata += len;
			}
			else
			{
				rlen = pkt->len;
				memcpy(ptr, pkt->pdata, rlen);
				*chno = pkt->hdr.flags.chno;

				_MBufPutPacket(pkt);

				prb->pkt_q[prb->head] = NULL;
				/* "first_seq" always updates with "head",
				 * and is used to calculate the inserted posistion
				 * when a packet is received
				 */
				prb->head = (prb->head+1)%prb->q_size;
				prb->first_seq++;

				prb->win++;	/**/

				if(prb->win == 1 && !prb->should_ack)
				{
					prb->should_ack = ACKT_OPENWND;
					_RudpOutput(s, PHY_CHN(*chno), 0);
					//signalOutput(s, *chno);
				}
			}
		}
		else
		{
			if(s->flags & RF_NBLK)
				rlen = -ERUDP_AGAIN;
			else
				goto wait_data;
		}
	}
	else if(s->err == ERUDP_PEER_CLOSED)
		rlen = 0;
	else
		rlen = s->err;
	PA_MutexUnlock(s->mutex_r);

	return rlen;
}

//return: <0 - failed
//	  =0 - not ready
//	  >0 - ready
int RUDPSelectSock(RUDPSOCKET sock, int chno, int flag, const struct timeval *timeout)
{
	struct rudp_socket *s;
#if defined(__LINUX__) || defined(__ANDROID__)
	struct timespec ts;
#endif
	s = (struct rudp_socket*)sock;
	if(s->tag != RUDP_SOCKET_TAG) return ERUDP_NOT_SOCKET;


#if defined(__LINUX__) || defined(__ANDROID__)
	if(timeout)
	{
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += timeout->tv_sec;
		ts.tv_nsec += 1000 * timeout->tv_usec;
		if(ts.tv_nsec > 1000000000) {
			ts.tv_nsec -= 1000000000;
			ts.tv_sec ++;
		}
	}
#endif

	if(flag == RUDPSELECT_READABLE)
	{
		struct rcvbuf *prb;
		int no_data;
		PA_MutexLock(s->mutex_r);
		if(s->err == ERUDP_RESETED || s->err == ERUDP_PEER_CLOSED)
		{
			PA_MutexUnlock(s->mutex_r);
			return 1;
		}

		no_data = 1;
		if(no_data/* && s->state == RS_ESTABLISHED*/)
		{
			int i;
			if(s->state == RS_LISTEN)
			{
				struct list_head *p;
				list_for_each(p, &s->listen_queue)
				{
					struct rudp_socket *ss = list_entry(p, struct rudp_socket, listen_queue);
					if(ss->state == RS_ESTABLISHED)	//established socket, can be returned by RUDPAccept
					{
						no_data = 0;
						break;
					}
				}
			}
			else if(s->state == RS_ESTABLISHED)
			{
				for(i=0; i<MAX_PHY_CHANNELS; i++)
				{
					if(chno < 0 || chno == i)
					{
						prb = &s->pcb->channel[i].rbuf;
						if(prb->pkt_q[prb->head]) { no_data = 0; break; }
					}
				}
			}

			if(no_data)
			{
#if defined(WIN32) || defined(ARM_UCOS_LWIP)
				PA_MutexUnlock(s->mutex_r);
				if(timeout)
					no_data = !PA_EventWaitTimed(s->event_r, timeout->tv_sec*1000+timeout->tv_usec/1000);
				else {
					PA_EventWait(s->event_r);
					no_data = 0;
				}
				PA_MutexLock(s->mutex_r);
#else
				if(timeout)
					no_data = pthread_cond_timedwait(&s->event_r, &s->mutex_r, &ts) == ETIMEDOUT ? 1 : 0;
				else
					no_data = pthread_cond_wait(&s->event_r, &s->mutex_r) != 0;
#endif	
			}
		}

		PA_MutexUnlock(s->mutex_r);
		if(s->err) return s->err;
		else return !no_data;
	}


	if(flag == RUDPSELECT_WRITABLE) do 
	{
		struct sndbuf *ps;
		int writable = 1;

		if(chno < 0) return ERUDP_INVALID;
		if(s->state != RS_ESTABLISHED) break;

		PA_MutexLock(s->mutex_w);
		if(s->state == RS_DEAD) 
		{
			PA_MutexLock(s->mutex_w);
			return s->err;
		}

		ps = &s->pcb->channel[chno].sbuf;
		if(ps->n_pkt >= ps->max_pkts && s->state != RS_DEAD)
		{
#if defined(WIN32) || defined(ARM_UCOS_LWIP)
			PA_MutexUnlock(s->mutex_w);
			if(timeout)
				writable = PA_EventWaitTimed(s->event_w, timeout->tv_sec*1000+timeout->tv_usec/1000); 
			else {
				PA_EventWait(s->event_w);
				writable = 1;
			}
			PA_MutexLock(s->mutex_w);
#else
			if(timeout)
				writable = pthread_cond_timedwait(&s->event_w, &s->mutex_w, &ts) == ETIMEDOUT ? 0 : 1;
			else
				writable = pthread_cond_wait(&s->event_w, &s->mutex_w) == 0;
#endif	
		}

		PA_MutexUnlock(s->mutex_w);
		if(s->err) return s->err;
		else return writable;
	} while(0);

	return 0;
}

int RUDPSelect(RUDPSOCKCHNO *r_rscs, int *n_rrscs, RUDPSOCKCHNO *w_rscs, int *n_wrscs, 
		RUDPSOCKCHNO *e_rscs, int *n_erscs, const struct timeval *timeout)
{
	int i, ii, n;
       	int nr, nw, ne;
	struct list_head *p;
	struct rudp_socket *s, *ss;
	unsigned int wait_ticks, t0;

	struct timeval tv;
	int _rfds[64], _wfds[64], _efds[64];
	int _nr, _nw, _ne;
	fd_set _rfds_, _wfds_, _efds_;
	int max_fd = -1;

	if(timeout)
		wait_ticks = timeout->tv_sec * 1000 + timeout->tv_usec / 1000;
	else
		wait_ticks = ~0UL;

	/* Extract udp sockets */
	_nr = _nw = _ne = 0;
	if(r_rscs)
	{
		for(i=0; i<*n_rrscs; i++)
		{
			if(r_rscs[i].sock == NULL)
			{
				_rfds[_nr++] = r_rscs[i].chno;
				max_fd = max(max_fd, r_rscs[i].chno);
			}
		}
	}
	if(w_rscs)
	{
		for(i=0; i<*n_wrscs; i++)
		{
			if(w_rscs[i].sock == NULL)
			{
				_wfds[_nw++] = w_rscs[i].chno;
				max_fd = max(max_fd, w_rscs[i].chno);
			}
		}
	}
	if(e_rscs)
	{
		for(i=0; i<*n_erscs; i++)
		{
			if(e_rscs[i].sock == NULL)
			{
				_efds[_ne++] = e_rscs[i].chno;
				max_fd = max(max_fd, e_rscs[i].chno);
			}
		}
	}


	nr = nw = ne = 0;
	t0 = PA_GetTickCount();
	do {
		PA_MutexLock(mutex_sock_list);
		if(r_rscs)
		{
			n = *n_rrscs;
			for(i = 0; i < n; i++)
			{
				if(r_rscs[i].sock == NULL) continue;
				s = (struct rudp_socket*)r_rscs[i].sock;
				if(s->err == ERUDP_RESETED || s->err == ERUDP_PEER_CLOSED)
				{
					RUDP_SET(s, -1, r_rscs, nr);
				}
				else
				{
					if(s->state <= RS_CLOSED || s->state >= RS_FIN_QUEUED) 
					{
						continue;
						//PA_MutexUnlock(mutex_sock_list);
						//return ERUDP_NOT_SOCKET;
					}
					PA_MutexLock(s->mutex_r);
					if(s->state == RS_LISTEN)
					{
						list_for_each(p, &s->listen_queue)
						{
							ss = list_entry(p, struct rudp_socket, listen_queue);
							if(ss->state == RS_ESTABLISHED)	//established socket, can be returned by RUDPAccept
							{
								r_rscs[nr++].sock = s;
								break;
							}

						}
					}
					else if(s->state == RS_CLOSE_WAIT)
					{
						RUDP_SET(s, -1, r_rscs, nr);
					}
					else
					{
						struct rcvbuf *prb;
						for(ii=0; ii<MAX_PHY_CHANNELS; ii++)
						{
							if(r_rscs[i].chno < 0 || ii == r_rscs[i].chno)
							{
								prb = &s->pcb->channel[ii/*r_rscs[i].chno*/].rbuf;
								if(prb->pkt_q[prb->head])
								{
									RUDP_SET(r_rscs[i].sock, ii, r_rscs, nr);
									break;
								}
							}
						}
					}
					PA_MutexUnlock(s->mutex_r);
				}
			}
			if(nr)  { *n_rrscs = nr; r_rscs[nr].sock = INVALID_RUDPSOCKET; }
		}

		if(w_rscs)
		{
			n = *n_wrscs;
			for(i = 0; i < n; i++)
			{
				if(w_rscs[i].sock == NULL) continue;
				s = (struct rudp_socket*)w_rscs[i].sock;
				if(s->state <= RS_CLOSED || s->state >= RS_FIN_QUEUED)
				{
					//PA_MutexUnlock(mutex_sock_list);
					//return ERUDP_NOT_SOCKET;
					continue;
				}
				PA_MutexLock(s->mutex_w);
				if(s->state == RS_ESTABLISHED)
				{
					for(ii=0; ii<MAX_PHY_CHANNELS; ii++)
					{
						if(w_rscs[i].chno < 0 || w_rscs[i].chno == ii)
						{
							struct sndbuf *psb = &s->pcb->channel[ii].sbuf;
							if(psb->n_pkt < psb->max_pkts)
							{
								RUDP_SET(w_rscs[i].sock, ii, w_rscs, nw);
								break;
							}
						}
					}
				}
				else if(s->state == RS_CLOSE_WAIT)
				{
					RUDP_SET(w_rscs[i].sock, -1, w_rscs, nw);
				}
				PA_MutexUnlock(s->mutex_w);
			}
			if(nw) { *n_wrscs = nw; w_rscs[nw].sock = INVALID_RUDPSOCKET; }
		}
		if(e_rscs)
		{
			for(i=0; i<*n_erscs; i++)
			{
				if(e_rscs[i].sock == NULL) continue;
				s = (struct rudp_socket*)e_rscs[i].sock;
				if(s->err) RUDP_SET(s, -1, e_rscs, ne);
			}
			if(ne) { *n_erscs = ne; e_rscs[ne].sock = INVALID_RUDPSOCKET; }
		}
		PA_MutexUnlock(mutex_sock_list);

		//if(nr || nw || ne) break;	//normal socket will be starved
		if(_nr || _nw || _ne)
		{
			tv.tv_sec = 0; tv.tv_usec = 0;
			if(_nr) { FD_ZERO(&_rfds_); for(i=0; i<_nr; i++) FD_SET(_rfds[i], &_rfds_); }
			if(_nw) { FD_ZERO(&_wfds_); for(i=0; i<_nw; i++) FD_SET(_wfds[i], &_wfds_); }
			if(_ne) { FD_ZERO(&_efds_); for(i=0; i<_ne; i++) FD_SET(_efds[i], &_efds_); }
			if(select(max_fd+1, _nr?&_rfds_:NULL, _nw?&_wfds_:NULL, _ne?&_efds_:NULL, &tv) > 0)
			{
				if(_nr) for(i=0; i<_nr; i++)
					if(FD_ISSET(_rfds[i], &_rfds_))
					{
						r_rscs[nr].sock = NULL;
						r_rscs[nr++].chno = _rfds[i];
					}
				if(_nw) for(i=0; i<_nw; i++)
					if(FD_ISSET(_wfds[i], &_wfds_))
					{
						w_rscs[nw].sock = NULL;
						w_rscs[nw++].chno = _wfds[i];
					}
				if(_ne) for(i=0; i<_ne; i++)
					if(FD_ISSET(_efds[i], &_efds_))
					{
						e_rscs[ne].sock = NULL;
						e_rscs[ne++].chno = _efds[i];
					}
			}
		}

		if(nr || nw || ne) break;
		PA_Sleep(5);
	} while(PA_GetTickCount() - t0 < wait_ticks);

	if(n_erscs) *n_erscs = ne;
	if(n_rrscs) *n_rrscs = nr;
	if(n_wrscs) *n_wrscs = nw;

	return nr + nw + ne;
}

int RUDP_FD_ISSET(int fd, const RUDPSOCKCHNO *prc, int size)
{
	int i;
	if(fd < 0) return 0;
	for(i=0; i<size; i++)
	{
		if(prc[i].sock == NULL && prc[i].chno == fd) return 1;
	}
	return 0;
}

int RUDP_ISSET(RUDPSOCKET s, const RUDPSOCKCHNO *prc, int size)
{
	int i;
	if(s == INVALID_RUDPSOCKET) return 0;
	for(i=0; i<size; i++)
	{
		if(prc[i].sock == s) return 1;
	}
	return 0;
}

int RUDPGetSockOpt(RUDPSOCKET sock, int opt, void *optval, int *optlen)
{
	struct rudp_socket *s = (struct rudp_socket*)sock;
	if(s->tag != RUDP_SOCKET_TAG) return ERUDP_NOT_SOCKET;

	switch(opt)
	{
	case OPT_UDP_SNDBUF:
		if(*optlen != sizeof(int)) return ERUDP_INVALID;
		PA_GetSockOpt(s->udp_sock, SOL_SOCKET, SO_SNDBUF, (char*)&opt, optlen);
		return 0;
	case OPT_UDP_RCVBUF:
		if(*optlen != sizeof(int)) return ERUDP_INVALID;
		PA_GetSockOpt(s->udp_sock, SOL_SOCKET, SO_RCVBUF, (char*)&opt, optlen);
		return 0;
	case OPT_RUDP_SNDBUF:
		if(*optlen != sizeof(int)) return ERUDP_INVALID;
		*((int*)optval) = s->pcb->channel[0].sbuf.max_pkts;
		break;
	case OPT_RUDP_RCVBUF:
		if(*optlen != sizeof(int)) return ERUDP_INVALID;
		*((int*)optval) = s->pcb->channel[0].rbuf.q_size;
		break;
	case OPT_LINGER:
	//case OPT_MSS:
	case OPT_SNDTIMEO:
	case OPT_RCVTIMEO:
		break;
	case OPT_ADHOC:
		if(*optlen != sizeof(int)) return ERUDP_INVALID;
		*((int*)optval) = (s->flags & RF_ADHOC)?1:0;
		break;

	case OPT_NBLK:
		if(*optlen != sizeof(int)) return ERUDP_INVALID;
		*((int*)optval) = (s->flags & RF_NBLK)?1:0;
		break;

	case OPT_REUSEADDR:
		if(*optlen != sizeof(int)) return ERUDP_INVALID;
		PA_GetSockOpt(s->udp_sock, SOL_SOCKET, SO_REUSEADDR, (char*)optval, optlen);
		break;
	case OPT_ERR:
		if(*optlen != sizeof(int)) return ERUDP_INVALID;
		*((int*)optval) = s->err;

	default:
		return ERUDP_INVALID;
	}
	return 0;
}

int RUDPSetSockOpt(RUDPSOCKET sock, int opt, const void *optval, int optlen)
{
	int i, val;
	struct rudp_socket *s = (struct rudp_socket*)sock;
	if(s->tag != RUDP_SOCKET_TAG) return ERUDP_NOT_SOCKET;

	switch(opt)
	{
	case OPT_UDP_SNDBUF:
		if(optlen != sizeof(int)) return ERUDP_INVALID;
		setsockopt(s->udp_sock, SOL_SOCKET, SO_SNDBUF, (char*)&opt, optlen);
		break;
	case OPT_UDP_RCVBUF:
		if(optlen != sizeof(int)) return ERUDP_INVALID;
		setsockopt(s->udp_sock, SOL_SOCKET, SO_RCVBUF, (char*)&opt, optlen);
		break;
	case OPT_RUDP_SNDBUF:
		if(optlen != sizeof(int)) return ERUDP_INVALID;
		if(s->pcb)
		{
			val = *((int*)optval);
			if(val > MAX_WINDOW) val = MAX_WINDOW;
			if(val < 64) val = 64;
			for(i=0; i<MAX_PHY_CHANNELS; i++)
				s->pcb->channel[i].sbuf.max_pkts = val;
		}
		break;
	case OPT_RUDP_RCVBUF:
		if(optlen != sizeof(int)) return ERUDP_INVALID;
		if(!s->pcb && s->state == RS_CLOSED)
		{
			val = *((int*)optval);
			if(val > MAX_WINDOW) val = MAX_WINDOW;
			if(val < 64) val = 64;
			s->rcvbuf_sz = val;
		}
		break;
	case OPT_LINGER:
	//case OPT_MSS:
	case OPT_SNDTIMEO:
	case OPT_RCVTIMEO:
		break;
	case OPT_ADHOC:
		if(optlen != sizeof(int)) return ERUDP_INVALID;
		if(s->state != RS_CLOSED || s->pcb) return ERUDP_NOT_ALLOWED;
		if(*((int*)optval)) s->flags |= RF_ADHOC;
		else s->flags &= ~RF_ADHOC;

	case OPT_REUSEADDR:
		if(optlen != sizeof(int)) return ERUDP_INVALID;
		setsockopt(s->udp_sock, SOL_SOCKET, SO_REUSEADDR, (char*)optval, optlen);
		break;

	case OPT_NBLK:
		if(optlen != sizeof(int)) return ERUDP_INVALID;
		if(s->state < RS_CLOSED) return ERUDP_NOT_ALLOWED;
		if(*((int*)optval)) s->flags |= RF_NBLK;
		else s->flags &= ~RF_NBLK;
		break;

	default:
		return ERUDP_INVALID;
	}
	return 0;
}

int RUDPGetSockName(RUDPSOCKET sock, struct sockaddr *name)
{
	struct rudp_socket *s = (struct rudp_socket*)sock;
	socklen_t len = sizeof(struct sockaddr);
	return getsockname(s->udp_sock, name, &len);
}

int RUDPGetPeerName(RUDPSOCKET sock, struct sockaddr *name)
{
	struct rudp_socket *s = (struct rudp_socket*)sock;
	if(s->pcb)
	{
		memcpy(name, &s->pcb->peer, sizeof(struct sockaddr));
		return 0;
	}
	memset(name, 0, sizeof(struct sockaddr));
	return 0;
}

