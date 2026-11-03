#include <stdint.h>
#include <stddef.h>
#include <string.h>
static inline int sys_csrand_get(void *output,size_t length)
{
    uint64_t identity=0x1234;
    if(length!=sizeof(identity))return -22;
    memcpy(output,&identity,length);return 0;
}
