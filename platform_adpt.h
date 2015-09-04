#ifndef __platform_adpt_h__
#define __platform_adpt_h__

#include "basetype.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#ifdef WIN32	//Windows
#include <winsock2.h>
#include <ws2tcpip.h>

#if defined(__cplusplus) && defined(USE_MFC)
#include <afx.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif


#ifdef __cplusplus
extern "C" {
#endif

/*
 *  SOCKET Functions
 */
void PA_NetLibInit();
#define PA_NetLibUninit()	WSACleanup()

#define PA_IOVEC	WSABUF
#define PA_IoVecGetPtr(pvec) ((pvec)->buf)
#define PA_IoVecGetLen(pvec) ((pvec)->len)
#define PA_IoVecSetPtr(pvec, ptr) (pvec)->buf = (char*)(ptr)
#define PA_IoVecSetLen(pvec, _len) (pvec)->len = _len

#define PA_SocketClose closesocket
#define CloseSocket	closesocket
#define PA_Send(s, p, len, f) send(s, (const char*)p, len, f)
#define PA_SendTo(s, p, len, f, paddr, alen) sendto(s, (const char*)p, len, f, paddr, alen)
#define PA_Recv(s, p, len, f) recv(s, (char*)p, len, f)
#define PA_RecvFrom(s, p, len, f, paddr, palen) recvfrom(s, (char*)p, len, f, paddr, palen)
#define PA_GetSockName(s, paddr, paddr_len) getsockname(s, paddr, (int*)paddr_len)
#define PA_GetPeerName(s, paddr, paddr_len) getpeername(s, paddr, (int*)paddr_len)
#define PA_Accept(s, paddr, paddr_len) accept(s, paddr, paddr_len)
#define PA_GetSockOpt(s, level, optname, optval, optlen) getsockopt(s, level, optname, (char*)optval, (int*)optlen)
#define PA_SetSockOpt(s, level, optname, optval, optlen) setsockopt(s, level, optname, (const char*)optval, optlen)

#define PA_SOCKET	SOCKET
#define PA_SocketIsValid(s) (s!=INVALID_SOCKET)
#define PA_SocketGetError() WSAGetLastError()
#define PA_SOCKET_ERROR	SOCKET_ERROR	//return value of socket operations


#define EWOULDBLOCK             WSAEWOULDBLOCK
#define EINPROGRESS             WSAEINPROGRESS
#define EALREADY                WSAEALREADY
#define ENOTSOCK                WSAENOTSOCK
#define EDESTADDRREQ            WSAEDESTADDRREQ
#define EMSGSIZE                WSAEMSGSIZE
#define EPROTOTYPE              WSAEPROTOTYPE
#define ENOPROTOOPT             WSAENOPROTOOPT
#define EPROTONOSUPPORT         WSAEPROTONOSUPPORT
#define ESOCKTNOSUPPORT         WSAESOCKTNOSUPPORT
#define EOPNOTSUPP              WSAEOPNOTSUPP
#define EPFNOSUPPORT            WSAEPFNOSUPPORT
#define EAFNOSUPPORT            WSAEAFNOSUPPORT
#define EADDRINUSE              WSAEADDRINUSE
#define EADDRNOTAVAIL           WSAEADDRNOTAVAIL
#define ENETDOWN                WSAENETDOWN
#define ENETUNREACH             WSAENETUNREACH
#define ENETRESET               WSAENETRESET
#define ECONNABORTED            WSAECONNABORTED
#define ECONNRESET              WSAECONNRESET
#define ENOBUFS                 WSAENOBUFS
#define EISCONN                 WSAEISCONN
#define ENOTCONN                WSAENOTCONN
#define ESHUTDOWN               WSAESHUTDOWN
#define ETOOMANYREFS            WSAETOOMANYREFS
#define ETIMEDOUT               WSAETIMEDOUT
#define ECONNREFUSED            WSAECONNREFUSED
#define ELOOP                   WSAELOOP
#define EHOSTDOWN               WSAEHOSTDOWN
#define EHOSTUNREACH            WSAEHOSTUNREACH
#define EPROCLIM                WSAEPROCLIM
#define EUSERS                  WSAEUSERS
#define EDQUOT                  WSAEDQUOT
#define ESTALE                  WSAESTALE
#define EREMOTE                 WSAEREMOTE



/*
 *
 */
#define PA_INVALID_HANDLE	INVALID_HANDLE_VALUE
#define PA_IsValidHandle(handle) (handle != INVALID_HANDLE_VALUE)

/*
 *  Synchronous Objects
 */
#define PA_MUTEX	HANDLE
#define PA_EVENT	HANDLE
#define PA_COND		HANDLE
#define PA_SEM		HANDLE	//semaphore
#define PA_PIPE	HANDLE
#define PA_SPIN	CRITICAL_SECTION

#define PA_DEFINEMUTEX(x) PA_MUTEX x = CreateMutex(NULL, FALSE, NULL)
#define PA_MutexInit(x) x = CreateMutex(NULL, FALSE, NULL)
#define PA_MutexUninit(x) CloseHandle(x)
#define PA_MutexLock(x) WaitForSingleObject(x, INFINITE)
#define PA_MutexUnlock(x) ReleaseMutex(x)
#define PA_MutexTryLock(x) (WaitForSingleObject(x, 0) == WAIT_OBJECT_0)

#define PA_SpinInit(x) InitializeCriticalSection(&x)
#define PA_SpinUninit(x) DeleteCriticalSection(&x)
#define PA_SpinLock(x) EnterCriticalSection(&x)
#define PA_SpinTryLock(x) TryEnterCriticalSection(&x)
#define PA_SpinUnlock(x) LeaveCriticalSection(&x)

#define PA_EventInit(x) x = CreateEvent(NULL, FALSE, FALSE, NULL)
#define PA_EventUninit(x) CloseHandle(x)
#define PA_EventSet(x) SetEvent(x)
//#define PA_ResetEvent(x) ResetEvent(x)
#define PA_EventWait(x) WaitForSingleObject(x, INFINITE)
#define PA_EventWaitTimed(e, ms) (WaitForSingleObject(e, ms)==WAIT_OBJECT_0)

#define PA_SemInit(x, max_value) x = CreateSemaphore(NULL, 0, max_value, NULL)
#define PA_SemUninit(x)	CloseHandle(x)
#define PA_SemWait(x) WaitForSingleObject(x, INFINITE)
#define PA_SemPost(x) ReleaseSemaphore(x, 1, NULL)

/*
 *  Threads
 */
#define PA_HTHREAD	HANDLE
#define PA_HTHREAD_NULL	NULL
#define PA_THREAD_RETTYPE	DWORD
typedef PA_THREAD_RETTYPE (__STDCALL PA_ThreadRoutine)(void*);
#define PA_ThreadGetCurrentHandle() GetCurrentThread()
#define PA_ThreadCloseHandle(hThread) CloseHandle(hThread)
void *PA_ThreadWaitUntilTerminate(PA_HTHREAD hThread);

#define PA_Sleep(ms) Sleep(ms)

/*
 * Read-Write Lock
 */
typedef struct ReadWriteLock
{
	int    m_currentLevel;
	int    m_readerCount, m_writeCount;
	DWORD  m_writerId;
	HANDLE m_unlockEvent; 
	HANDLE m_accessMutex;
	CRITICAL_SECTION m_csStateChange;
} RWLOCK, *LPRWLOCK;

RWLOCK *RWLockCreate();
BOOL RWLockDestroy(RWLOCK *pLock);
BOOL RWLockLockR(RWLOCK *pLock, DWORD timeout);
BOOL RWLockLockW(RWLOCK *pLock, DWORD timeout);
void RWLockUnlock(RWLOCK *pLock);

#define PA_RWLOCK	LPRWLOCK
#define PA_RWLockInit(x) x = RWLockCreate()
#define PA_RWLockUninit(x) RWLockDestroy(x)
#define PA_RWLockLockR(x) RWLockLockR(x, INFINITE)
#define PA_RWLockLockW(x) RWLockLockW(x, INFINITE)
#define PA_RWLockFailed(op) (op == FALSE)
#define PA_RWLockLockRTimed(x, timeout) RWLockLockR(x, timeout)
#define PA_RWLockLockWTimed(x, timeout) RWLockLockW(x, timeout)
#define PA_RWLockUnlock(x) RWLockUnlock(x)


/*
 *  String functions
 */
#define PA_StrCaseCmp stricmp
#define PA_StrNCaseCmp strnicmp
#define PA_StrNCmp	strncmp

/*
 *  File Operations
 */
#define PA_HFILE	HANDLE
int PA_Write(PA_HFILE hFile, const void *pBuff, unsigned int size);
int PA_Read(PA_HFILE hFile, void *pBuff, unsigned int size);
#define PA_FileClose(hf) CloseHandle(hf)
#define PA_FileIsValid(h) (h!=INVALID_HANDLE_VALUE)
#define PA_DeleteFile(f)	DeleteFile(f)

/*
 *  Time
 */
#define PA_GetTickCount() GetTickCount()

#ifdef __cplusplus
}
#endif

