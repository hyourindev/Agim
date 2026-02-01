/*
 * Agim - Logging Infrastructure
 *
 * Thread-safe, configurable logging for production monitoring.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_LOG_H
#define AGIM_LOG_H

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>

/* Log Levels */

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3,
    LOG_LEVEL_FATAL = 4,
    LOG_LEVEL_OFF = 5
} LogLevel;

/* Log Configuration */

typedef struct LogConfig {
    LogLevel min_level;      /* Minimum level to log */
    FILE *output;            /* Output stream (default: stderr) */
    bool show_timestamp;     /* Include timestamp in output */
    bool show_level;         /* Include level name in output */
    bool show_location;      /* Include file:line in output */
    bool use_colors;         /* Use ANSI colors (if terminal) */
} LogConfig;

/* Initialize with defaults */
LogConfig log_config_default(void);

/* Global Configuration */

void log_init(const LogConfig *config);
void log_shutdown(void);
void log_set_level(LogLevel level);
LogLevel log_get_level(void);
void log_set_output(FILE *output);

/* Logging Functions */

void log_write(LogLevel level, const char *file, int line,
               const char *fmt, ...) __attribute__((format(printf, 4, 5)));

void log_write_v(LogLevel level, const char *file, int line,
                 const char *fmt, va_list args);

/* Convenience Macros */

#define LOG_DEBUG(fmt, ...) \
    log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) \
    log_write(LOG_LEVEL_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    log_write(LOG_LEVEL_WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
    log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_FATAL(fmt, ...) \
    log_write(LOG_LEVEL_FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

/* Level name lookup */
const char *log_level_name(LogLevel level);

/* Check if level would be logged */
bool log_enabled(LogLevel level);

#endif /* AGIM_LOG_H */
