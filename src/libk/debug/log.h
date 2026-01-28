#ifndef LOG_H
#define LOG_H

#include "stdbool.h"
#include "../spinlock.h"

#define debug 0
extern spinlock_t loglock;
extern char* os_version;

void log_internal(const char* file, int line, const char* fmt, int level, int visibility, ...);
void shutdown(void);

#define log(fmt, level, visibility, ...) \
    log_internal(__FILE__, __LINE__, fmt, level, visibility, ##__VA_ARGS__)

#endif
