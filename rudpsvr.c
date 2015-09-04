#include "rudp.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#define SERVER_SEND	0x01
#define SERVER_RECV	0x02
struct comm_stat {
	RUDPSOCKET sock;
	float br_recv, br_send;
	int cmd;
};
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
volatile static struct comm_stat br_counter[10];

//===================================================================
int sendData(struct comm_stat *pcs)
{
	char line[3000];
	int i=0, rlt = 0;
	int total = 0;
	struct timeval tv1, tv2;

	gettimeofday(&tv1, NULL);
	memset(line, '1' + i%36, 3000);

	unsigned long n_byt;
	for(i=0; 0<50000; i++)
	{
		struct timeval tv = { 0, 25000 };
		int flag = RUDPSELECT_WRITABLE;
		PA_IOVEC v[3];
		PA_IoVecSetPtr(&v[0], line);
		PA_IoVecSetPtr(&v[1], line);
		PA_IoVecSetPtr(&v[2], line);
		PA_IoVecSetLen(&v[0], 230);
		PA_IoVecSetLen(&v[1], 1100);
		PA_IoVecSetLen(&v[2], 100);

		n_byt = PA_IoVecGetLen(&v[0]) + PA_IoVecGetLen(&v[1]) + PA_IoVecGetLen(&v[2]);
		if(RUDPSelectSock(pcs->sock, 1, flag, &tv) > 0)
		{
			rlt = RUDPSendV(pcs->sock, 1, v, 3, 0);
			if(rlt < 0)
			{
				printf("RUDPSend error: %d\n", rlt);
				break;
			}
			else
			{
				total += n_byt;
			}
		}

		gettimeofday(&tv2, NULL);
		if(tv2.tv_sec - tv1.tv_sec >= 1)
		{
			float delta = (tv2.tv_sec - tv1.tv_sec) + ((int)(tv2.tv_usec/1000) - (int)(tv1.tv_usec/1000))/1000.0;
			pcs->br_send = total/delta*8/1024/1024.0;
			total = 0;
			tv1 = tv2;
		}
	}

	printf("***************************************\n");
	if(rlt > 0)
	{
		line[0] = '\0';
		RUDPSend(pcs->sock, 0, line, 1, 0);
		printf("all data sent.\n");
	}
	return 0;
}

void recvData(struct comm_stat *pcs)
{
	int cnt = 0;
	char line[2100];
	int len, chno;
	int total = 0;
	struct timeval tv1, tv2;

	gettimeofday(&tv1, NULL);
	while( 1 )
	{
		struct timeval tv = { 0, 500000 };
		int flag = RUDPSELECT_READABLE, rlt;
		if(1)//(rlt = RUDPSelectSock(pcs->sock, -1, flag, &tv)) > 0)
		{
			if((len = RUDPRecv(pcs->sock, &chno, line, 2000, 0)) > 0)
			{
				if(chno == 0)
				{
					//fprintf(stderr, "\rPacket %d from chn 0", cnt); fflush(stderr);
				}
				cnt++;
				line[len] = '\0';
				if(line[0] == '\0') break;
				//printf("%s\n", line);
				total += len;
			}
			else if(len < 0)
			{
				fprintf(stderr, "RUDPRecv: %d\n", len);
				exit(-1);
			}
			gettimeofday(&tv2, NULL);
			if(tv2.tv_sec - tv1.tv_sec >= 1)
			{
				float delta = (tv2.tv_sec - tv1.tv_sec) + ((int)(tv2.tv_usec/1000) - (int)(tv1.tv_usec/1000))/1000.0;
				pcs->br_recv= total/delta*8/1024/1024.0;
				total = 0;
				tv1 = tv2;
			}
		} else if(rlt < 0)
		{
			printf("ERROR: RUDPSelectSock: %d\n", rlt);
			exit(-1);
		}
	}
	printf("receiving finished.\n");
}
#if 0
int sendRecvData(struct comm_stat *pcs)
{
	char line[3000];
	int rlt = 0;
	int totalr, totalw;
	struct timeval tv1, tv2;

	totalr = totalw = 0;
	gettimeofday(&tv1, NULL);
	memset(line, '1', 3000);

	unsigned long n_byt;
	while(1)
	{
		struct timeval tv = { 0, 50000 };
		int flag = 0;
		PA_IOVEC v[3];
		v[0].iov_base = v[1].iov_base = v[2].iov_base = line;
		v[0].iov_len = 230;
		v[1].iov_len = 1100;
		v[2].iov_len = 100;
		n_byt = v[0].iov_len + v[1].iov_len + v[2].iov_len;

		if(pcs->cmd & SERVER_SEND) flag |= RUDPSELECT_WRITABLE;
		if(pcs->cmd & SERVER_RECV) flag |= RUDPSELECT_READABLE;
		if(RUDPSelectSock(pcs->sock, 0, &flag, &tv) > 0) //Not supported
		{
			if(flag & RUDPSELECT_WRITABLE)
			{
				rlt = RUDPSendV(pcs->sock, 0, v, 3, 0);
				if(rlt < 0)
				{
					printf("RUDPSend error: %d\n", rlt);
					break;
				}
				else
				{
					totalw += n_byt;
				}
			}
			if(flag & RUDPSELECT_READABLE)
			{
				int chno, len;
				if((len = RUDPRecv(pcs->sock, &chno, line, 2000, 0)) > 0)
				{
					totalr += len;
				}
				else if(len < 0)
				{
					fprintf(stderr, "RUDPRecv: %d\n", len);
					exit(-1);
				}
			}
		}

		gettimeofday(&tv2, NULL);
		if(tv2.tv_sec - tv1.tv_sec >= 1)
		{
			float delta = (tv2.tv_sec - tv1.tv_sec) + ((int)(tv2.tv_usec/1000) - (int)(tv1.tv_usec/1000))/1000.0;
			pcs->br_send = totalw/delta*8/1024/1024.0;
			pcs->br_recv = totalr/delta*8/1024/1024.0;
			totalr = totalw = 0;
			tv1 = tv2;
		}
	}

	printf("***************************************\n");
	if(rlt > 0)
	{
		line[0] = '\0';
		RUDPSend(pcs->sock, 0, line, 1, 0);
		printf("all data sent.\n");
	}
	return 0;
}

