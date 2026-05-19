#ifndef _ENDIAN_H
#define _ENDIAN_H

#include <byteswap.h>
#include <stdint.h>

#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN 4321
#define __PDP_ENDIAN 3412

#define LITTLE_ENDIAN __LITTLE_ENDIAN
#define BIG_ENDIAN __BIG_ENDIAN
#define PDP_ENDIAN __PDP_ENDIAN

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define __BYTE_ORDER __LITTLE_ENDIAN
#define BYTE_ORDER LITTLE_ENDIAN
#define htobe16(x) bswap_16(x)
#define htole16(x) ((uint16_t)(x))
#define be16toh(x) bswap_16(x)
#define le16toh(x) ((uint16_t)(x))
#define htobe32(x) bswap_32(x)
#define htole32(x) ((uint32_t)(x))
#define be32toh(x) bswap_32(x)
#define le32toh(x) ((uint32_t)(x))
#define htobe64(x) bswap_64(x)
#define htole64(x) ((uint64_t)(x))
#define be64toh(x) bswap_64(x)
#define le64toh(x) ((uint64_t)(x))
#else
#define __BYTE_ORDER __BIG_ENDIAN
#define BYTE_ORDER BIG_ENDIAN
#define htobe16(x) ((uint16_t)(x))
#define htole16(x) bswap_16(x)
#define be16toh(x) ((uint16_t)(x))
#define le16toh(x) bswap_16(x)
#define htobe32(x) ((uint32_t)(x))
#define htole32(x) bswap_32(x)
#define be32toh(x) ((uint32_t)(x))
#define le32toh(x) bswap_32(x)
#define htobe64(x) ((uint64_t)(x))
#define htole64(x) bswap_64(x)
#define be64toh(x) ((uint64_t)(x))
#define le64toh(x) bswap_64(x)
#endif

#endif
