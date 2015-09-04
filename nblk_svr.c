#include "rudp.h"
#include <string.h>
#include <stdio.h>

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

	int opt = 1, rlt = 0;
	//RUDPSetSockOpt(s, OPT_NBLK, &opt, sizeof(opt));

	while(1)
	{
		int r;
		struct timeval tv = { 1, 0 };
#if 0
		r = RUDPSELECT_READABLE;
		rlt = RUDPSelectSock(s, -1, &r, &tv);
		if(rlt < 0)
		{
			printf("RUDPSelectSock: %d\n", rlt);
			break;
		}
		if(rlt == 0)
		{
			printf("No incoming connection\n");
			continue;
		}
#else
		RUDPSOCKCHNO r_schs[2];
		int n_schs = 0;
		RUDP_SET(s, -1, r_schs, n_schs);
		rlt = RUDPSelect(r_schs, &n_schs, NULL, 0, NULL, 0, &tv);
		if(rlt < 0)
		{
			printf("RUDPSelect: %d\n", rlt);
			break;
		}
		if(rlt == 0)
		{
			printf("No incoming connection\n");
			continue;
		}
#endif
		int sa_len = sizeof(sai);
		RUDPSOCKET a;
		if(RUDPAccept(s, &a, (struct sockaddr*)&sai, &sa_len) < 0)
		{
			printf("accept error\n");
		}
		else
		{
			char line[1100];
			int len, chno;
			tv.tv_sec = 1; tv.tv_usec = 0;
			while(1)
			{
				r = RUDPSELECT_READABLE;
				if( (r = RUDPSelectSock(a, -1, &r, &tv)) < 0 )
				{
					printf("RUDPSelectSock on accepted socket: %d\n", r);
					break;
				}
				if(r == 0) { continue; }

				if( (len = RUDPRecv(a, &chno, line, 1000, 0)) > 0)
				{
					line[len] = '\0';
					//printf("%s\n", line);
					if(line[0] == '\0') break;
				}
			}
			printf("receiving finished.\n");
			RUDPClose(a);
		}
	}

	RUDPClose(s);

	RUDPCleanup();

	return 0;
}
