#include "rudp.h"
#include "platform_adpt.h"
#include <string.h>
#include <stdio.h>

int sendData(RUDPSOCKET s)
{
	char line[3000];
	int i=0, rlt = 0;
	memset(line, '1' + i%36, 3000);

	time_t t0, t_prev, t;
	unsigned long nTotal = 0, nByt = 0;
	t0 = t_prev = time(NULL);
	for(i=0; 0<50000; i++)
	{
		PA_IOVEC v[3];
		v[0].iov_base = v[1].iov_base = v[2].iov_base = line;
		v[0].iov_len = 230;
		v[1].iov_len = 1100;
		v[2].iov_len = 100;

		int onesent = v[0].iov_len + v[1].iov_len + v[2].iov_len;
		nTotal += onesent;
		nByt += onesent;
		if( (i % 4) == 0)
		{
			rlt = RUDPSendV(s, 1, v, 3, 0);
		}
		else
			rlt = RUDPSendV(s, 0/*1*/, v, 3, 0);
		if(rlt < 0)
		{
			printf("RUDPSend error: %d\n", rlt);
			break;
		}

		time(&t);
		if(t != t_prev)
		{
			fprintf(stderr, "rate: Avg.: %d B/s, Rt. %dB/s        \r", nTotal/(t-t0), nByt);
			fflush(stderr);
			t_prev = t;
			nByt = 0;
		}
	}

	printf("***************************************\n");
	if(rlt > 0)
	{
		line[0] = '\0';
		RUDPSend(s, 0, line, 1, 0);
		printf("all data sent.\n");
	}
	return 0;
}

void recvData(RUDPSOCKET a)
{
	int cnt = 0;
	char line[2100];
	int len, chno;
	while( 1 )
	{
		struct timeval tv = { 0, 500000 };
		int flag = RUDPSELECT_READABLE, rlt;
		if( 1 )//(rlt = RUDPSelectSock(a, -1, flag, &tv)) > 0)
		{
			if((len = RUDPRecv(a, &chno, line, 2000, 0)) > 0)
			{
				if(chno == 0)
				{
					//fprintf(stderr, "Packet %d from chn 0\n", cnt);
				}
				cnt++;
				line[len] = '\0';
				//printf("%s\n", line);
				if(line[0] == '\0') break;
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
	}
	printf("receiving finished.\n");
}
void *threadSend(void *p)
{
	RUDPSOCKET a = (RUDPSOCKET*)p;
	pthread_detach(pthread_self());

	//recvData(a);
	sendData(a);

	RUDPClose(a);
	sleep(5);
	return NULL;
}

void* threadRecv(void *p)
{
	RUDPSOCKET a = (RUDPSOCKET*)p;
	pthread_detach(pthread_self());

	recvData(a);
	//sendData(a);

	RUDPClose(a);
	sleep(5);
	return NULL;
}


//Usage: rudpclt [-a] [server] [port]
//	------- -a: asynchronous
//	------- server: default is 127.0.0.1, 
//	------- port: 5001 by default
#define SERVER_SEND	0x01
#define SERVER_RECV	0x02
int main(int argc, char *argv[])
{
	struct sockaddr_in sai;
	int sa_len;
	int rlt, i, async = 0;
	RUDPSOCKET s;
	char cmd = 3;


	PA_NetLibInit();
	RUDPStart();
       
	s = RUDPSocket();

	memset(&sai, 0, sizeof(sai));
	sai.sin_family = AF_INET;
	sai.sin_addr.s_addr = inet_addr("127.0.0.1");
	sai.sin_port = htons(5001);
	sa_len = sizeof(sai);

	for(i=1; i<argc; i++)
	{
		if(strchr(argv[i], '.'))
			sai.sin_addr.s_addr = inet_addr(argv[i]);
		else
			sai.sin_port = htons(atoi(argv[i]));
	}

	int opt;
	//opt = 256; RUDPSetSockOpt(s, OPT_RUDP_RCVBUF, &opt, sizeof(int));
	if( (rlt = RUDPConnect(s, (struct sockaddr*)&sai, sizeof(sai))) == 0)
	{
		char line[1024];
		int chn=0;
		while(fgets(line, 1024, stdin))
		{
			if(strncmp(line, "quit", 4) == 0) break;
			RUDPSend(s, chn, line, strlen(line)+1, 0);
			chn = !chn;
		}
	}

	if(s) RUDPClose(s);

	sleep(4);
	RUDPCleanup();

	return 0;
}
