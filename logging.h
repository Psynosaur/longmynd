/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: logging.h                                                                   */
/* -------------------------------------------------------------------------------------------------- */
/*
    Quiet-mode logging helpers.

    lm_log(fmt, ...)  — prints to stdout; suppressed when quiet mode (-q) is active.
    ERROR/WARNING prints are never suppressed — use plain printf() for those.
*/

#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>
#include <stdbool.h>

extern bool lm_quiet;

#define lm_log(fmt, ...) do { if (!lm_quiet) printf(fmt, ##__VA_ARGS__); } while(0)

#endif /* LOGGING_H */
