#include "diagnostics.h"

#include <stdarg.h>

void kasm_error(Assembler *as, SourceLoc loc, const char *fmt, ...)
{
    if (loc.column <= 0)
        loc.column = 1;
    fprintf(stderr, "%s:%d:%d: error: ", loc.path ? loc.path : "<input>", loc.line, loc.column);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n --> %s:%d:%d\n", loc.path ? loc.path : "<input>", loc.line, loc.column);
    if (as && loc.line > 0 && (size_t)loc.line <= as->source_line_count) {
        const char *line = as->source_lines[loc.line - 1];
        fprintf(stderr, "  |\n");
        fprintf(stderr, "%d | %s\n", loc.line, line);
        fprintf(stderr, "  | ");
        for (int i = 1; i < loc.column; i++)
            fputc(line[i - 1] == '\t' ? '\t' : ' ', stderr);
        fprintf(stderr, "^\n");
    }
    as->errors++;
}
