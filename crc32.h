#ifndef __crc32_h__
#define __crc32_h__

#include "basetype.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t calc_crc32(uint32_t crc, char *buf, int len);

#ifdef __cplusplus
}
#endif

#endif

