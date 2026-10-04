
#define _POSIX_C_SOURCE 200809L
#include "packages.h"
#include "value.h"
#include "env.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#  include <windows.h>
#  include <wininet.h>
#  include <direct.h>
#else
#  include <dirent.h>
#  include <unistd.h>
#endif

/* ── helpers ─────────────────────────────────────────────────── */

static Value *val_strn(const char *s, size_t n) {
    char *t = xstrndup(s, n);
    Value *v = val_str(t);
    free(t);
    return v;
}

static void require_num(Value *v, int pos, const char *fn) {
    if (!v || v->type != VAL_NUM)
        fatal("'%s': argument %d must be a number", fn, pos);
}
static void require_str(Value *v, int pos, const char *fn) {
    if (!v || v->type != VAL_STR)
        fatal("'%s': argument %d must be a string", fn, pos);
}
static void require_list(Value *v, int pos, const char *fn) {
    if (!v || v->type != VAL_LIST)
        fatal("'%s': argument %d must be a list", fn, pos);
}

/* ═══════════════════════════════════════════════════════════════
   math package
   ═══════════════════════════════════════════════════════════════ */

static Value *nat_sqrt(Value **a, int n) {
    (void)n; require_num(a[0], 1, "sqrt");
    if (a[0]->num < 0) fatal("'sqrt': argument must be non-negative");
    return val_num(sqrt(a[0]->num));
}
static Value *nat_floor(Value **a, int n) {
    (void)n; require_num(a[0], 1, "floor");
    return val_num(floor(a[0]->num));
}
static Value *nat_ceil(Value **a, int n) {
    (void)n; require_num(a[0], 1, "ceil");
    return val_num(ceil(a[0]->num));
}
static Value *nat_round(Value **a, int n) {
    (void)n; require_num(a[0], 1, "round");
    return val_num(round(a[0]->num));
}
static Value *nat_abs(Value **a, int n) {
    (void)n; require_num(a[0], 1, "abs");
    return val_num(fabs(a[0]->num));
}
static Value *nat_power(Value **a, int n) {
    (void)n;
    require_num(a[0], 1, "power"); require_num(a[1], 2, "power");
    return val_num(pow(a[0]->num, a[1]->num));
}
static Value *nat_log(Value **a, int n) {
    (void)n; require_num(a[0], 1, "log");
    if (a[0]->num <= 0) fatal("'log': argument must be positive");
    return val_num(log(a[0]->num));
}
static Value *nat_log10_fn(Value **a, int n) {
    (void)n; require_num(a[0], 1, "log10");
    if (a[0]->num <= 0) fatal("'log10': argument must be positive");
    return val_num(log10(a[0]->num));
}
static Value *nat_sin(Value **a, int n)  { (void)n; require_num(a[0],1,"sin");  return val_num(sin(a[0]->num));  }
static Value *nat_cos(Value **a, int n)  { (void)n; require_num(a[0],1,"cos");  return val_num(cos(a[0]->num));  }
static Value *nat_tan(Value **a, int n)  { (void)n; require_num(a[0],1,"tan");  return val_num(tan(a[0]->num));  }
static Value *nat_min(Value **a, int n) {
    (void)n; require_num(a[0],1,"min"); require_num(a[1],2,"min");
    return val_num(a[0]->num < a[1]->num ? a[0]->num : a[1]->num);
}
static Value *nat_max(Value **a, int n) {
    (void)n; require_num(a[0],1,"max"); require_num(a[1],2,"max");
    return val_num(a[0]->num > a[1]->num ? a[0]->num : a[1]->num);
}
static Value *nat_random(Value **a, int n) {
    (void)a; (void)n;
    return val_num((double)rand() / ((double)RAND_MAX + 1.0));
}
static Value *nat_random_int(Value **a, int n) {
    (void)n; require_num(a[0],1,"random_int"); require_num(a[1],2,"random_int");
    int lo = (int)a[0]->num, hi = (int)a[1]->num;
    if (lo > hi) fatal("'random_int': first argument must be <= second");
    return val_num((double)(lo + rand() % (hi - lo + 1)));
}

static SengNative math_fns[] = {
    {"sqrt",       nat_sqrt,       1},
    {"floor",      nat_floor,      1},
    {"ceil",       nat_ceil,       1},
    {"round",      nat_round,      1},
    {"abs",        nat_abs,        1},
    {"power",      nat_power,      2},
    {"log",        nat_log,        1},
    {"log10",      nat_log10_fn,   1},
    {"sin",        nat_sin,        1},
    {"cos",        nat_cos,        1},
    {"tan",        nat_tan,        1},
    {"min",        nat_min,        2},
    {"max",        nat_max,        2},
    {"random",     nat_random,     0},
    {"random_int", nat_random_int, 2},
    {NULL, NULL, 0}
};

static void pkg_math(Env *g) {
    static int seeded = 0;
    if (!seeded) { srand((unsigned int)time(NULL)); seeded = 1; }
    for (int i = 0; math_fns[i].name; i++) {
        Value *v = val_native(&math_fns[i]);
        env_set(g, math_fns[i].name, v);
        val_deref(v);
    }
    Value *pi = val_num(3.14159265358979323846);
    env_set(g, "pi", pi);
    val_deref(pi);
}

/* ═══════════════════════════════════════════════════════════════
   time package
   ═══════════════════════════════════════════════════════════════ */

static Value *nat_now(Value **a, int n) {
    (void)a; (void)n;
    return val_num((double)time(NULL));
}
static Value *nat_format_time(Value **a, int n) {
    (void)n; require_num(a[0],1,"format_time");
    time_t t = (time_t)a[0]->num;
    struct tm *info = localtime(&t);
    char buf[128];
    if (!info) return val_str("invalid time");
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", info);
    return val_str(buf);
}

static SengNative time_fns[] = {
    {"now",         nat_now,         0},
    {"format_time", nat_format_time, 1},
    {NULL, NULL, 0}
};

static void pkg_time(Env *g) {
    for (int i = 0; time_fns[i].name; i++) {
        Value *v = val_native(&time_fns[i]);
        env_set(g, time_fns[i].name, v);
        val_deref(v);
    }
}

/* ═══════════════════════════════════════════════════════════════
   string package
   ═══════════════════════════════════════════════════════════════ */

