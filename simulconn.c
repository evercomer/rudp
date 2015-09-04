/* 
 * In this demo, two peers create(does not listen) 
 * a rudp socket and connect to each other, simultaneously.
 *
 */
#include "rudp.h"
#include <string.h>
#include <stdio.h>
#include "platform_adpt.h"

RUDPSOCKET s;
int	udp_sock;

PA_THREAD_RETTYPE __STDCALL recv_thread(void *p)
{
	struct timeval tv = { 1, 0 };
	RUDPSOCKCHNO rset[2];
	int nr, rlt;
	char line[1000];

	while(1)
	{
		nr = 0;
		RUDP_FD_SET(udp_sock, rset, nr);
		RUDP_SET(s, 0, rset, nr);
		if((rlt = RUDPSelect(rset, &nr, NULL, NULL, NULL, NULL, &tv)) > 0)
		{
			int chno, len, rudp;

			if(RUDP_ISSET(s, rset, nr))
			{
				len = RUDPRecv(s, &chno, line, 1000, 0);
				rudp = 1;
			}
			else if(RUDP_FD_ISSET(udp_sock, rset, nr))
			{
				struct sockaddr sa;
				int sa_len = sizeof(sa);
				len = recvfrom(udp_sock, line, 1000, 0, &sa, &sa_len);
				rudp = 0;
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
	return (PA_THREAD_RETTYPE)0;
}

int main(int argc, char *argv[])
{
	struct sockaddr_in sai;
	int i, connected = 0;
	char line[1000];

	if(argc < 2) { printf("simulconn remotehost [localport remoteport]\n"); return -1; }


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
	i = 1;
	//RUDPSetSockOpt(s, OPT_ADHOC, &i, sizeof(i));

	sai.sin_port = htons(ntohs(sai.sin_port)+1);
	udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if(bind(udp_sock, (struct sockaddr*)&sai, sizeof(sai)) < 0)
	{
		perror("bind udp");
		RUDPCleanup();
		return -1;
	}


	if(argc > 3) sai.sin_port = htons(atoi(argv[3]));
	sai.sin_addr.s_addr = inet_addr(argv[1]);
	PA_Sleep(5000);
	printf("begin connection....\n");
	for(i=0; i<3; i++)
	{
		if(RUDPConnect(s, (struct sockaddr*)&sai, sizeof(sai)) == 0)
		{
			connected = 1;
			break;
		}
		else
		{
			printf("connection trying %d\n", i);
			PA_Sleep(1000);
		}
	}

	sai.sin_port = htons(ntohs(sai.sin_port)+1);

	if(connected)
	{
		PA_HTHREAD thd;
		thd = PA_ThreadCreate(recv_thread, NULL);

		while(fgets(line, 1000, stdin))
		{
			int rlt, flag = RUDPSELECT_WRITABLE;
			struct timeval tv = { 0, 0 };
			rlt = RUDPSelectSock(s, 0, flag, &tv);
			if(rlt > 0)
			{
				if(line[0] & 0x01)
				{
					if( (rlt = RUDPSend(s, 0, line, strlen(line), 0)) < 0)
					{
						printf("RUDPSend: %d\n", rlt);
						break;
					}
				}
				else
					sendto(udp_sock, line, strlen(line), 0, (struct sockaddr*)&sai, sizeof(sai));
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
