/*
 * Agim CLI - Run Agim programs
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "lang/agim.h"
#include "vm/vm.h"
#include "vm/value.h"
#include "vm/primitives.h"
#include "runtime/scheduler.h"
#include "runtime/block.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "agim: could not open file '%s'\n", path);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = malloc(size + 1);
    if (!buffer) {
        fprintf(stderr, "agim: out of memory\n");
        fclose(file);
        return NULL;
    }

    size_t read = fread(buffer, 1, size, file);
    buffer[read] = '\0';
    fclose(file);

    return buffer;
}

static void print_usage(const char *program) {
    fprintf(stderr, "Agim - A language for building isolated AI agents\n\n");
    fprintf(stderr, "Usage: %s [options] <file.im>\n\n", program);
    fprintf(stderr, "File extensions:\n");
    fprintf(stderr, "  .ag            Agent workflow (declarative) [planned]\n");
    fprintf(stderr, "  .im            Implementation (imperative)\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -h, --help     Show this help message\n");
    fprintf(stderr, "  -v, --version  Show version information\n");
    fprintf(stderr, "  -d, --disasm   Disassemble bytecode instead of running\n");
    fprintf(stderr, "  -t, --tools    List registered tools\n");
}

static void print_version(void) {
    printf("agim 0.1.0\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *filename = NULL;
    bool disassemble = false;
    bool list_tools = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--disasm") == 0) {
            disassemble = true;
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--tools") == 0) {
            list_tools = true;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "agim: unknown option '%s'\n", argv[i]);
            return 1;
        } else {
            filename = argv[i];
        }
    }

    if (!filename) {
        fprintf(stderr, "agim: no input file\n");
        return 1;
    }

    /* Read source file */
    char *source = read_file(filename);
    if (!source) {
        return 1;
    }

    /* Compile */
    const char *error = NULL;
    Bytecode *code = agim_compile(source, &error);
    free(source);

    if (!code) {
        fprintf(stderr, "agim: compile error: %s\n", error ? error : "unknown error");
        if (error) agim_error_free(error);
        return 1;
    }

    /* List tools mode */
    if (list_tools) {
        size_t tool_count;
        const ToolInfo *tools = bytecode_get_tools(code, &tool_count);

        if (tool_count == 0) {
            printf("No tools defined.\n");
        } else {
            printf("Tools (%zu):\n", tool_count);
            for (size_t i = 0; i < tool_count; i++) {
                printf("\n  tool %s(", tools[i].name);
                for (size_t j = 0; j < tools[i].param_count; j++) {
                    if (j > 0) printf(", ");
                    printf("%s", tools[i].params[j].name ? tools[i].params[j].name : "?");
                    if (tools[i].params[j].type) {
                        printf(": %s", tools[i].params[j].type);
                    }
                }
                printf(")");
                if (tools[i].return_type) {
                    printf(" -> %s", tools[i].return_type);
                }
                if (tools[i].description) {
                    printf("\n    \"%s\"", tools[i].description);
                }
                /* Show parameter descriptions if present */
                for (size_t j = 0; j < tools[i].param_count; j++) {
                    if (tools[i].params[j].description) {
                        printf("\n    @param %s: %s",
                               tools[i].params[j].name ? tools[i].params[j].name : "?",
                               tools[i].params[j].description);
                    }
                }
                printf("\n");
            }
        }
        bytecode_free(code);
        return 0;
    }

    /* Disassemble mode */
    if (disassemble) {
        chunk_disassemble(code->main, "main");
        for (size_t i = 0; i < code->functions_count; i++) {
            char name[32];
            snprintf(name, sizeof(name), "fn_%zu", i);
            chunk_disassemble(code->functions[i], name);
        }
        bytecode_free(code);
        return 0;
    }

    /* Create scheduler (single-threaded by default) */
    SchedulerConfig config = scheduler_config_default();
    Scheduler *scheduler = scheduler_new(&config);
    if (!scheduler) {
        fprintf(stderr, "agim: failed to create scheduler\n");
        bytecode_free(code);
        return 1;
    }

    /* Initialize primitives runtime for tool support */
    PrimitivesRuntime *primitives = primitives_new();
    if (primitives) {
        primitives_register_builtins(primitives);
        scheduler_set_primitives(scheduler, primitives);
    }

    /* Spawn main program as a block */
    Pid main_pid = scheduler_spawn(scheduler, code, "main");
    if (main_pid == PID_INVALID) {
        fprintf(stderr, "agim: failed to spawn main block\n");
        scheduler_free(scheduler);
        return 1;
    }

    /* Run until all blocks complete */
    scheduler_run(scheduler);

    /* Check result */
    int exit_code = 0;
    Block *main_block = scheduler_get_block(scheduler, main_pid);
    if (main_block && main_block->vm) {
        if (main_block->vm->error) {
            fprintf(stderr, "agim: runtime error: %s\n", main_block->vm->error);
            exit_code = 1;
        } else {
            Value *result = vm_peek(main_block->vm, 0);
            if (result && !value_is_nil(result)) {
                value_print(result);
                printf("\n");
            }
        }
    }

    if (primitives) {
        primitives_free(primitives);
    }
    scheduler_free(scheduler);
    return exit_code;
}