static Value *nat_str_len(Value **a, int n) {
    (void)n; require_str(a[0], 1, "str_len");
    return val_num((double)strlen(a[0]->str));
}
static Value *nat_upper(Value **a, int n) {
    (void)n; require_str(a[0], 1, "upper");
    char *s = xstrdup(a[0]->str);
    for (size_t i = 0; s[i]; i++) s[i] = (char)toupper((unsigned char)s[i]);
    Value *v = val_str(s); free(s); return v;
}
static Value *nat_lower(Value **a, int n) {
    (void)n; require_str(a[0], 1, "lower");
    char *s = xstrdup(a[0]->str);
    for (size_t i = 0; s[i]; i++) s[i] = (char)tolower((unsigned char)s[i]);
    Value *v = val_str(s); free(s); return v;
}
static Value *nat_trim(Value **a, int n) {
    (void)n; require_str(a[0], 1, "trim");
    const char *s = a[0]->str;
    while (*s && isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
    return val_strn(s, len);
}
static Value *nat_contains(Value **a, int n) {
    (void)n; require_str(a[0],1,"contains"); require_str(a[1],2,"contains");
    return val_bool(strstr(a[0]->str, a[1]->str) != NULL);
}
static Value *nat_starts_with(Value **a, int n) {
    (void)n; require_str(a[0],1,"starts_with"); require_str(a[1],2,"starts_with");
    size_t plen = strlen(a[1]->str);
    return val_bool(strncmp(a[0]->str, a[1]->str, plen) == 0);
}
static Value *nat_ends_with(Value **a, int n) {
    (void)n; require_str(a[0],1,"ends_with"); require_str(a[1],2,"ends_with");
    size_t slen = strlen(a[0]->str), plen = strlen(a[1]->str);
    if (plen > slen) return val_bool(0);
    return val_bool(strcmp(a[0]->str + slen - plen, a[1]->str) == 0);
}
static Value *nat_replace(Value **a, int n) {
    (void)n;
    require_str(a[0],1,"replace"); require_str(a[1],2,"replace"); require_str(a[2],3,"replace");
    const char *src = a[0]->str, *old_s = a[1]->str, *rep = a[2]->str;
    size_t olen = strlen(old_s), rlen = strlen(rep);
    if (olen == 0) return val_str(src);

    size_t cnt = 0;
    const char *p = src;
    while ((p = strstr(p, old_s)) != NULL) { cnt++; p += olen; }

    size_t srclen = strlen(src);
    size_t add  = (rlen > olen) ? (rlen - olen) * cnt : 0;
    size_t sub  = (olen > rlen) ? (olen - rlen) * cnt : 0;
    size_t newlen = srclen + add - sub;
    char *out = (char *)xmalloc(newlen + 1);
    char *dst = out;
    p = src;
    const char *q;
    while ((q = strstr(p, old_s)) != NULL) {
        size_t prefix = (size_t)(q - p);
        memcpy(dst, p, prefix); dst += prefix;
        memcpy(dst, rep, rlen); dst += rlen;
        p = q + olen;
    }
    size_t tail = strlen(p);
    memcpy(dst, p, tail + 1);
    Value *v = val_str(out); free(out); return v;
}
static Value *nat_split(Value **a, int n) {
    (void)n; require_str(a[0],1,"split"); require_str(a[1],2,"split");
    const char *s = a[0]->str, *sep = a[1]->str;
    size_t seplen = strlen(sep);
    Value *list = val_list();
    if (seplen == 0) {
        for (; *s; s++) list_push(list, val_strn(s, 1));
        return list;
    }
    const char *p = s;
    const char *q;
    while ((q = strstr(p, sep)) != NULL) {
        list_push(list, val_strn(p, (size_t)(q - p)));
        p = q + seplen;
    }
    list_push(list, val_str(p));
    return list;
}
static Value *nat_join(Value **a, int n) {
    (void)n; require_list(a[0],1,"join"); require_str(a[1],2,"join");
    SengList *lst = a[0]->list;
    const char *sep = a[1]->str;
    size_t seplen = strlen(sep);
    /* compute total length */
    size_t total = 0;
    char **parts = (char **)xmalloc(sizeof(char *) * (size_t)(lst->count + 1));
    for (int i = 0; i < lst->count; i++) {
        parts[i] = val_to_string(lst->items[i]);
        total += strlen(parts[i]);
        if (i < lst->count - 1) total += seplen;
    }
    char *out = (char *)xmalloc(total + 1);
    char *dst = out;
    for (int i = 0; i < lst->count; i++) {
        size_t plen = strlen(parts[i]);
        memcpy(dst, parts[i], plen); dst += plen;
        if (i < lst->count - 1) { memcpy(dst, sep, seplen); dst += seplen; }
        free(parts[i]);
    }
    *dst = '\0';
    free(parts);
    Value *v = val_str(out); free(out); return v;
}
static Value *nat_str_num(Value **a, int n) {
    (void)n; require_str(a[0],1,"str_num");
    char *end; double d = strtod(a[0]->str, &end);
    if (end == a[0]->str)
        fatal("'str_num': cannot convert \"%s\" to a number", a[0]->str);
    return val_num(d);
}
static Value *nat_num_str(Value **a, int n) {
    (void)n; require_num(a[0],1,"num_str");
    char *s = val_to_string(a[0]);
    Value *v = val_str(s); free(s); return v;
}
static Value *nat_str_repeat(Value **a, int n) {
    (void)n; require_str(a[0],1,"str_repeat"); require_num(a[1],2,"str_repeat");
    int times = (int)a[1]->num;
    if (times < 0) times = 0;
    size_t slen = strlen(a[0]->str);
    size_t total = slen * (size_t)times;
    char *out = (char *)xmalloc(total + 1);
    char *dst = out;
    for (int i = 0; i < times; i++) { memcpy(dst, a[0]->str, slen); dst += slen; }
    *dst = '\0';
    Value *v = val_str(out); free(out); return v;
}
static Value *nat_char_at(Value **a, int n) {
    (void)n; require_str(a[0],1,"char_at"); require_num(a[1],2,"char_at");
    int idx = (int)a[1]->num - 1;
    int len = (int)strlen(a[0]->str);
    if (idx < 0 || idx >= len)
        fatal("'char_at': index %d out of range (length %d)", idx + 1, len);
    return val_strn(a[0]->str + idx, 1);
}
static Value *nat_sub_str(Value **a, int n) {
    (void)n;
    require_str(a[0],1,"sub_str"); require_num(a[1],2,"sub_str"); require_num(a[2],3,"sub_str");
    int start = (int)a[1]->num - 1;
    int end   = (int)a[2]->num;
    int len   = (int)strlen(a[0]->str);
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start >= end) return val_str("");
    return val_strn(a[0]->str + start, (size_t)(end - start));
}

