#ifndef __JIFFIES_H__
#define __JIFFIES_H__

/*
 * Define USE_SYSINFO to use sysinfo(2) to get uptime. Else, undefine
 * it to use clock_gettime(2)
 */
#undef USE_SYSINFO

/*
 * Ensure HZ value matches kernel's with:
 *
 * zcat /proc/config.gz | grep CONFIG_HZ=
 *
 * or
 *
 * grep CONFIG_HZ= /boot/config-$(uname -r)
 */
#define HZ	1000

/*
 * This is the initial jiffies value that is set to rollover after 5 minutes
 * (300 seconds) on 32-bit machines to catch bugs more quickly.
 */
#define INITIAL_JIFFIES ((unsigned long)(unsigned int) (-300*HZ))


/* Get an estimate of the current the jiffy value */
unsigned long long estimate_jiffies(void);

#endif /* __JIFFIES_H__ */
