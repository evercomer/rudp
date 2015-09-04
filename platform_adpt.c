#include "platform_adpt.h"
#include <stdlib.h>

#ifdef WIN32

void PA_NetLibInit()
{
	WSADATA wd;
	WSAStartup(0x0202, &wd);
}

int PA_Write(PA_HFILE hFile, const void *pBuff, unsigned int size)
{
	DWORD dwWritten;
	if( ! WriteFile(hFile, pBuff, size, &dwWritten, NULL) )
		return -1;
	return dwWritten;
}
int PA_Read(PA_HFILE hFile, void *pBuff, unsigned int size)
{
	DWORD dwReaden;
	if( ! ReadFile(hFile, pBuff, size, &dwReaden, NULL) )
		return -1;
	return dwReaden;
}

PA_HTHREAD PA_ThreadCreate(PA_ThreadRoutine* routine, void* data)
{
	return CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)routine, data, 0, NULL);
}

void *PA_ThreadWaitUntilTerminate(PA_HTHREAD hThread) 
{ 
	DWORD dwExitCode; 
	WaitForSingleObject(hThread, INFINITE); 
	GetExitCodeThread(hThread, &dwExitCode); 
	return (void*)dwExitCode; 
}

BOOL PA_PipeCreate(PA_PIPE *pHPipeRd, PA_PIPE *pHPipeWrt)
{
	return CreatePipe(pHPipeRd, pHPipeWrt, NULL, 0) == TRUE;
}
BOOL PA_PipeClose(PA_PIPE hPipe)
{
	return CloseHandle(hPipe);
}

int PA_SocketSetNBlk(PA_SOCKET s, BOOL b)
{
	return ioctlsocket(s, FIONBIO, (u_long*)&b);
}

/***************************** 
 *      Read-Write Lock
 *****************************/
enum { LOCK_LEVEL_NONE, LOCK_LEVEL_READ, LOCK_LEVEL_WRITE };

RWLOCK *RWLockCreate()
{
	RWLOCK *pLock = (RWLOCK*)malloc(sizeof(RWLOCK));
	pLock->m_currentLevel = LOCK_LEVEL_NONE;
	pLock->m_readerCount    =  pLock->m_writeCount = 0;
	pLock->m_writerId = 0;
	//pLock->m_unlockEvent  = CreateEvent( NULL, TRUE, FALSE, NULL );
	pLock->m_unlockEvent  = CreateEvent( NULL, FALSE, FALSE, NULL );
	pLock->m_accessMutex  = CreateMutex( NULL, FALSE, NULL );
	InitializeCriticalSection( &pLock->m_csStateChange );
	return pLock;
}

BOOL RWLockDestroy(RWLOCK *pLock)
{
	DeleteCriticalSection( &pLock->m_csStateChange );
	if ( pLock->m_accessMutex ) CloseHandle( pLock->m_accessMutex );
	if ( pLock->m_unlockEvent ) CloseHandle( pLock->m_unlockEvent );
	free(pLock);
	return TRUE;
}

