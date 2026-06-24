#include "lexer.h"

char *kasm_strip_comment(char *line)
{
    int in_string = 0;
    int escaped = 0;
    for (char *p = line; *p; p++) {
        if (escaped) {
            escaped = 0;
            continue;
        }
        if (*p == '\\' && in_string) {
            escaped = 1;
            continue;
        }
        if (*p == '"')
            in_string = !in_string;
        if (*p == ';' && !in_string) {
            *p = 0;
            break;
        }
    }
    return line;
}
