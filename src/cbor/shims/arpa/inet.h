#pragma once

#include <stdint.h>

static inline uint16_t ntohs(uint16_t value) {
  return (uint16_t)((value >> 8) | (value << 8));
}

static inline uint16_t htons(uint16_t value) {
  return ntohs(value);
}

static inline uint32_t ntohl(uint32_t value) {
  return
      ((value & 0x000000ffu) << 24) |
      ((value & 0x0000ff00u) << 8) |
      ((value & 0x00ff0000u) >> 8) |
      ((value & 0xff000000u) >> 24);
}

static inline uint32_t htonl(uint32_t value) {
  return ntohl(value);
}
