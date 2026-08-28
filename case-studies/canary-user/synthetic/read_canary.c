#include "logging.h"


int main(void)
{
    unsigned long __stack_chk_guard;
    asm volatile ("mov %%fs:0x28, %0 \n\t" : "=r" (__stack_chk_guard) ::);
    log("__stack_chk_guard = 0x%lx\n", __stack_chk_guard);
}
