#ifndef UTILS_H
#define UTILS_H

#include <time.h>

long utils_parse_long(const char *s, long min_value, long max_value, int *ok);
time_t utils_now(void);
char utils_owner_symbol(const char *nickname);

#endif
