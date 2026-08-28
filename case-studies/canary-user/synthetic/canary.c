#include "logging.h"
#include "libprimitive.h"
#include <stdlib.h>

int main(void)
{
    log("Zapping L1 key...\n");
    trigger_overwrite_of_l1_key();

    system("./read_canary");

    /* Success! */
    return 0;
}