static Value *nat_format(Value **a, int n) {
    (void)n; require_str(a[0],1,"format"); require_list(a[1],2,"format");
    const char *fmt = a[0]->str;
    SengList *args = a[1]->list;
    size_t cap = strlen(fmt) + 128, len = 0;
    char *res = xmalloc(cap); res[0] = '\0';
    for (const char *p = fmt; *p; ) {
        if (*p == '{' && isdigit(*(p+1))) {
            char *end;
            int idx = (int)strtol(p + 1, &end, 10);
            if (*end == '}') {
                if (idx >= 0 && idx < args->count) {
                    char *s = val_to_string(args->items[idx]);
                    size_t slen = strlen(s);
                    if (len + slen + 1 > cap) { cap = len + slen + 128; res = xrealloc(res, cap); }
                    strcpy(res + len, s); len += slen;
                    free(s); p = end + 1; continue;
                }
            }
        }
        if (len + 2 > cap) { cap *= 2; res = xrealloc(res, cap); }
        res[len++] = *p++; res[len] = '\0';
    }
    Value *v = val_str(res); free(res); return v;
}

static SengNative str_fns[] = {
    {"str_len",    nat_str_len,    1},
    {"upper",      nat_upper,      1},
    {"lower",      nat_lower,      1},
    {"trim",       nat_trim,       1},
    {"contains",   nat_contains,   2},
    {"starts_with",nat_starts_with,2},
    {"ends_with",  nat_ends_with,  2},
    {"replace",    nat_replace,    3},
    {"split",      nat_split,      2},
    {"join",       nat_join,       2},
    {"str_num",    nat_str_num,    1},
    {"num_str",    nat_num_str,    1},
    {"str_repeat", nat_str_repeat, 2},
    {"char_at",    nat_char_at,    2},
    {"sub_str",    nat_sub_str,    3},
    {"format",     nat_format,     2},
    {NULL, NULL, 0}
};

static void pkg_string(Env *g) {
    for (int i = 0; str_fns[i].name; i++) {
        Value *v = val_native(&str_fns[i]);
        env_set(g, str_fns[i].name, v);
        val_deref(v);
    }
}

/* ═══════════════════════════════════════════════════════════════
   io package
   ═══════════════════════════════════════════════════════════════ */

static Value *nat_read_file(Value **a, int n) {
    (void)n; require_str(a[0],1,"read_file");
    char *src = read_file(a[0]->str);
    if (!src) fatal("'read_file': cannot open file '%s'", a[0]->str);
    Value *v = val_str(src); free(src); return v;
}
static Value *nat_write_file(Value **a, int n) {
    (void)n; require_str(a[0],1,"write_file"); require_str(a[1],2,"write_file");
    FILE *f = fopen(a[0]->str, "w");
    if (!f) fatal("'write_file': cannot write file '%s'", a[0]->str);
    fputs(a[1]->str, f);
    fclose(f);
    return val_bool(1);
}
static Value *nat_file_exists(Value **a, int n) {
    (void)n; require_str(a[0],1,"file_exists");
    FILE *f = fopen(a[0]->str, "r");
    if (f) { fclose(f); return val_bool(1); }
    return val_bool(0);
}
static Value *nat_print_inline(Value **a, int n) {
    (void)n; require_str(a[0],1,"print_inline");
    printf("%s", a[0]->str);
    fflush(stdout);
    return val_null();
}

static Value *nat_append_file(Value **a, int n) {
    (void)n; require_str(a[0],1,"append_file"); require_str(a[1],2,"append_file");
    FILE *f = fopen(a[0]->str, "a");
    if (!f) fatal("'append_file': cannot open '%s'", a[0]->str);
    fputs(a[1]->str, f);
    fclose(f);
    return val_bool(1);
}
static Value *nat_delete_file(Value **a, int n) {
    (void)n; require_str(a[0],1,"delete_file");
    return val_bool(remove(a[0]->str) == 0);
}
static Value *nat_rename_file(Value **a, int n) {
    (void)n; require_str(a[0],1,"rename_file"); require_str(a[1],2,"rename_file");
    return val_bool(rename(a[0]->str, a[1]->str) == 0);
}
static Value *nat_file_size(Value **a, int n) {
    (void)n; require_str(a[0],1,"file_size");
    struct stat st;
    if (stat(a[0]->str, &st) != 0) fatal("'file_size': cannot stat '%s'", a[0]->str);
    return val_num((double)st.st_size);
}
static Value *nat_dir_exists(Value **a, int n) {
    (void)n; require_str(a[0],1,"dir_exists");
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(a[0]->str);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) return val_bool(1);
    return val_bool(0);
#else
    struct stat st;
    if (stat(a[0]->str, &st) == 0 && S_ISDIR(st.st_mode)) return val_bool(1);
    return val_bool(0);
#endif
}
static Value *nat_make_dir(Value **a, int n) {
    (void)n; require_str(a[0],1,"make_dir");
#ifdef _WIN32
    return val_bool(_mkdir(a[0]->str) == 0);
#else
    return val_bool(mkdir(a[0]->str, 0755) == 0);
#endif
}
static Value *nat_list_dir(Value **a, int n) {
    (void)n; require_str(a[0],1,"list_dir");
    Value *lst = val_list();
#ifdef _WIN32
    char search_path[MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s\\*", a[0]->str);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_path, &fd);
    if (hFind == INVALID_HANDLE_VALUE) fatal("'list_dir': cannot open directory '%s'", a[0]->str);
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        list_push(lst, val_str(fd.cFileName));
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    DIR *dir = opendir(a[0]->str);
    if (!dir) fatal("'list_dir': cannot open directory '%s'", a[0]->str);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        list_push(lst, val_str(entry->d_name));
    }
    closedir(dir);
#endif
    return lst;
}
static Value *nat_get_cwd(Value **a, int n) {
    (void)a; (void)n;
    char buf[4096];
#ifdef _WIN32
    if (!_getcwd(buf, sizeof buf)) fatal("'get_cwd': failed");
#else
    if (!getcwd(buf, sizeof buf)) fatal("'get_cwd': failed");
#endif
    /* normalize backslashes on Windows */
    for (int i = 0; buf[i]; i++) if (buf[i] == '\\') buf[i] = '/';
    return val_str(buf);
}

