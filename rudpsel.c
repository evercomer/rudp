#include "rudp.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

//===================================================================

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

	int ic = 0;
	char cBusy[] = "-\\|/";

	while(1)
	{
		struct timeval tv = { 0, 500000 };
		if(RUDPSelectSock(s, -1, RUDPSELECT_READABLE, &tv) > 0)
		{
			int sa_len = sizeof(sai);
			RUDPSOCKET a;

			if(RUDPAccept(s, &a, (struct sockaddr*)&sai, &sa_len) < 0)
			{
				printf("accept error\n");
			}
			else
				RUDPClose(a);
		}
		else
		{
#if 0
			printf("\r%c", cBusy[ic]); fflush(stdout);
			ic = (ic+1)%4;
#else
			printf("\r%d", ic++); fflush(stdout);
#endif
		}
	}

	RUDPClose(s);

	RUDPCleanup();

	return 0;
}
