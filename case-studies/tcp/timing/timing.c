#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define handle_error(msg) \
  do { perror(msg); exit(EXIT_FAILURE); } while (0)

static inline unsigned long long get_real_ns(void)
{
  struct timespec ts;
  unsigned long long ns;

  /* Get a timestamp */
  if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
    handle_error("clock_gettime(CLOCK_REALTIME)");

  /* Convert it to nanseconds */
  ns = ts.tv_sec * 1000000000LL + ts.tv_nsec;

  /* Return it! */
  return ns;
}

int main(void)
{
  printf("");
  printf("%llu", get_real_ns() >> 6);
}
