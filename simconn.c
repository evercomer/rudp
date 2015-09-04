/*
 *
 * Demo for RUDPSimConnnect(...)
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rudp.h"
#include "platform_adpt.h"

RUDPSOCKET s;

void *recv_thread(void *p)
{
	struct timeval tv = { 1, 0 };
	RUDPSOCKCHNO rset[2];
	int nr, rlt;
	char line[1000];

	while(1)
	{
		nr = 0;
		RUDP_SET(s, 0, rset, nr);
		if((rlt = RUDPSelect(rset, &nr, NULL, NULL, NULL, NULL, &tv)) > 0)
		{
			int chno, len, rudp;

			if(RUDP_ISSET(s, rset, nr))
			{
				len = RUDPRecv(s, &chno, line, 1000, 0);
				rudp = 1;
			}
			if(len > 0)
			{
				line[len] = '\0';
				printf("%s: %s\n", rudp?"rudp":"udp", line);
			}
			else
			{
				printf("Recv Error %d\n", len);
				break;
			}
		}
		else if(rlt == 0)
		{
			printf(".");
			fflush(stdout);
		}
		else
		{
			printf("RUDPSelect: %d\n", rlt);
			break;
		}
	}
	return NULL;
}


int main(int argc, char *argv[])
{
	struct sockaddr_in sai;
	int i, connected = 0;

	if(argc < 2) { printf("simconn remotehost [localport remoteport]\n"); return -1; }


	PA_NetLibInit();
	RUDPStart();
       
	s = RUDPSocket();
	memset(&sai, 0, sizeof(sai));
	sai.sin_family = AF_INET;
	sai.sin_port = htons(argc>2?atoi(argv[2]):5001);
	if(RUDPBind(s, (struct sockaddr*)&sai, sizeof(sai)) < 0)
	{
		printf("bind to local port %d failed\n", ntohs(sai.sin_port));
		RUDPCleanup();
		return -1;
	}
	//i = 1;
	//RUDPSetSockOpt(s, OPT_ADHOC, &i, sizeof(i));


	if(argc > 3) sai.sin_port = htons(atoi(argv[3]));
	sai.sin_addr.s_addr = inet_addr(argv[1]);
	sleep(5);
	printf("begin connection....\n");
	if(RUDPSimConnect(s, (struct sockaddr*)&sai, 1, NULL, 0) == 0)
		connected = 1;


	char line[1000];
	if(connected)
	{
		pthread_t thd;
		pthread_create(&thd, NULL, recv_thread, NULL);
		pthread_detach(thd);

		while(fgets(line, 1000, stdin))
		{
			int rlt, flag = RUDPSELECT_WRITABLE;
			struct timeval tv = { 0, 0 };
			rlt = RUDPSelectSock(s, 0, flag, &tv);
			if(rlt > 0)
			{
				if( (rlt = RUDPSend(s, 0, line, strlen(line), 0)) < 0)
				{
					printf("RUDPSend: %d\n", rlt);
					break;
				}
			}
			else
			{
			}
		}
	}
	else
		printf("connect to %s failed\n", argv[1]);

	RUDPClose(s);

	printf("press Enter to terminate.");
	fgets(line, 1000, stdin);
	RUDPCleanup();
	return 0;
}