#elif defined(__LINUX__) || defined(__ANDROID__)
//
// OS:  Linux
//

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <netdb.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <semaphore.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  SOCKET Functions
 */
#define PA_NetLibInit()
#define PA_NetLibUninit()

#define PA_IOVEC	struct iovec	
#define PA_IoVecGetPtr(pvec) ((pvec)->iov_base)
#define PA_IoVecGetLen(pvec) ((pvec)->iov_len)
#define PA_IoVecSetPtr(pvec, ptr) (pvec)->iov_base = (void*)(ptr)
#define PA_IoVecSetLen(pvec, len) (pvec)->iov_len = len

#define PA_SocketClose close
#define CloseSocket	close
#define PA_Send send
#define PA_SendTo sendto
#define PA_Recv recv
#define PA_RecvFrom(s, buf, size, flags, paddr, paddr_len) recvfrom(s, buf, size, flags, paddr, (socklen_t*)paddr_len)
#define PA_GetSockName(s, paddr, paddr_len) getsockname(s, paddr, (socklen_t*)paddr_len)
#define PA_GetPeerName(s, paddr, paddr_len) getpeername(s, paddr, (socklen_t*)paddr_len)
#define PA_Accept(s, paddr, paddr_len) accept(s, paddr, (socklen_t*)paddr_len)
#define PA_GetSockOpt(s, level, optname, optval, optlen) getsockopt(s, level, optname, optval, (socklen_t*)optlen)
#define PA_SetSockOpt setsockopt