static SengNative io_fns[] = {
    {"read_file",    nat_read_file,   1},
    {"write_file",   nat_write_file,  2},
    {"append_file",  nat_append_file, 2},
    {"delete_file",  nat_delete_file, 1},
    {"rename_file",  nat_rename_file, 2},
    {"file_exists",  nat_file_exists, 1},
    {"file_size",    nat_file_size,   1},
    {"dir_exists",   nat_dir_exists,  1},
    {"make_dir",     nat_make_dir,    1},
    {"list_dir",     nat_list_dir,    1},
    {"get_cwd",      nat_get_cwd,     0},
    {"print_inline", nat_print_inline,1},
    {NULL, NULL, 0}
};

static void pkg_io(Env *g) {
    for (int i = 0; io_fns[i].name; i++) {
        Value *v = val_native(&io_fns[i]);
        env_set(g, io_fns[i].name, v);
        val_deref(v);
    }
}

/* ═══════════════════════════════════════════════════════════════
   type package
   ═══════════════════════════════════════════════════════════════ */

static Value *nat_to_num(Value **a, int n) {
    (void)n;
    if (!a[0]) fatal("'to_num': argument is nothing");
    switch (a[0]->type) {
        case VAL_NUM:  return val_num(a[0]->num);
        case VAL_BOOL: return val_num(a[0]->bool_val ? 1.0 : 0.0);
        case VAL_STR: {
            char *end; double d = strtod(a[0]->str, &end);
            if (end == a[0]->str)
                fatal("'to_num': cannot convert \"%s\" to a number", a[0]->str);
            return val_num(d);
        }
        default: fatal("'to_num': cannot convert this value to a number"); return val_null();
    }
}
static Value *nat_to_str(Value **a, int n) {
    (void)n; char *s = val_to_string(a[0]); Value *v = val_str(s); free(s); return v;
}
static Value *nat_is_num(Value **a, int n)      { (void)n; return val_bool(a[0] && a[0]->type == VAL_NUM);    }
static Value *nat_is_str(Value **a, int n)      { (void)n; return val_bool(a[0] && a[0]->type == VAL_STR);    }
static Value *nat_is_bool(Value **a, int n)     { (void)n; return val_bool(a[0] && a[0]->type == VAL_BOOL);   }
static Value *nat_is_list_val(Value **a, int n) { (void)n; return val_bool(a[0] && a[0]->type == VAL_LIST);   }
static Value *nat_is_nothing(Value **a, int n)  { (void)n; return val_bool(!a[0] || a[0]->type == VAL_NULL);  }
static Value *nat_type_of(Value **a, int n) {
    (void)n;
    if (!a[0]) return val_str("nothing");
    switch (a[0]->type) {
        case VAL_NUM:    return val_str("number");
        case VAL_STR:    return val_str("string");
        case VAL_BOOL:   return val_str("boolean");
        case VAL_NULL:   return val_str("nothing");
        case VAL_LIST:   return val_str("list");
        case VAL_FUNC:   return val_str("function");
        case VAL_NATIVE: return val_str("function");
        default:         return val_str("unknown");
    }
}

static Value *nat_is_circular(Value **a, int n) {
    (void)n;
    if (!a[0]) return val_bool(0);
    return val_bool(val_is_circular(a[0]));
}

static SengNative type_fns[] = {
    {"to_num",      nat_to_num,      1},
    {"to_str",      nat_to_str,      1},
    {"is_num",      nat_is_num,      1},
    {"is_str",      nat_is_str,      1},
    {"is_bool",     nat_is_bool,     1},
    {"is_list_val", nat_is_list_val, 1},
    {"is_nothing",  nat_is_nothing,  1},
    {"type_of",     nat_type_of,     1},
    {"is_circular", nat_is_circular, 1},
    {NULL, NULL, 0}
};

static void pkg_type(Env *g) {
    for (int i = 0; type_fns[i].name; i++) {
        Value *v = val_native(&type_fns[i]);
        env_set(g, type_fns[i].name, v);
        val_deref(v);
    }
}

/* ═══════════════════════════════════════════════════════════════
   http package  (WinINet on Windows, stub on other platforms)
   ═══════════════════════════════════════════════════════════════ */

#ifdef _WIN32
/* Shared HTTP helper: performs a GET or POST via WinINet.
   Returns heap-allocated response body (caller frees), or NULL on error. */
static char *wininet_request(const char *url, const char *method,
                             const char *extra_headers, const char *body,
                             size_t body_len) {
    HINTERNET hInet = InternetOpenA("SENG/1.0", INTERNET_OPEN_TYPE_PRECONFIG,
                                    NULL, NULL, 0);
    if (!hInet) return NULL;

    /* ── GET: simple path via InternetOpenUrlA ── */
    if (strcmp(method, "GET") == 0) {
        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
        HINTERNET hUrl = InternetOpenUrlA(hInet, url,
                             extra_headers, extra_headers ? (DWORD)-1L : 0,
                             flags, 0);
        if (!hUrl) { InternetCloseHandle(hInet); return NULL; }
        char chunk[4096]; DWORD got;
        size_t total = 0, cap = 65536;
        char *out = (char *)xmalloc(cap);
        while (InternetReadFile(hUrl, chunk, sizeof chunk, &got) && got > 0) {
            if (total + got + 1 > cap) { cap = (cap + got) * 2; out = (char *)xrealloc(out, cap); }
            memcpy(out + total, chunk, got); total += got;
        }
        out[total] = '\0';
        InternetCloseHandle(hUrl); InternetCloseHandle(hInet);
        return out;
    }

    /* ── POST/PUT/etc: crack URL then use HttpXxx API ── */
    URL_COMPONENTSA uc; memset(&uc, 0, sizeof uc);
    uc.dwStructSize = sizeof uc;
    char scheme[16]={0}, host[512]={0}, path[4096]={0};
    uc.lpszScheme=scheme; uc.dwSchemeLength=sizeof scheme;
    uc.lpszHostName=host; uc.dwHostNameLength=sizeof host;
    uc.lpszUrlPath=path;  uc.dwUrlPathLength=sizeof path;
    if (!InternetCrackUrlA(url, 0, 0, &uc)) { InternetCloseHandle(hInet); return NULL; }
    if (uc.nPort == 0)
        uc.nPort = (uc.nScheme == INTERNET_SCHEME_HTTPS)
                   ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;

    HINTERNET hConn = InternetConnectA(hInet, host, uc.nPort,
                                        NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); return NULL; }

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
    if (uc.nScheme == INTERNET_SCHEME_HTTPS) flags |= INTERNET_FLAG_SECURE;

    HINTERNET hReq = HttpOpenRequestA(hConn, method, path, NULL, NULL, NULL, flags, 0);
    if (!hReq) { InternetCloseHandle(hConn); InternetCloseHandle(hInet); return NULL; }

    BOOL ok = HttpSendRequestA(hReq,
                                extra_headers, extra_headers ? (DWORD)-1L : 0,
                                (LPVOID)body, body ? (DWORD)body_len : 0);
    char *out = NULL;
    if (ok) {
        char chunk[4096]; DWORD got;
        size_t total = 0, cap = 65536;
        out = (char *)xmalloc(cap);
        while (InternetReadFile(hReq, chunk, sizeof chunk, &got) && got > 0) {
            if (total + got + 1 > cap) { cap = (cap + got) * 2; out = (char *)xrealloc(out, cap); }
            memcpy(out + total, chunk, got); total += got;
        }
        out[total] = '\0';
    }
    InternetCloseHandle(hReq); InternetCloseHandle(hConn); InternetCloseHandle(hInet);
    return out;
}
#endif /* _WIN32 */

