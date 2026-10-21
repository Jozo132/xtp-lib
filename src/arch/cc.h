#pragma once

#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>

#define BYTE_ORDER LITTLE_ENDIAN
#define LWIP_TIMEVAL_PRIVATE 1
#define LWIP_ERRNO_INCLUDE <errno.h>
#define LWIP_NOASSERT_ON_ERROR 1
#define LWIP_PLATFORM_DIAG(message) do { (void)sizeof(message); } while (0)
#define LWIP_PLATFORM_ASSERT(message) do { (void)sizeof(message); } while (0)
extern uint32_t xtp_lwip_rand(void);
#define LWIP_RAND() ((u32_t)xtp_lwip_rand())

typedef uint32_t sys_prot_t;

typedef uint8_t u8_t;
typedef int8_t s8_t;
typedef uint16_t u16_t;
typedef int16_t s16_t;
typedef uint32_t u32_t;
typedef int32_t s32_t;
typedef uint64_t u64_t;
typedef int64_t s64_t;
typedef uintptr_t mem_ptr_t;

#define PACK_STRUCT_FIELD(x) x
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END
#define LWIP_DECLARE_MEMORY_ALIGNED(variable_name, size) \
  uint8_t variable_name[((size) + 31U) & ~31U] __attribute__((aligned(32)))
