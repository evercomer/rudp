#ifndef __basetype_h__
#define __basetype_h__

#define IN
#define OUT
#define INOUT

#ifdef WIN32

typedef char                    int8_t;
typedef unsigned char           uint8_t;
typedef short                   int16_t;
typedef unsigned short          uint16_t;
typedef int                     int32_t;
typedef unsigned int            uint32_t;

typedef __int64                 int64_t;
typedef unsigned __int64        uint64_t;

#define __BEGIN_ALIGNMENT(x)__ __pragma(pack(push, x))
#define __END_ALIGNMENT(x)__ __pragma(pack(pop))

#define __BEGIN_PACKED__ __pragma(pack(push, 1))
#define __END_PACKED__ __pragma(pack(pop))
#define __PACKED__

#define __STDCALL  __stdcall
#define INLINE	__inline
#define __LIKELY(x)	x
#define __UNLIKELY(x)	x


#elif defined(__LINUX__) || defined(__ANDROID__)

typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;

typedef unsigned char UCHAR;
typedef unsigned short USHORT;
typedef unsigned int UINT;
typedef unsigned long ULONG;

#include <stdint.h>

typedef long LONG;

typedef const char * LPCSTR;
typedef char * LPSTR;

#define BOOL	int
#define TRUE	1
#define FALSE	0

#define __BEGIN_ALIGNMENT__(x)
#define __END_ALIGNMENT__(x) 

#define __BEGIN_PACKED__ 
#define __END_PACKED__ 
#define __PACKED__ __attribute__((__packed__))

#define __STDCALL
#define INLINE inline
#define __LIKELY(x) __builtin_expect(!!(x), 1)
#define __UNLIKELY(x) __builtin_expect(!!(x), 0)

#elif defined(ARM_UCOS_LWIP)

typedef char                    int8_t;
typedef unsigned char           uint8_t;
typedef short                   int16_t;
typedef unsigned short          uint16_t;
typedef int                     int32_t;
typedef unsigned int            uint32_t;

typedef __int64                 int64_t;
typedef unsigned __int64        uint64_t;

typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;

typedef unsigned char UCHAR;
typedef unsigned short USHORT;
typedef unsigned int UINT;
typedef unsigned long ULONG;


#define __BEGIN_ALIGNMENT(x)__ 
#define __END_ALIGNMENT(x)__ 

#define __BEGIN_PACKED__ 
#define __END_PACKED__ 
#define __PACKED__ __attribute__((__packed__))

#define __STDCALL
#define INLINE	__inline
#define __LIKELY(x)	x
#define __UNLIKELY(x)	x

#define BOOL	int
#define TRUE	1
#define FALSE	0


#endif

#endif
