#include <stdint.h>

extern "C" {
[[gnu::visibility("hidden")]] void* __dso_handle = &__dso_handle;
}

extern "C" int __popcountdi2(uint64_t x) {
  x = x - ((x >> 1) & 0x5555555555555555ULL);
  x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
  x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
  return int((x * 0x0101010101010101ULL) >> 56);
}
