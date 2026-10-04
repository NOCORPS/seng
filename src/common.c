
#include "common.h"

char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f && (path[0] == '/' || path[0] == '\\')) {
        f = fopen(path + 1, "rb");
    }
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = (char *)xmalloc((size_t)sz + 1);
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

void fatal(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "\033[1;31mseng error:\033[0m ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}