static BOOL _Lock(RWLOCK *pLock, int level, DWORD timeout) 
{
	BOOL  bresult    = TRUE;
	DWORD waitResult = 0;

	waitResult = WaitForSingleObject( pLock->m_accessMutex, timeout );
	if ( waitResult != WAIT_OBJECT_0 )  return FALSE;

	if ( level == LOCK_LEVEL_READ && pLock->m_currentLevel != LOCK_LEVEL_WRITE )
	{
		EnterCriticalSection( &pLock->m_csStateChange );
		pLock->m_currentLevel = level;
		pLock->m_readerCount += 1;
		ResetEvent( pLock->m_unlockEvent );
		LeaveCriticalSection( &pLock->m_csStateChange );
	}
	else if ( level == LOCK_LEVEL_READ && 
			pLock->m_currentLevel == LOCK_LEVEL_WRITE )
	{
		waitResult = WaitForSingleObject( pLock->m_unlockEvent, timeout );
		if ( waitResult == WAIT_OBJECT_0 )
		{
			EnterCriticalSection( &pLock->m_csStateChange );
			pLock->m_currentLevel = level;
			pLock->m_readerCount += 1;
			ResetEvent( pLock->m_unlockEvent );
			LeaveCriticalSection( &pLock->m_csStateChange );
		}
		else bresult = FALSE;
	}
	else if ( level == LOCK_LEVEL_WRITE && 
			pLock->m_currentLevel == LOCK_LEVEL_NONE )
	{
		EnterCriticalSection( &pLock->m_csStateChange );
		pLock->m_currentLevel = level;
		pLock->m_writerId = GetCurrentThreadId();
		pLock->m_writeCount = 1;
		ResetEvent( pLock->m_unlockEvent );
		LeaveCriticalSection( &pLock->m_csStateChange );
	}
	else if ( level == LOCK_LEVEL_WRITE && 
			pLock->m_currentLevel != LOCK_LEVEL_NONE )
	{
		DWORD id = GetCurrentThreadId();
		if(id == pLock->m_writerId) pLock->m_writeCount++;
		else
		{
			waitResult = WaitForSingleObject( pLock->m_unlockEvent, timeout );
			if ( waitResult == WAIT_OBJECT_0 )
			{
				EnterCriticalSection( &pLock->m_csStateChange );
				pLock->m_currentLevel = level;
				pLock->m_writerId = GetCurrentThreadId();
				pLock->m_writeCount = 1;
				ResetEvent( pLock->m_unlockEvent );
				LeaveCriticalSection( &pLock->m_csStateChange );
			}
			else bresult = FALSE;
		}
	}

	ReleaseMutex( pLock->m_accessMutex );
	return bresult;

} // lock()

BOOL RWLockLockR(RWLOCK *pLock, DWORD timeout)
{
	return _Lock(pLock, LOCK_LEVEL_READ, timeout);
}

BOOL RWLockLockW(RWLOCK *pLock, DWORD timeout)
{
       return _Lock(pLock, LOCK_LEVEL_WRITE, timeout); 
}

void RWLockUnlock(RWLOCK *pLock)
{ 
	EnterCriticalSection( &pLock->m_csStateChange );
	if ( pLock->m_currentLevel == LOCK_LEVEL_READ )
	{
		pLock->m_readerCount --;
		if ( pLock->m_readerCount == 0 ) 
		{
			pLock->m_currentLevel = LOCK_LEVEL_NONE;
			SetEvent (pLock->m_unlockEvent);
		}
	}
	else if ( pLock->m_currentLevel == LOCK_LEVEL_WRITE )
	{
		pLock->m_writeCount--;
		if(pLock->m_writeCount == 0)
		{
			pLock->m_currentLevel = LOCK_LEVEL_NONE;
			pLock->m_writerId = 0;
			SetEvent ( pLock->m_unlockEvent );
		}
	}
	LeaveCriticalSection( &pLock->m_csStateChange );
}


/////////////////////////////////////////////////////////
#elif defined(__LINUX__) || defined(__ANDROID__)

BOOL PA_EventWaitTimed(PA_EVENT e, DWORD ms)
{
	struct timespec ts;

#if 0
	clock_gettime(CLOCK_MONOTONIC, &ts);
	ts.tv_sec += ms/1000;
	ts.tv_nsec += (ms%1000)*1000000;
#else
	gettimeofday((struct timeval*)&ts, NULL);
	ts.tv_nsec *= 1000;
	ts.tv_sec += ms/1000;
	ts.tv_nsec += (ms%1000)*1000000;
	if(ts.tv_nsec > 1000000000) {
		ts.tv_nsec -= 1000000000;
		ts.tv_sec ++;
	}
#endif
	return sem_timedwait(e, &ts) == 0;
}

PA_HTHREAD PA_ThreadCreate(PA_ThreadRoutine* routine, void* data)
{
	pthread_t thd;
	if(pthread_create(&thd, NULL, routine, data) == 0)
		return thd;
	else
		return 0;
}