void *threadSendRecv(void *p)
{
	struct comm_stat *pcs = (struct comm_stat*)p;
	pthread_detach(pthread_self());

	sendRecvData(pcs);

	RUDPClose(pcs->sock);
	memset(pcs, 0, sizeof(*pcs));
	sleep(5);
	return NULL;
}

#endif

void *threadSend(void *p)
{
	struct comm_stat *pcs = (struct comm_stat*)p;
	pthread_detach(pthread_self());

	//recvData(pcs);
	sendData(pcs);

	RUDPClose(pcs->sock);
	memset(pcs, 0, sizeof(*pcs));
	sleep(5);
	return NULL;
}

void* threadRecv(void *p)
{
	struct comm_stat *pcs = (struct comm_stat*)p;
	pthread_detach(pthread_self());

	recvData(pcs);
	//sendData(pcs);

	RUDPClose(pcs->sock);
	memset(pcs, 0, sizeof(*pcs));
	sleep(5);
	return NULL;
}

void *threadPrint(void *p)
{
	int i, len;
	char ss[128];

	pthread_detach(pthread_self());
	while(1)
	{
		sleep(1);
		len = sprintf(ss, "Mbps recv|send::: ");
		for(i=0; i<5; i++)
		{
			len += sprintf(ss+len, "%d: %.2f|%.2f; ", i, br_counter[i].br_recv, br_counter[i].br_send);
		}
		fprintf(stderr, "\r%s", ss); fflush(stderr);
	}
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

	pthread_create(&thd, NULL, threadPrint, NULL);
	while(1)
	{
		if(RUDPAccept(s, &a, (struct sockaddr*)&sai, &sa_len) < 0)
		{
			printf("accept error\n");
		}
		else
		{
			int i;
			for(i=0; i<10; i++)
			{
				if(br_counter[i].sock == NULL)
				{
					char cmd;
					int chno, len;
					br_counter[i].sock = a;

					while((len = RUDPRecv(a, &chno, &cmd, 1, 0)) > 0)
					{
						if( chno == 0 )
							break;
					}
					printf("client cmd: %d\n", cmd);
					br_counter[i].cmd = cmd;
#if 0
					pthread_create(&thd, NULL, threadSendRecv, (void*)&br_counter);
#else
					if(cmd & SERVER_SEND)
						pthread_create(&thd, NULL, threadSend, (void*)&br_counter[i]);
					if(cmd & SERVER_RECV)
						pthread_create(&thd, NULL, threadRecv, (void*)&br_counter[i]);
#endif
					break;
				}
			}
		}
	}

	RUDPClose(s);

	RUDPCleanup();

	return 0;
}
