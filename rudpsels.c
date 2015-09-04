#include "rudp.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#define SERVER_SEND	0x01
#define SERVER_RECV	0x02

//===================================================================
int sendData(RUDPSOCKET sock)
{
	char line[3000];
	int i=0, rlt = 0;

	memset(line, '1' + i%36, 3000);

	unsigned long n_byt;
	for(i=0; 0<50000; i++)
	{
		struct timeval tv = { 0, 25000 };
		int flag = RUDPSELECT_WRITABLE;
		PA_IOVEC v[3];
		v[0].iov_base = v[1].iov_base = v[2].iov_base = line;
		v[0].iov_len = 230;
		v[1].iov_len = 1100;
		v[2].iov_len = 100;

		n_byt = v[0].iov_len + v[1].iov_len + v[2].iov_len;
		if(RUDPSelectSock(sock, 1, flag, &tv) > 0)
		{
			rlt = RUDPSendV(sock, 1, v, 3, 0);
			if(rlt < 0)
			{
				printf("RUDPSend error: %d\n", rlt);
				break;
			}
			else
			{
			}
		}
	}

	printf("***************************************\n");
	if(rlt > 0)
	{
		line[0] = '\0';
		RUDPSend(sock, 0, line, 1, 0);
		printf("all data sent.\n");
	}
	return 0;
}

void recvData(RUDPSOCKET sock)
{
	int cnt = 0;
	char line[2100];
	int len, chno;
	int ic = 0;
	char cBusy[] = "-\\|/";

	while( 1 )
	{
		struct timeval tv = { 0, 500000 };
		int flag = RUDPSELECT_READABLE, rlt;
		if((rlt = RUDPSelectSock(sock, -1, flag, &tv)) > 0)
		{
			if((len = RUDPRecv(sock, &chno, line, 2000, 0)) > 0)
			{
				printf(line);
			}
			else if(len < 0)
			{
				fprintf(stderr, "RUDPRecv: %d\n", len);
				exit(-1);
			}
		} else if(rlt < 0)
		{
			printf("ERROR: RUDPSelectSock: %d\n", rlt);
			exit(-1);
		}
		else {
			printf("\r%c", cBusy[ic]); fflush(stdout);
			ic = (ic+1)%4;
		}
	}
	printf("receiving finished.\n");
}

void *threadSend(void *p)
{
	RUDPSOCKET sock = (RUDPSOCKET)p;
	pthread_detach(pthread_self());

	//recvData(pcs);
	sendData(sock);

	RUDPClose(sock);
	sleep(1);
	return NULL;
}

void* threadRecv(void *p)
{
	RUDPSOCKET sock = (RUDPSOCKET)p;
	pthread_detach(pthread_self());

	recvData(sock);

	RUDPClose(sock);
	sleep(1);
	return NULL;
}

int main(int argc, char *argv[])
{
	struct sockaddr_in sai;
	RUDPSOCKET s;

	RUDPStart();
       
	s = RUDPSocket();

	memset(&sai, 0, sizeof(sai));
	sai.sin_family = AF_INET;
	sai.sin_addr.s_addr = argc==2?inet_addr(argv[1]):htonl(INADDR_ANY);
	sai.sin_port = htons(5001);
	if(RUDPBind(s, (struct sockaddr*)&sai, sizeof(sai)) < 0)
	{
		perror("bind");
		return -1;
	}

	RUDPListen(s, 5);

	int sa_len = sizeof(sai);
	RUDPSOCKET a;
	pthread_t thd;

	while(1)
	{
		if(RUDPAccept(s, &a, (struct sockaddr*)&sai, &sa_len) < 0)
		{
			printf("accept error\n");
		}
		else
		{
#if 0
			char cmd;
			int chno, len;

			while((len = RUDPRecv(a, &chno, &cmd, 1, 0)) > 0)
			{
				if( chno == 0 )
					break;
			}
			if(cmd & SERVER_SEND)
				pthread_create(&thd, NULL, threadSend, a);
			if(cmd & SERVER_RECV)
				pthread_create(&thd, NULL, threadRecv, a);
#else
			pthread_create(&thd, NULL, threadRecv, a);
#endif
		}
	}

	RUDPClose(s);

	RUDPCleanup();

	return 0;
}