#define PA_SOCKET	int
#define INVALID_SOCKET	-1	
#define PA_SocketIsValid(s) (s>=0)
#define PA_SocketGetError() errno
#define PA_SOCKET_ERROR	-1	//return value of socket operations

/*
 *
 */
#define PA_IsValidHandle(fd) (fd>=0)
#define PA_INVALID_HANDLE -1

/*
 *  Synchronous Objects
 */
#define PA_MUTEX	pthread_mutex_t
#define PA_PIPE	int

#define PA_EVENT sem_t*
#define PA_SEM	sem_t

#define PA_DEFINEMUTEX(x) pthread_mutex_t x = PTHREAD_MUTEX_INITIALIZER
#define PA_MutexInit(x) pthread_mutex_init(&x, NULL)
#define PA_MutexUninit(x) pthread_mutex_destroy(&x)
#define PA_MutexLock(x) pthread_mutex_lock(&x)
#define PA_MutexUnlock(x) pthread_mutex_unlock(&x)
#define PA_MutexTryLock(x) (pthread_mutex_trylock(&x) == 0)

#ifdef HAVE_SPIN_T
#define PA_SPIN	pthread_spinlock_t
#define PA_SpinInit(x) pthread_spin_init(&x, PTHREAD_PROCESS_PRIVATE)
#define PA_SpinUninit(x) pthread_spin_destroy(&x)
#define PA_SpinLock(x) pthread_spin_lock(&x)
#define PA_SpinTryLock(x) pthread_spin_trylock(&x)
#define PA_SpinUnlock(x) pthread_spin_unlock(&x)
#else
#define PA_SPIN	pthread_mutex_t
#define PA_SpinInit(x) pthread_mutex_init(&x, NULL)
#define PA_SpinUninit(x) pthread_mutex_destroy(&x)
#define PA_SpinLock(x) pthread_mutex_lock(&x)
#define PA_SpinTryLock(x) pthread_mutex_trylock(&x)
#define PA_SpinUnlock(x) pthread_mutex_unlock(&x)
#endif

#define PA_EventInit(x)		do { x=(sem_t*)malloc(sizeof(sem_t)); sem_init(x, 0, 0); } while(0)
#define PA_EventUninit(x)	do { sem_destroy(x); free(x); } while(0)
#define PA_EventSet(x)		sem_post(x) 
#define PA_EventWait(x)		sem_wait(x)
BOOL PA_EventWaitTimed(PA_EVENT e, DWORD ms);

