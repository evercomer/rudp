#include <string.h>
#include "rudp_punch.h"

/*
 * \param local_port 
 * 	Local port the socket will bind to(but not listen on).
 * \param listening_peer 
 * 	The address the other side listening on, may be NULL.
 * \param candidate_peers, n_peer
 * 	The possible addresses the other side can be reached by.
 */
RUDPSOCKET RUDPPunch(unsigned short local_port, 
		const struct sockaddr *listening_peer, 
		const struct sockaddr_in *candidate_peers, int n_peer, 
		CHECKCONNECTIONCB cb, void *cb_data)
{
#define MAX_CANDIDATES 10
	RUDPSOCKET rsock_c = INVALID_RUDPSOCKET, rsocks[MAX_CANDIDATES];
	RUDPSOCKCHNO rset[MAX_CANDIDATES], wset[MAX_CANDIDATES], eset[MAX_CANDIDATES];
	int n_rset, n_wset, n_eset, nr, nw, ne;
	struct timeval tv;
	struct sockaddr_in sai;
	int i, opt;

	if(n_peer > 9) n_peer = 9;
	if(listening_peer) n_peer++;

	memset(&sai, 0, sizeof(sai));
	sai.sin_family = AF_INET;
	sai.sin_port = htons(local_port);
	opt = 1;
	n_rset = n_wset = n_eset = 0;
	for(i=0; i<n_peer; i++)
	{
		rsocks[i] = RUDPSocket();
		RUDPSetSockOpt(rsocks[i], OPT_REUSEADDR, &opt, sizeof(int));
		if(RUDPBind(rsocks[i], (struct sockaddr*)&sai, sizeof(sai)) != 0)
		{
			dbg_msg("RUDPBind failed.\n");
		}
		RUDPSetSockOpt(rsocks[i], OPT_NBLK, &opt, sizeof(int));
		if(i==n_peer-1 && listening_peer)
		{
			RUDPConnect(rsocks[i], listening_peer, sizeof(struct sockaddr));
			rsock_c = rsocks[i];
		}
		else
			RUDPConnect(rsocks[i], (const struct sockaddr*)&candidate_peers[i], sizeof(struct sockaddr));

		RUDP_SET(rsocks[i], -1, eset, n_eset);
		RUDP_SET(rsocks[i], -1, rset, n_rset);
	}


	tv.tv_sec = 1; tv.tv_usec = 0;
	time_t t0 = time(NULL);
	do {

		nr = n_rset; nw = n_wset; ne = n_eset;
		if(RUDPSelect(rset, &nr, wset, &nw, eset, &ne, &tv) <= 0)
			continue;

		for(i=0; i<n_peer; i++)
			if(rsocks[i] != INVALID_RUDPSOCKET && RUDP_ISSET(rsocks[i], rset, nr))	//data arrived
			{
				switch(cb(rsocks[i], CONNSTATUS_READABLE, cb_data))
				{
				case CHECKCONNECTION_OK:
					rsock_c = rsocks[i];
					rsocks[i] = INVALID_RUDPSOCKET;
					goto out;

				case CHECKCONNECTION_FAKE:
					RUDPClose(rsocks[i]);
					rsocks[i] = INVALID_RUDPSOCKET;
					break;
				}
			}

		for(i=0; i<n_peer; i++)
			if(rsocks[i] != INVALID_RUDPSOCKET && RUDP_ISSET(rsocks[i], wset, nw))	//connected
			{
				switch(cb(rsocks[i], (i==n_peer-1)&&listening_peer?CONNSTATUS_ACCEPTED:CONNSTATUS_CONNECTED, cb_data))
				{
				case CHECKCONNECTION_OK:
					rsock_c = rsocks[i];
					rsocks[i] = INVALID_RUDPSOCKET;
					goto out;
					
				case CHECKCONNECTION_CONTINUE:
					RUDP_SET(rsocks[i], 0, rset, n_rset);
					RUDP_CLR(rsocks[i], 0, wset, n_wset);
					break;

				case CHECKCONNECTION_FAKE:
					RUDPClose(rsocks[i]);
					rsocks[i] = INVALID_RUDPSOCKET;
					break;
				}
			}

		for(i=0; i<n_peer; i++)
			if(rsocks[i] != INVALID_RUDPSOCKET && RUDP_ISSET(rsocks[i], eset, ne))
			{
				if(i==n_peer-1 && listening_peer)
				{
					RUDPConnect(rsocks[i], listening_peer, sizeof(struct sockaddr));
				}
				else
					RUDPConnect(rsocks[i], (const struct sockaddr*)&candidate_peers[i], sizeof(struct sockaddr));
			}

	} while(time(NULL) - t0 < 8);

out:
	for(i=0; i<n_peer; i++)
		if(rsocks[i] != INVALID_RUDPSOCKET)
			RUDPClose(rsocks[i]);
	return rsock_c;
}

int _ClientPunchCb(RUDPSOCKET s, int status, void *data)
{
	int len, chno;
	struct dcs_punch dp;

	switch(status)
	{
	case CONNSTATUS_CONNECTED:
	case CONNSTATUS_ACCEPTED:
		if(RUDPSend(s, (char*)data, sizeof(struct dcs_header) + ntohl(((struct dcs_header*)data)->length), 0) < 0)
			break;
		return CHECKCONNECTION_CONTINUE;

	case CONNSTATUS_READABLE:
		len = RUDPRecv(s, &chno, &dp, sizeof(dp));
		if(len >= sizeof(struct dcs_header) && check_dcs_header(&dp.dh) && dp.dh.cls == CLS_RESPONSE && dp.dh.st == ST_IPCAM)
			return(dp.dh.status == 0)?CHECKCONNECTION_OK:-dp.dh.status;
		else
			return -1000;
		break;
	}

	return CHECKCONNECTION_CONTINUE;
}

int _CameraPunchCb(RUDPSOCKET s, int status, void *data)
{
	int len;
	struct dcs_punch dp;
	switch(status)
	{
	case CONNSTATUS_CONNECTED:
		;//if this is client, send confirmation
		break;
	case CONNSTATUS_ACCEPTED:
		;//send confirmation
		break;
	case CONNSTATUS_READABLE:
		len = RUDPRecv(s, &chno, &dp, sizeof(dp));
		if(len >= sizeof(struct dcs_header) && check_dcs_header(&dp.dh) && dp.dh.cls == CLS_REQUEST && dp.dh.st == ST_CLT)
		{
			;//check session and password
		}
		break;
	}

	return CHECKCONNECTION_CONTINUE;
}


int main()
{
	return 0;
}