static Value *nat_http_get(Value **a, int n) {
    (void)n; require_str(a[0],1,"http_get");
#ifdef _WIN32
    char *body = wininet_request(a[0]->str, "GET", NULL, NULL, 0);
    if (!body) fatal("'http_get': request failed for '%s'", a[0]->str);
    Value *v = val_str(body); free(body); return v;
#else
    char cmd[2048];
    snprintf(cmd, sizeof cmd, "curl -s \"%s\"", a[0]->str);
    FILE *fp = popen(cmd, "r");
    if (!fp) fatal("'http_get': curl failed");
    char *res = NULL; size_t rlen = 0, rcap = 0;
    char buf[1024];
    while (fgets(buf, sizeof buf, fp)) {
        size_t blen = strlen(buf);
        if (rlen + blen + 1 > rcap) { rcap = rcap ? rcap * 2 : 1024; res = xrealloc(res, rcap); }
        if (rlen == 0) res[0] = '\0';
        strcat(res, buf); rlen += blen;
    }
    pclose(fp);
    if (!res) res = xstrdup("");
    Value *v = val_str(res); free(res); return v;
#endif
}
static Value *nat_http_post(Value **a, int n) {
    (void)n; require_str(a[0],1,"http_post"); require_str(a[1],2,"http_post");
#ifdef _WIN32
    const char *hdrs = "Content-Type: text/plain\r\n";
    char *resp = wininet_request(a[0]->str, "POST", hdrs, a[1]->str, strlen(a[1]->str));
    if (!resp) fatal("'http_post': request failed for '%s'", a[0]->str);
    Value *v = val_str(resp); free(resp); return v;
#else
    char cmd[4096];
    snprintf(cmd, sizeof cmd, "curl -s -X POST -d '%s' \"%s\"", a[1]->str, a[0]->str);
    FILE *fp = popen(cmd, "r");
    if (!fp) fatal("'http_post': curl failed");
    char *res = NULL; size_t rlen = 0, rcap = 0;
    char buf[1024];
    while (fgets(buf, sizeof buf, fp)) {
        size_t blen = strlen(buf);
        if (rlen + blen + 1 > rcap) { rcap = rcap ? rcap * 2 : 1024; res = xrealloc(res, rcap); }
        if (rlen == 0) res[0] = '\0';
        strcat(res, buf); rlen += blen;
    }
    pclose(fp);
    if (!res) res = xstrdup("");
    Value *v = val_str(res); free(res); return v;
#endif
}
static Value *nat_http_post_json(Value **a, int n) {
    (void)n; require_str(a[0],1,"http_post_json"); require_str(a[1],2,"http_post_json");
#ifdef _WIN32
    const char *hdrs = "Content-Type: application/json\r\n";
    char *resp = wininet_request(a[0]->str, "POST", hdrs, a[1]->str, strlen(a[1]->str));
    if (!resp) fatal("'http_post_json': request failed for '%s'", a[0]->str);
    Value *v = val_str(resp); free(resp); return v;
#else
    char cmd[4096];
    snprintf(cmd, sizeof cmd, "curl -s -X POST -H 'Content-Type: application/json' -d '%s' \"%s\"", a[1]->str, a[0]->str);
    FILE *fp = popen(cmd, "r");
    if (!fp) fatal("'http_post_json': curl failed");
    char *res = NULL; size_t rlen = 0, rcap = 0;
    char buf[1024];
    while (fgets(buf, sizeof buf, fp)) {
        size_t blen = strlen(buf);
        if (rlen + blen + 1 > rcap) { rcap = rcap ? rcap * 2 : 1024; res = xrealloc(res, rcap); }
        if (rlen == 0) res[0] = '\0';
        strcat(res, buf); rlen += blen;
    }
    pclose(fp);
    if (!res) res = xstrdup("");
    Value *v = val_str(res); free(res); return v;
#endif
}
static Value *nat_http_put(Value **a, int n) {
    (void)n; require_str(a[0],1,"http_put"); require_str(a[1],2,"http_put");
#ifdef _WIN32
    const char *hdrs = "Content-Type: text/plain\r\n";
    char *resp = wininet_request(a[0]->str, "PUT", hdrs, a[1]->str, strlen(a[1]->str));
    if (!resp) fatal("'http_put': request failed for '%s'", a[0]->str);
    Value *v = val_str(resp); free(resp); return v;
#else
    char cmd[4096];
    snprintf(cmd, sizeof cmd, "curl -s -X PUT -d '%s' \"%s\"", a[1]->str, a[0]->str);
    FILE *fp = popen(cmd, "r");
    if (!fp) fatal("'http_put': curl failed");
    char *res = NULL; size_t rlen = 0, rcap = 0;
    char buf[1024];
    while (fgets(buf, sizeof buf, fp)) {
        size_t blen = strlen(buf);
        if (rlen + blen + 1 > rcap) { rcap = rcap ? rcap * 2 : 1024; res = xrealloc(res, rcap); }
        if (rlen == 0) res[0] = '\0';
        strcat(res, buf); rlen += blen;
    }
    pclose(fp);
    if (!res) res = xstrdup("");
    Value *v = val_str(res); free(res); return v;
#endif
}
static Value *nat_http_delete(Value **a, int n) {
    (void)n; require_str(a[0],1,"http_delete");
#ifdef _WIN32
    char *resp = wininet_request(a[0]->str, "DELETE", NULL, NULL, 0);
    if (!resp) fatal("'http_delete': request failed for '%s'", a[0]->str);
    Value *v = val_str(resp); free(resp); return v;
#else
    char cmd[2048];
    snprintf(cmd, sizeof cmd, "curl -s -X DELETE \"%s\"", a[0]->str);
    FILE *fp = popen(cmd, "r");
    if (!fp) fatal("'http_delete': curl failed");
    char *res = NULL; size_t rlen = 0, rcap = 0;
    char buf[1024];
    while (fgets(buf, sizeof buf, fp)) {
        size_t blen = strlen(buf);
        if (rlen + blen + 1 > rcap) { rcap = rcap ? rcap * 2 : 1024; res = xrealloc(res, rcap); }
        if (rlen == 0) res[0] = '\0';
        strcat(res, buf); rlen += blen;
    }
    pclose(fp);
    if (!res) res = xstrdup("");
    Value *v = val_str(res); free(res); return v;
#endif
}

