#include "rudp.h"
#include "platform_adpt.h"
#include <string.h>
#include <stdio.h>
#include <signal.h>

int g_quit = 0;
int async = 0;
void sig_handler(int sig)
{
	if(sig == SIGINT || sig == SIGTERM)
	{
		g_quit = 1;
	}
}

int sendData(RUDPSOCKET s)
{
	char line[3000];
	int i=0, rlt = 0;
	memset(line, '1' + i%36, 3000);

	time_t t0, t_prev, t;
	unsigned long nTotal = 0, nByt = 0;
	t0 = t_prev = time(NULL);
	for(i=0; 0<50000 && !g_quit; i++)
	{
		PA_IOVEC v[3];
		PA_IoVecSetPtr(&v[0], line);
		PA_IoVecSetPtr(&v[1], line);
		PA_IoVecSetPtr(&v[2], line);
		PA_IoVecSetLen(&v[0], 230);
		PA_IoVecSetLen(&v[1], 1100);
		PA_IoVecSetLen(&v[2], 100);

		if( (i % 100) == 0)
		{
			rlt = RUDPSendV(s, 0, v, 3, 0);
		}
		else
			rlt = RUDPSendV(s, 0/*1*/, v, 3, 0);
		if(rlt < 0)
		{
			if(rlt == ERUDP_AGAIN)
			{
				//usleep(10);
				continue;
			}
			printf("RUDPSend error: %d\n", rlt);
			break;
		}

		int onesent = PA_IoVecGetLen(&v[0]) + PA_IoVecGetLen(&v[1]) + PA_IoVecGetLen(&v[2]);
		nTotal += onesent;
		nByt += onesent;

		time(&t);
		if(t != t_prev)
		{
			fprintf(stderr, "rate: Avg.: %d B/s, Rt. %d B/s        \r", nTotal/(t-t0), nByt);
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
	while( !g_quit )
	{
		struct timeval tv = { 0, 500000 };
		int flag = RUDPSELECT_READABLE, rlt;
		if( (rlt = RUDPSelectSock(a, -1, flag, &tv)) > 0)
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
				if(len == ERUDP_AGAIN)
				{
					//usleep(10);
					continue;
				}
				fprintf(stderr, "RUDPRecv: %d\n", len);
				exit(-1);
			}
		} 
		else if(rlt < 0)
		{
				if(rlt == ERUDP_AGAIN)
				{
					//usleep(10);
					continue;
				}
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
	int rlt, i;
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
		if(argv[i][0] == '-')
		{
			if(strcmp("-a", argv[i]) == 0)
			{
				async = 1;
			}
			else if(strcmp("--recvonly", argv[i]) == 0)
				cmd &= ~SERVER_RECV;
			else if(strcmp("--sendonly", argv[i]) == 0)
				cmd &= ~SERVER_SEND;
			else if(strcmp("-?", argv[i]) == 0)
			{
				printf("Usage: rudpclt [-a] [server] [port]\n"
					"\t-a: asynchronous\n"
					"\tserver: default is 127.0.0.1\n"
					"\tport: 5001 by default\n");
				exit(0);
			}

		}
		else if(strchr(argv[i], '.'))
			sai.sin_addr.s_addr = inet_addr(argv[i]);
		else
			sai.sin_port = htons(atoi(argv[i]));
	}

	int opt;
	//opt = 256; RUDPSetSockOpt(s, OPT_RUDP_RCVBUF, &opt, sizeof(int));
	if( (rlt = RUDPConnect(s, (struct sockaddr*)&sai, sizeof(sai))) == 0)
	{
			opt = 256; RUDPSetSockOpt(s, OPT_RUDP_SNDBUF, &opt, sizeof(int));
			if(async) RUDPSetSockOpt(s, OPT_NBLK, &async, sizeof(async));
			pthread_t thd;
			RUDPSend(s, 0, &cmd, 1, 0);
			//sleep(5);
			if(cmd & SERVER_RECV) pthread_create(&thd, NULL, threadSend, (void*)s);
			recvData(s);
	}
	else if(rlt == -ERUDP_AGAIN)
	{
		RUDPClose(s);
		s = NULL;
	}

	if(s) RUDPClose(s);

	sleep(4);
	RUDPCleanup();

	return 0;
}
