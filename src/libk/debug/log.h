/**
 * 
 * @file : /src/libk/debug/log.h
 * @brief : Kernel logging, ZenOS's Heart, Liver, whatever.
 * 
 * This file is a part of the Zen (ZenOS)
 * Operating System, and is released under
 * the terms of the MIT Licensing : Read
 * LICENSE at the root of the repository.
 * 
 * @copyright (c) 2026
 * @author : Rishies2010
 * 
 */

#ifndef LOG_H
#define LOG_H

#include "stdbool.h"
#include "../spinlock.h"

#define env 0
extern spinlock_t loglock;
extern char* os_version;

void log_internal(const char* file, int line, const char* fmt, int level, int visibility, ...);
void shutdown(void);

#define log(fmt, level, visibility, ...) \
    log_internal(__FILE__, __LINE__, fmt, level, visibility, ##__VA_ARGS__)

#endif