static SengNative http_fns[] = {
    {"http_get",       nat_http_get,       1},
    {"http_post",      nat_http_post,      2},
    {"http_post_json", nat_http_post_json, 2},
    {"http_put",       nat_http_put,       2},
    {"http_delete",    nat_http_delete,    1},
    {NULL, NULL, 0}
};

static void pkg_http(Env *g) {
    for (int i = 0; http_fns[i].name; i++) {
        Value *v = val_native(&http_fns[i]);
        env_set(g, http_fns[i].name, v);
        val_deref(v);
    }
}

/* ═══════════════════════════════════════════════════════════════
   sys package
   ═══════════════════════════════════════════════════════════════ */

static int   sys_argc = 0;
static char **sys_argv = NULL;

void pkg_set_args(int argc, char **argv) {
    sys_argc = argc;
    sys_argv = argv;
}

static Value *nat_args(Value **a, int n) {
    (void)a; (void)n;
    Value *list = val_list();
    for (int i = 0; i < sys_argc; i++) {
        list_push(list, val_str(xstrdup(sys_argv[i])));
    }
    return list;
}

static Value *nat_sleep_ms(Value **a, int n) {
    (void)n; require_num(a[0],1,"sleep_ms");
    int ms = (int)a[0]->num; if (ms < 0) ms = 0;
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((unsigned int)ms * 1000u);
#endif
    return val_null();
}
static Value *nat_time_ms(Value **a, int n) {
    (void)a; (void)n;
#ifdef _WIN32
    return val_num((double)GetTickCount64());
#else
    return val_num((double)(clock() * 1000 / CLOCKS_PER_SEC));
#endif
}
static Value *nat_timestamp(Value **a, int n) {
    (void)a; (void)n;
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char buf[32];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", tm_info);
    return val_str(buf);
}
static Value *nat_env_get(Value **a, int n) {
    (void)n; require_str(a[0],1,"env_get");
    const char *val = getenv(a[0]->str);
    return val ? val_str(val) : val_null();
}
static Value *nat_run_cmd(Value **a, int n) {
    (void)n; require_str(a[0],1,"run_cmd");
#ifdef _WIN32
    FILE *fp = _popen(a[0]->str, "r");
#else
    FILE *fp = popen(a[0]->str, "r");
#endif
    if (!fp) fatal("'run_cmd': cannot run '%s'", a[0]->str);
    char chunk[1024];
    size_t total = 0, cap = 65536;
    char *out = (char *)xmalloc(cap);
    size_t got;
    while ((got = fread(chunk, 1, sizeof chunk, fp)) > 0) {
        if (total + got + 1 > cap) { cap = (cap + got) * 2; out = (char *)xrealloc(out, cap); }
        memcpy(out + total, chunk, got); total += got;
    }
    out[total] = '\0';
#ifdef _WIN32
    _pclose(fp);
#else
    pclose(fp);
#endif
    Value *v = val_str(out); free(out); return v;
}
static Value *nat_exit_code(Value **a, int n) {
    (void)n;
    int code = (a[0] && a[0]->type == VAL_NUM) ? (int)a[0]->num : 0;
    exit(code);
    return val_null();
}

static SengNative sys_fns[] = {
    {"sleep_ms",  nat_sleep_ms,  1},
    {"time_ms",   nat_time_ms,   0},
    {"timestamp", nat_timestamp, 0},
    {"env_get",   nat_env_get,   1},
    {"run_cmd",   nat_run_cmd,   1},
    {"exit_code", nat_exit_code, 1},
    {"args",      nat_args,      0},
    {NULL, NULL, 0}
};

static void pkg_sys(Env *g) {
    for (int i = 0; sys_fns[i].name; i++) {
        Value *v = val_native(&sys_fns[i]);
        env_set(g, sys_fns[i].name, v);
        val_deref(v);
    }
}

/* ═══════════════════════════════════════════════════════════════
   JSON package – minimal recursive-descent parser + builder
   ═══════════════════════════════════════════════════════════════ */

typedef struct { const char *src; int pos; } JP;
static Value *jp_value(JP *p);  /* forward */