#define PA_SemInit(x, max_value) do { x = (sem_t*)malloc(sizeof(sem_t)); sem_init(x, 0, max_value); } while(0)
#define PA_SemUninit(x)	CloseHandle(x)	{ sem_destroy(x); free(x); }
#define PA_SemWait(x) sem_wait(x)
#define PA_SemPost(x) sem_post(x)

/*
 *  Threads
 */
#define PA_HTHREAD pthread_t
#define PA_HTHREAD_NULL		0L
#define PA_THREAD_RETTYPE	void*
typedef PA_THREAD_RETTYPE (__STDCALL PA_ThreadRoutine)(void*);
#define PA_ThreadGetCurrentHandle() pthread_self()
#define PA_ThreadCloseHandle(hThread) pthread_detach(hThread)
void* PA_ThreadWaitUntilTerminate(PA_HTHREAD hThread);

void PA_Sleep(UINT ms);	//Milliseconds

/*
 * Read-Write Lock
 */
#define INFINITE 0xFFFFFFFF

#define PA_RWLOCK	pthread_rwlock_t	
#define PA_RWLockInit(x) pthread_rwlock_init(&x, NULL)
#define PA_RWLockUninit(x) pthread_rwlock_destroy(&x)
BOOL _RWLockLockR(PA_RWLOCK *x, DWORD timeout);
BOOL _RWLockLockW(PA_RWLOCK *x, DWORD timeout);
#define PA_RWLockLockR(x) pthread_rwlock_rdlock(&x)
#define PA_RWLockLockW(x) pthread_rwlock_wrlock(&x)
#define PA_RWLockFailed(op) (op != 0)
#define PA_RWLockLockRTimed(x, timeout) _RWLockLockR(&x, timeout)
#define PA_RWLockLockWTimed(x, timeout) _RWLockLockR(&x, timeout)
#define PA_RWLockUnlock(x) pthread_rwlock_unlock(&x)

/*
 *  String functions
 */
#define PA_StrCaseCmp strcasecmp
#define PA_StrNCaseCmp strncasecmp
#define PA_StrNCmp	strncmp

/*
 *  File Operations
 */
#define PA_HFILE	int
#define PA_Write	write
#define PA_Read		read
#define PA_FileIsValid(h) (h>=0)
#define PA_FileClose(f) close(f)
BOOL PA_DeleteFile(const char *fn);

/*
 *  Time
 */
DWORD PA_GetTickCount();

#ifdef __cplusplus
}
#endif
/* End of __LINUX__ */

#elif defined(ARM_UCOS_LWIP)

#include <lwip/opt.h>
#include <lwip/init.h>

#include <lwip/mem.h>
#include <lwip/memp.h>
#include <lwip/sys.h>
#include <lwip/stats.h>
#include <lwip/netdb.h>
#include <lwip/tcpip.h>
#include <lwip/sockets.h>


/*
 * Synchronous Objects: Mutex
 */
#define PA_EVENT sys_sem_t
#define PA_SEM	sys_sem_t

#define PA_EventInit(x) sys_sem_new(&(x), 0)
#define PA_EventUninit(x) sys_sem_free(&(x))
#define PA_EventSet(x) sys_sem_signal(&(x))
//#define PA_ResetEvent(x) ResetEvent(x)
#define PA_EventWait(x) sys_arch_sem_wait(&(x), 0)
#define PA_EventWaitTimed(e, ms) (sys_arch_sem_wait(&(e), ms)!=SYS_ARCH_TIMEOUT)

#define PA_MUTEX sys_sem_t
#define PA_MutexInit(x) sys_sem_new(&(x), 1)
#define PA_MutexUninit(x) sys_sem_free(&(x))
#define PA_MutexLock(x) sys_arch_sem_wait(&(x), 0)
#define PA_MutexUnlock(x) sys_sem_signal(&(x))

#define PA_PIPE	void*

/*
 * Thread
 */
#define PA_HTHREAD sys_thread_t
#define PA_HTHREAD_NULL	NULL
#define PA_THREAD_RETTYPE void*	
typedef PA_THREAD_RETTYPE (PA_ThreadRoutine)(void*);
void* PA_ThreadWaitUntilTerminate(PA_HTHREAD hThread);
#define PA_ThreadCloseHandle(hThread)

/*
 * SOCKET Functions
 */
#define PA_NetLibInit() lwip_socket_init()
#define PA_NetLibUninit()

