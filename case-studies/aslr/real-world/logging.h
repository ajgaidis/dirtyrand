#ifndef __LOGGING_H__
#define __LOGGING_H__

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>


/* attributes */
#define RESET         "0"
#define BRIGHT        "1"
#define DIM           "2"
#define UNDERLINE     "3"
/* for more see: https://tinyurl.com/mvnfnzs2 */

/* foreground colors */
#define FG_BLACK      "30"
#define FG_RED        "31"
#define FG_GREEN      "32"
#define FG_YELLOW     "33"
#define FG_BLUE       "34"
#define FG_MAGENTA    "35"
#define FG_CYAN       "36"
#define FG_WHITE      "37"

/* background colors */
#define BG_BLACK      "40"
#define BG_RED        "41"
#define BG_GREEN      "42"
#define BG_YELLOW     "43"
#define BG_BLUE       "44"
#define BG_MAGENTA    "45"
#define BG_CYAN       "46"
#define BG_WHITE      "47"

/* color code format: <ESC>[{attr};{fg};{bg}m */
#define CODE1(x)        "\x1B[" x "m"
#define CODE2(x, y)     "\x1B[" x ";" y "m"
#define CODE3(x, y, z)  "\x1B[" x ";" y ";" z "m"

#define FMT0(fmt)                           fmt
#define FMT1(fmt, x)        CODE1(x)        fmt CODE1(RESET)
#define FMT2(fmt, x, y)     CODE2(x, y)     fmt CODE1(RESET)
#define FMT3(fmt, x, y, z)  CODE3(x, y, z)  fmt CODE1(RESET)

#define GET_COLOR_FMT(_0, _1, _2, _3, NAME, ...) NAME
#define COLOR_FMT(fmt, ...) \
  GET_COLOR_FMT(_0, ##__VA_ARGS__, FMT3, FMT2, FMT1, FMT0)(fmt, __VA_ARGS__)


#define esym   COLOR_FMT("[E] ", BRIGHT, FG_RED)
#define wsym   COLOR_FMT("[W] ", BRIGHT, FG_YELLOW)
#define lsym   COLOR_FMT("[+] ", BRIGHT, FG_GREEN)
#define l2sym  COLOR_FMT("[.] ", BRIGHT, FG_GREEN)
#define slsym  COLOR_FMT("... ", BRIGHT, FG_GREEN)
#define dsym   COLOR_FMT("[D] ", BRIGHT, FG_BLUE)

#define NOFMT_PRINT(s)        printf(s)
#define NOFMT_PRINT_GREEN(s)  printf(COLOR_FMT(s, BRIGHT, FG_GREEN))
#define NOFMT_PRINT_BLUE(s)   printf(COLOR_FMT(s, BRIGHT, FG_BLUE))

#define handle_error_en(en, msg) do { \
  errno = (en); \
  perror(esym COLOR_FMT(msg, FG_RED)); \
  exit(EXIT_FAILURE); \
} while (0)

#define handle_error(msg) do { \
  perror(esym COLOR_FMT(msg, FG_RED)); \
  exit(EXIT_FAILURE); \
} while (0)

#define handle_error_no_en(fmt, ...) do { \
  fprintf(stderr, esym COLOR_FMT(fmt, FG_RED), ##__VA_ARGS__); \
  exit(EXIT_FAILURE); \
} while (0)

#define warn(fmt, ...) \
  fprintf(stderr, wsym COLOR_FMT(fmt, FG_YELLOW), ##__VA_ARGS__)

#define log(fmt, ...) \
  fprintf(stdout, lsym COLOR_FMT(fmt, FG_GREEN), ##__VA_ARGS__)
#define log2(fmt, ...) \
  fprintf(stdout, l2sym COLOR_FMT(fmt, FG_GREEN), ##__VA_ARGS__)
#define slog(fmt, ...) \
  fprintf(stdout, slsym COLOR_FMT(fmt, FG_GREEN), ##__VA_ARGS__)

#define hexdump(bytes, nbytes) do { \
  for (int __i = 0; __i < nbytes; __i++) { \
    if (__i % 16 == 0) { \
      if (__i != 0) { \
        fprintf(stdout, "\n"); \
      } \
      fprintf(stdout, "\t"); \
    } else { \
      if (__i % 4 == 0) { \
        fprintf(stdout, "   "); \
      } else { \
        fprintf(stdout, " "); \
      } \
    } \
    fprintf(stdout, COLOR_FMT("%02x", FG_GREEN), ((uint8_t *)bytes)[__i]); \
  } \
  fprintf(stdout, "\n"); \
} while (0)

#ifdef DEBUG
  #define dbg(fmt, ...) \
    fprintf(stdout, dsym COLOR_FMT(fmt, FG_BLUE), ##__VA_ARGS__)
#else
  #define dbg(fmt, ...)
#endif

#endif /* __LOGGING_H__ */