static void jp_ws(JP *p) {
    while (p->src[p->pos]==' '||p->src[p->pos]=='\t'||
           p->src[p->pos]=='\n'||p->src[p->pos]=='\r') p->pos++;
}
static Value *jp_string(JP *p) {
    p->pos++;
    size_t cap=64, len=0;
    char *buf=(char*)xmalloc(cap);
    while (p->src[p->pos] && p->src[p->pos]!='"') {
        char c=p->src[p->pos++];
        if (c=='\\' && p->src[p->pos]) {
            char e=p->src[p->pos++];
            switch(e){case '"':c='"';break;case '\\':c='\\';break;
                       case '/':c='/';break;case 'n':c='\n';break;
                       case 'r':c='\r';break;case 't':c='\t';break;
                       default:c=e;break;}
        }
        if (len+2>cap){cap*=2;buf=(char*)xrealloc(buf,cap);}
        buf[len++]=c;
    }
    if (p->src[p->pos]=='"') p->pos++;
    buf[len]='\0';
    Value *v=val_str(buf); free(buf); return v;
}
static Value *jp_array(JP *p) {
    p->pos++;
    Value *lst=val_list(); jp_ws(p);
    if (p->src[p->pos]==']'){p->pos++;return lst;}
    for(;;){
        jp_ws(p);
        Value *e=jp_value(p); list_push(lst,e);
        jp_ws(p);
        if(p->src[p->pos]==','){p->pos++;continue;} break;
    }
    jp_ws(p); if(p->src[p->pos]==']') p->pos++;
    return lst;
}
/* objects → flat list: [key,val,key,val,...] */
static Value *jp_object(JP *p) {
    p->pos++;
    Value *lst=val_list(); jp_ws(p);
    if(p->src[p->pos]=='}'){p->pos++;return lst;}
    for(;;){
        jp_ws(p);
        if(p->src[p->pos]!='"') break;
        Value *key=jp_string(p); jp_ws(p);
        if(p->src[p->pos]==':') p->pos++;
        jp_ws(p);
        Value *val=jp_value(p);
        list_push(lst,key); list_push(lst,val);
        jp_ws(p);
        if(p->src[p->pos]==','){p->pos++;continue;} break;
    }
    jp_ws(p); if(p->src[p->pos]=='}') p->pos++;
    return lst;
}
static Value *jp_value(JP *p) {
    jp_ws(p);
    char c=p->src[p->pos];
    if(c=='"') return jp_string(p);
    if(c=='[') return jp_array(p);
    if(c=='{') return jp_object(p);
    if(c=='t'&&strncmp(p->src+p->pos,"true",4)==0){p->pos+=4;return val_bool(1);}
    if(c=='f'&&strncmp(p->src+p->pos,"false",5)==0){p->pos+=5;return val_bool(0);}
    if(c=='n'&&strncmp(p->src+p->pos,"null",4)==0){p->pos+=4;return val_null();}
    if(c=='-'||(c>='0'&&c<='9')){char*e;double d=strtod(p->src+p->pos,&e);p->pos=(int)(e-p->src);return val_num(d);}
    return val_null();
}

/* get value by key from a parsed flat-list object */
static Value *obj_get_key(Value *obj, const char *key) {
    if(!obj||obj->type!=VAL_LIST) return NULL;
    for(int i=0;i+1<obj->list->count;i+=2){
        Value *k=obj->list->items[i];
        if(k->type==VAL_STR&&strcmp(k->str,key)==0)
            return obj->list->items[i+1];
    }
    return NULL;
}

/* stringify buffer helpers */
static void jbuf_c(char **b, size_t *l, size_t *c, char ch) {
    if(*l+2>*c){*c=(*c)*2+64;*b=(char*)xrealloc(*b,*c);}
    (*b)[(*l)++]=ch; (*b)[*l]='\0';
}
static void jbuf_s(char **b, size_t *l, size_t *c, const char *s) {
    for(;*s;s++) jbuf_c(b,l,c,*s);
}
static void jval_str(Value *v, char **b, size_t *l, size_t *c);
static void jval_str(Value *v, char **b, size_t *l, size_t *c) {
    if(!v||v->type==VAL_NULL){jbuf_s(b,l,c,"null");return;}
    if(v->type==VAL_BOOL){jbuf_s(b,l,c,v->bool_val?"true":"false");return;}
    if(v->type==VAL_NUM){
        char tmp[64];
        if(v->num==(double)(long long)v->num&&v->num>=-1e15&&v->num<=1e15)
            snprintf(tmp,sizeof tmp,"%lld",(long long)v->num);
        else snprintf(tmp,sizeof tmp,"%.10g",v->num);
        jbuf_s(b,l,c,tmp); return;
    }
    if(v->type==VAL_STR){
        jbuf_c(b,l,c,'"');
        for(const char *s=v->str;*s;s++){
            switch(*s){
                case '"':  jbuf_s(b,l,c,"\\\""); break;
                case '\\': jbuf_s(b,l,c,"\\\\"); break;
                case '\n': jbuf_s(b,l,c,"\\n");  break;
                case '\r': jbuf_s(b,l,c,"\\r");  break;
                case '\t': jbuf_s(b,l,c,"\\t");  break;
                default:   jbuf_c(b,l,c,*s);     break;
            }
        }
        jbuf_c(b,l,c,'"'); return;
    }
    if(v->type==VAL_LIST){
        jbuf_c(b,l,c,'[');
        for(int i=0;i<v->list->count;i++){
            if(i) jbuf_c(b,l,c,',');
            jval_str(v->list->items[i],b,l,c);
        }
        jbuf_c(b,l,c,']'); return;
    }
    jbuf_s(b,l,c,"null");
}

