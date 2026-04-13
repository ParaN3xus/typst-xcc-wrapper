#pragma once

#ifndef __ORDER_LITTLE_ENDIAN__
#define __ORDER_LITTLE_ENDIAN__ 1234
#endif

#ifndef __ORDER_BIG_ENDIAN__
#define __ORDER_BIG_ENDIAN__ 4321
#endif

#ifndef __BYTE_ORDER__
#define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__
#endif

#ifndef UINT64_C
#define UINT64_C(value) value##ULL
#endif

#ifndef UINT32_C
#define UINT32_C(value) value##U
#endif