#define PA_SocketClose lwip_close
#define CloseSocket	iwip_close
#define PA_Send lwip_send
#define PA_SendTo lwip_sendto
#define PA_Recv lwip_recv
#define PA_RecvFrom(s, buf, size, flags, paddr, paddr_len) lwip_recvfrom(s, buf, size, flags, paddr, (socklen_t*)paddr_len)
#define PA_GetSockName(s, paddr, paddr_len) lwip_getsockname(s, paddr, (socklen_t*)paddr_len)
#define PA_GetPeerName(s, paddr, paddr_len) lwip_getpeername(s, paddr, (socklen_t*)paddr_len)
#define PA_Accept(s, paddr, paddr_len) lwip_accept(s, paddr, (socklen_t*)paddr_len)
#define PA_GetSockOpt(s, level, optname, optval, optlen) lwip_getsockopt(s, level, optname, optval, (socklen_t*)optlen)
#define PA_SetSockOpt lwip_setsockopt

#define PA_SOCKET	int
#define INVALID_SOCKET	-1	
#define PA_SocketIsValid(s) (s>=0)
#define PA_SocketGetError(s) get_socket(s)->err
#define PA_SOCKET_ERROR	-1	//return value of socket operations

struct LwipIoVec {
	void *iov_base;
	int  iov_len;
};
#define PA_IOVEC	struct LwipIoVec
#define PA_IoVecGetPtr(pvec) ((pvec)->iov_base)
#define PA_IoVecGetLen(pvec) ((pvec)->iov_len)
#define PA_IoVecSetPtr(pvec, ptr) (pvec)->iov_base = (void*)(ptr)
#define PA_IoVecSetLen(pvec, len) (pvec)->iov_len = len

/*
 * Time
 */
#define PA_GetTickCount() sys_now()
#define PA_Sleep(x) usleep(x/1000)

#else

#error "Platform must be specified !"

#endif	


#ifdef __cplusplus
extern "C" {
#endif
/*
 *  Common Wrapper
 */
PA_HTHREAD PA_ThreadCreate(PA_ThreadRoutine* routine, void* data);
int PA_SocketSetNBlk(PA_SOCKET s, BOOL b);
int PA_SocketSetLinger(PA_SOCKET s, int onoff, int linger);

/*
 *  Pipe functions
 */
BOOL PA_PipeCreate(PA_PIPE *pHPipeRd, PA_PIPE *pHPipeWrt);
BOOL PA_PipeClose(PA_PIPE hPipe);

/*
 *  Debug
 */
#ifdef _DEBUG
	#if defined(WIN32) && defined(__cplusplus) && defined(_USE_MFC) && !defined(_CONSOLE)
		#define dbg_msg TRACE
		#define PRINTF TRACE
	#else
		#define dbg_msg printf
		#define PRINTF printf
	#endif
void dbg_bin(const char *title, const void *p, int size);
#else
	#ifdef WIN32
		#define dbg_msg(fmt, __VA_ARGS__)
	#else
		#define dbg_msg(fmt, args...)
	#endif
#define dbg_bin(x,y,z)
#endif

#ifdef __ANDROID__
#include <android/log.h>
void android_log(int level, const char *tag, const char *sfmt, ...);
#define LOG(sfmt, args...) android_log(ANDROID_LOG_INFO, __FUNCTION__, sfmt, ##args)
#define LOGW(sfmt, args...) android_log(ANDROID_LOG_WARN, __FUNCTION__, sfmt, ##args)
#define LOGE(sfmt, args...) android_log(ANDROID_LOG_ERROR, __FUNCTION__, sfmt, ##args)

#undef dbg_msg
#define dbg_msg LOG

#elif defined(__LINUX__)

#define LOG(sfmt, args...) do { dbg_msg(sfmt, ##args); dbg_msg("\n"); } while(0)
#define LOGW(sfmt, args...) do { dbg_msg(sfmt, ##args); dbg_msg("\n"); } while(0)
#define LOGE(sfmt, args...) do { dbg_msg(sfmt, ##args); dbg_msg("\n"); } while(0)

#elif defined(WIN32)

#define LOG(sfmt, ...) do { dbg_msg(sfmt, __VA_ARGS__); dbg_msg("\n"); } while(0)
#define LOGW(sfmt, ...) do { dbg_msg(sfmt, __VA_ARGS__); dbg_msg("\n"); } while(0)
#define LOGE(sfmt, ...) do { dbg_msg(sfmt, __VA_ARGS__); dbg_msg("\n"); } while(0)

#endif

#ifdef __cplusplus
}
#endif

#endif	//#ifndef __dcs_platform_h__
