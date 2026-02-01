/*
 * Agim - Logging Infrastructure
 *
 * Thread-safe, configurable logging for production monitoring.
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "debug/log.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Global State */

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static _Atomic(LogLevel) global_level = LOG_LEVEL_INFO;
static FILE *log_output = NULL;
static LogConfig log_config;
static bool log_initialized = false;

/* ANSI Color Codes */

static const char *level_colors[] = {
    "\033[36m",  /* DEBUG: cyan */
    "\033[32m",  /* INFO: green */
    "\033[33m",  /* WARN: yellow */
    "\033[31m",  /* ERROR: red */
    "\033[35m",  /* FATAL: magenta */
};
static const char *color_reset = "\033[0m";

/* Level Names */

static const char *level_names[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL",
    "OFF"
};

/* Default Configuration */

LogConfig log_config_default(void) {
    return (LogConfig){
        .min_level = LOG_LEVEL_INFO,
        .output = NULL,  /* Will use stderr */
        .show_timestamp = true,
        .show_level = true,
        .show_location = false,
        .use_colors = true,
    };
}

/* Initialization */

void log_init(const LogConfig *config) {
    pthread_mutex_lock(&log_mutex);

    if (config) {
        log_config = *config;
    } else {
        log_config = log_config_default();
    }

    atomic_store(&global_level, log_config.min_level);
    log_output = log_config.output ? log_config.output : stderr;

    /* Detect if output is a terminal for color support */
    if (log_config.use_colors && log_output) {
        int fd = fileno(log_output);
        if (fd >= 0 && !isatty(fd)) {
            log_config.use_colors = false;
        }
    }

    log_initialized = true;

    pthread_mutex_unlock(&log_mutex);
}

void log_shutdown(void) {
    pthread_mutex_lock(&log_mutex);

    /* Don't close stderr/stdout */
    if (log_output && log_output != stderr && log_output != stdout) {
        fclose(log_output);
    }
    log_output = NULL;
    log_initialized = false;

    pthread_mutex_unlock(&log_mutex);
}

void log_set_level(LogLevel level) {
    atomic_store(&global_level, level);
}

LogLevel log_get_level(void) {
    return atomic_load(&global_level);
}

void log_set_output(FILE *output) {
    pthread_mutex_lock(&log_mutex);
    log_output = output ? output : stderr;
    pthread_mutex_unlock(&log_mutex);
}

/* Level Utilities */

const char *log_level_name(LogLevel level) {
    if (level >= LOG_LEVEL_DEBUG && level <= LOG_LEVEL_OFF) {
        return level_names[level];
    }
    return "UNKNOWN";
}

bool log_enabled(LogLevel level) {
    return level >= atomic_load(&global_level);
}

/* Core Logging */

void log_write_v(LogLevel level, const char *file, int line,
                 const char *fmt, va_list args) {
    if (!log_enabled(level)) return;

    pthread_mutex_lock(&log_mutex);

    FILE *out = log_output ? log_output : stderr;

    /* Timestamp */
    if (log_config.show_timestamp) {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        char timebuf[32];
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm);
        fprintf(out, "%s ", timebuf);
    }

    /* Level */
    if (log_config.show_level) {
        if (log_config.use_colors && level < LOG_LEVEL_OFF) {
            fprintf(out, "%s%-5s%s ", level_colors[level],
                    level_names[level], color_reset);
        } else {
            fprintf(out, "%-5s ", level_names[level]);
        }
    }

    /* Location */
    if (log_config.show_location && file) {
        /* Extract just the filename from path */
        const char *basename = strrchr(file, '/');
        basename = basename ? basename + 1 : file;
        fprintf(out, "[%s:%d] ", basename, line);
    }

    /* Message */
    vfprintf(out, fmt, args);
    fprintf(out, "\n");

    /* Flush for ERROR and FATAL */
    if (level >= LOG_LEVEL_ERROR) {
        fflush(out);
    }

    pthread_mutex_unlock(&log_mutex);
}

void log_write(LogLevel level, const char *file, int line,
               const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_write_v(level, file, line, fmt, args);
    va_end(args);
}
