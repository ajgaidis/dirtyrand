#include "jiffies.h"
#include <stdio.h>
#ifdef USE_SYSINFO
#include <sys/sysinfo.h>
#else
#include <time.h>
#endif /* USE_SYSINFO */

unsigned long long estimate_jiffies(void)
{
#ifdef USE_SYSINFO
    struct sysinfo info;
#else
    struct timespec ts;
#endif /* USE_SYSINFO */
    time_t uptime;
    unsigned long long jiffies;


#ifdef USE_SYSINFO
    sysinfo(&info);
    uptime = info.uptime;
#else
    /*
     * Get the system uptime. CLOCK_BOOTTIME is generally the most appropriate
     * clock for "system uptime" as it accounts for time spent in system
     * suspend. If CLOCK_BOOTTIME is not available (e.g., older kernels or
     * specific architectures), CLOCK_MONOTONIC is a good fallback, though it
     * does not account for suspend time.
     */
    clock_gettime(CLOCK_BOOTTIME, &ts);
    uptime = ts.tv_sec;
#endif /* USE_SYSINFO */

    /* 
     * Calculate jiffies (factoring in the INITIAL_JIFFIES count that is
     * present in the kernel to test for jiffy-wrap bugs in 32-bit systems). 
     */
    jiffies = uptime * HZ + INITIAL_JIFFIES;

    /* Verbose */
    printf("[+] Estimated jiffies: %llu\n", jiffies);

    /* All done! */
    return jiffies;
}

