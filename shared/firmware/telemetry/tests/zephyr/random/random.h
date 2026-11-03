#include "mock.h"
static inline int sys_csrand_get(void *output, size_t length)
{
    assert(length == sizeof(uint64_t));
    uint64_t identity = UINT64_C(0x123456789abc);
    memcpy(output, &identity, length);
    return 0;
}