/* native functions */
static Value *nat_json_parse(Value **a, int n) {
    (void)n; require_str(a[0],1,"json_parse");
    JP p={a[0]->str,0}; return jp_value(&p);
}
static Value *nat_json_get(Value **a, int n) {
    (void)n; require_str(a[0],1,"json_get"); require_str(a[1],2,"json_get");
    JP p={a[0]->str,0}; Value *obj=jp_value(&p);
    Value *found=obj_get_key(obj,a[1]->str);
    Value *res=found?(val_ref(found),found):val_null();
    val_deref(obj); return res;
}
static Value *nat_json_get_path(Value **a, int n) {
    (void)n; require_str(a[0],1,"json_get_path"); require_str(a[1],2,"json_get_path");
    JP p={a[0]->str,0};
    Value *cur=jp_value(&p);
    const char *s=a[1]->str;
    while(*s&&cur){
        const char *dot=strchr(s,'.');
        size_t klen=dot?(size_t)(dot-s):strlen(s);
        char key[256]; if(klen>=sizeof key) klen=sizeof key-1;
        memcpy(key,s,klen); key[klen]='\0';
        Value *found=obj_get_key(cur,key);
        Value *next=found?(val_ref(found),found):val_null();
        val_deref(cur); cur=next;
        if(!dot) break;
        s=dot+1;
    }
    return cur?cur:val_null();
}
static Value *nat_json_has(Value **a, int n) {
    (void)n; require_str(a[0],1,"json_has"); require_str(a[1],2,"json_has");
    JP p={a[0]->str,0}; Value *obj=jp_value(&p);
    int found=(obj_get_key(obj,a[1]->str)!=NULL);
    val_deref(obj); return val_bool(found);
}
static Value *nat_json_keys(Value **a, int n) {
    (void)n; require_str(a[0],1,"json_keys");
    JP p={a[0]->str,0}; Value *obj=jp_value(&p);
    Value *keys=val_list();
    if(obj->type==VAL_LIST){
        for(int i=0;i+1<obj->list->count;i+=2){
            Value *k=obj->list->items[i];
            if(k->type==VAL_STR){Value *s=val_str(k->str);list_push(keys,s);}
        }
    }
    val_deref(obj); return keys;
}
static Value *nat_json_array_len(Value **a, int n) {
    (void)n; require_str(a[0],1,"json_array_len");
    JP p={a[0]->str,0}; Value *arr=jp_value(&p);
    int len=(arr->type==VAL_LIST)?arr->list->count:0;
    val_deref(arr); return val_num((double)len);
}
static Value *nat_json_array_get(Value **a, int n) {
    (void)n; require_str(a[0],1,"json_array_get"); require_num(a[1],2,"json_array_get");
    JP p={a[0]->str,0}; Value *arr=jp_value(&p);
    int idx=(int)a[1]->num-1;
    Value *res;
    if(arr->type==VAL_LIST&&idx>=0&&idx<arr->list->count){
        val_ref(arr->list->items[idx]); res=arr->list->items[idx];
    } else res=val_null();
    val_deref(arr); return res;
}
static Value *nat_json_stringify(Value **a, int n) {
    (void)n;
    size_t len=0,cap=128; char *buf=(char*)xmalloc(cap); buf[0]='\0';
    jval_str(a[0],&buf,&len,&cap);
    Value *v=val_str(buf); free(buf); return v;
}
static Value *nat_json_make_obj(Value **a, int n) {
    (void)n; require_list(a[0],1,"json_make_obj"); require_list(a[1],2,"json_make_obj");
    SengList *keys=a[0]->list, *vals=a[1]->list;
    int cnt=keys->count<vals->count?keys->count:vals->count;
    size_t len=0,cap=128; char *buf=(char*)xmalloc(cap); buf[0]='\0';
    jbuf_c(&buf,&len,&cap,'{');
    for(int i=0;i<cnt;i++){
        if(i) jbuf_c(&buf,&len,&cap,',');
        jval_str(keys->items[i],&buf,&len,&cap);
        jbuf_c(&buf,&len,&cap,':');
        jval_str(vals->items[i],&buf,&len,&cap);
    }
    jbuf_c(&buf,&len,&cap,'}');
    Value *v=val_str(buf); free(buf); return v;
}
static Value *nat_json_pretty(Value **a, int n) {
    (void)n; require_str(a[0],1,"json_pretty");
    const char *src=a[0]->str;
    size_t cap=strlen(src)*4+64, len=0;
    char *buf=(char*)xmalloc(cap); buf[0]='\0';
    int depth=0, in_str=0; char last=0;
    for(const char *p=src; *p; p++){
        char c=*p;
        if(in_str){
            if(c=='\\'&&*(p+1)){jbuf_c(&buf,&len,&cap,c);jbuf_c(&buf,&len,&cap,*++p);}
            else{jbuf_c(&buf,&len,&cap,c);if(c=='"')in_str=0;}
            last=c; continue;
        }
        if(c==' '||c=='\t'||c=='\n'||c=='\r') continue;
        if(c=='"'){in_str=1;jbuf_c(&buf,&len,&cap,c);last=c;continue;}
        if(c=='{'||c=='['){
            jbuf_c(&buf,&len,&cap,c); depth++;
            const char *q=p+1;
            while(*q==' '||*q=='\t'||*q=='\n'||*q=='\r') q++;
            if(!((c=='{'&&*q=='}')||(c=='['&&*q==']'))){
                jbuf_c(&buf,&len,&cap,'\n');
                for(int i=0;i<depth;i++){jbuf_c(&buf,&len,&cap,' ');jbuf_c(&buf,&len,&cap,' ');}
            }
            last=c; continue;
        }
        if(c=='}'||c==']'){
            if(last!='{'&&last!='['){
                depth--;
                jbuf_c(&buf,&len,&cap,'\n');
                for(int i=0;i<depth;i++){jbuf_c(&buf,&len,&cap,' ');jbuf_c(&buf,&len,&cap,' ');}
            } else depth--;
            jbuf_c(&buf,&len,&cap,c); last=c; continue;
        }
        if(c==','){
            jbuf_c(&buf,&len,&cap,c); jbuf_c(&buf,&len,&cap,'\n');
            for(int i=0;i<depth;i++){jbuf_c(&buf,&len,&cap,' ');jbuf_c(&buf,&len,&cap,' ');}
            last=c; continue;
        }
        if(c==':'){jbuf_c(&buf,&len,&cap,':');jbuf_c(&buf,&len,&cap,' ');last=c;continue;}
        jbuf_c(&buf,&len,&cap,c); last=c;
    }
    Value *v=val_str(buf); free(buf); return v;
}

static SengNative json_fns[] = {
    {"json_parse",     nat_json_parse,     1},
    {"json_get",       nat_json_get,       2},
    {"json_get_path",  nat_json_get_path,  2},
    {"json_has",       nat_json_has,       2},
    {"json_keys",      nat_json_keys,      1},
    {"json_array_len", nat_json_array_len, 1},
    {"json_array_get", nat_json_array_get, 2},
    {"json_stringify", nat_json_stringify, 1},
    {"json_make_obj",  nat_json_make_obj,  2},
    {"json_pretty",    nat_json_pretty,    1},
    {NULL, NULL, 0}
};
static void pkg_json(Env *g) {
    for(int i=0;json_fns[i].name;i++){
        Value *v=val_native(&json_fns[i]); env_set(g,json_fns[i].name,v); val_deref(v);
    }
}

/* ═══════════════════════════════════════════════════════════════
   public registry
   ═══════════════════════════════════════════════════════════════ */

int pkg_register(Env *globals, const char *name) {
    if (strcmp(name, "math")   == 0) { pkg_math(globals);   return 1; }
    if (strcmp(name, "string") == 0) { pkg_string(globals); return 1; }
    if (strcmp(name, "io")     == 0) { pkg_io(globals);     return 1; }
    if (strcmp(name, "type")   == 0) { pkg_type(globals);   return 1; }
    if (strcmp(name, "http")   == 0) { pkg_http(globals);   return 1; }
    if (strcmp(name, "sys")    == 0) { pkg_sys(globals);    return 1; }
    if (strcmp(name, "json")   == 0) { pkg_json(globals);   return 1; }
    if (strcmp(name, "time")   == 0) { pkg_time(globals);   return 1; }
    return 0;
}