void* PA_ThreadWaitUntilTerminate(PA_HTHREAD hThread) 
{ 
	void *tmp; 
	pthread_join(hThread, &tmp); return tmp; 
}

BOOL PA_PipeCreate(PA_PIPE *pHPipeRd, PA_PIPE *pHPipeWrt)
{
	PA_PIPE fds[2];
	if(pipe(fds) < 0) return FALSE;
	*pHPipeRd = fds[0];
	*pHPipeWrt = fds[1];
	return TRUE;
}
BOOL PA_PipeClose(PA_PIPE hPipe)
{
	return 0 == close(hPipe);
}

void PA_Sleep(UINT ms)
{
	struct timeval tv;
	tv.tv_sec = ms/1000;
	tv.tv_usec = (ms%1000)*1000;
	select(0, NULL, NULL, NULL, &tv);
}

DWORD PA_GetTickCount()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return 1000*ts.tv_sec + ts.tv_nsec/1000000;
}

BOOL PA_DeleteFile(const char *fn)
{
	return unlink(fn) == 0; 
}

int PA_SocketSetNBlk(PA_SOCKET s, BOOL b)
{
	int opt = fcntl(s, F_GETFL, &opt, 0);
	if(b) opt |= O_NONBLOCK;
	else opt &= ~O_NONBLOCK;
	return fcntl(s, F_SETFL, opt);
}

/***********************
 *    Read-Write Lock
 ***********************/
BOOL _RWLockLockR(pthread_rwlock_t *lock, DWORD timeout)
{
	if(timeout == INFINITE)
		return pthread_rwlock_rdlock(lock) == 0;
	else
	{
		while(!pthread_rwlock_tryrdlock(lock) && timeout > 0)
		{
			usleep(10000);
			if(timeout > 10) timeout -= 10;
			else return FALSE;
		}
		return TRUE;
	}
}
BOOL _RWLockLockW(pthread_rwlock_t *lock, DWORD timeout)
{
	if(timeout == INFINITE)
		return pthread_rwlock_wrlock(lock)==0;
	else
	{
		while(!pthread_rwlock_trywrlock(lock) && timeout > 0)
		{
			usleep(10000);
			if(timeout > 10) timeout -= 10;
			else return FALSE;
		}
		return TRUE;
	}
}

#elif defined(ARM_UCOS_LWIP)
PA_HTHREAD PA_ThreadCreate(PA_ThreadRoutine* routine, void* data)
{
	//sys_thread_t sys_thread_new(char *name, void (* thread)(void *arg), void *arg, int stacksize, int prio)
	return sys_thread_new(NULL, routine, data, 0, 10);
}
void* PA_ThreadWaitUntilTerminate(PA_HTHREAD hThread)
{
	return NULL;
}

int PA_SocketSetNBlk(PA_SOCKET s, BOOL b)
{
	return lwip_ioctl(s, FIONBIO, (void*)&b);
}
#endif	//linux

#ifdef __ANDROID__
void android_log(int level, const char *tag, const char *sfmt, ...)
{
	va_list va;
	char buf[1024];
	va_start(va, sfmt);
	vsnprintf(buf, sizeof(buf), sfmt, va);
	va_end(va);
	__android_log_write(level, tag, buf);
}
#endif

int PA_SocketSetLinger(PA_SOCKET s, int onoff, int linger)
{
	struct linger opt = { onoff, linger };
	return setsockopt(s, SOL_SOCKET, SO_LINGER, 
#ifdef WIN32
		(const char*)&opt, 
#else
		&opt,
#endif
		sizeof(opt));
}

#ifdef _DEBUG
void dbg_bin(const char *title, const void *p, int size)
{
	int i;
	unsigned char *byts = (unsigned char*)p;
	printf(title);
	for(i=0; i<size; i++)
	{
		printf("%02X ", byts[i]);
		if(i>0 && (i&31) == 31) printf("\n");
	}
	printf("\n");
}
#else
#define dbg_bin(x, y, z)
#endif
