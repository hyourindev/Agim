#include <stdio.h>
#include "lang/agim.h"

int main(void) {
    printf("Starting minimal test...\n");
    fflush(stdout);

    printf("About to compile...\n");
    fflush(stdout);

    const char *error = NULL;
    Bytecode *code = agim_compile("let x = 1", &error);

    printf("Compiled: %p\n", (void*)code);
    fflush(stdout);

    if (code) bytecode_free(code);
    if (error) agim_error_free(error);

    printf("Done\n");
    return 0;
}
