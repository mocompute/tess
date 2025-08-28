#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "readline/history.h"
#include "readline/readline.h"

void eval_print(char *in) {
    printf("you said: '%s'\n", in);
}

int main(void) {
    puts("Hello, world!");

    while (true) {
        char *line = readline("tess > ");

        if (!line) continue;

        eval_print(line);
        add_history(line);

        free(line);
    }
}
