
#include "common.h"
#include "lexer.h"
#include "parser.h"
#include "interp.h"
#include "compiler.h"
#include "vm.h"
#include "packages.h"
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
  #include <direct.h>
  #define mkdir(path, mode) _mkdir(path)
  #include <sys/stat.h>
  #include <sys/types.h>
#else
  #include <sys/stat.h>
  #include <sys/types.h>
#endif

static void print_usage(void) {
    printf(
        "seng v1.0.2 — Simple English Programming Language\n"
        "\n"
        "Usage:\n"
        "  seng <file.se>              Run a .se source file (uses _secache if available)\n"
        "  seng compile <file.se>      Compile to _secache/ folder\n"
        "  seng run <file.sec>         Execute a compiled .sec file\n"
        "  seng disasm <file.sec>      Disassemble a .sec file\n"
        "  seng repl                   Start an interactive shell\n"
        "  seng help                   Show this help\n"
        "\n"
        "File extensions:\n"
        "  .se   — seng source file\n"
        "  .sec  — seng compiled bytecode\n"
    );
}

static int is_block_start(const char *line) {
    const char *starts[] = {"if ", "while ", "repeat ", "for ", "try ", "define ", "blueprint ", "create blueprint "};
    for (int i = 0; i < 8; i++) {
        if (strstr(line, starts[i]) == line) return 1;
    }
    return 0;
}

static void run_repl(void) {
    printf("seng REPL v" SENG_VERSION "\nType 'stop' or Ctrl+D to exit.\n");
    Interp *in = interp_new();
    char line[4096];
    char buffer[16384] = {0};
    int block_level = 0;

    while (1) {
        if (block_level == 0) printf("> ");
        else printf(".. ");
        
        if (!fgets(line, sizeof(line), stdin)) break;
        if (strchr(line, '\x04') || strcmp(line, "stop\n") == 0 || strcmp(line, "stop\r\n") == 0) break;
        
        strcat(buffer, line);

        /* Simple block level tracking */
        if (strstr(line, "if ") || strstr(line, "while ") || strstr(line, "repeat ") || 
            strstr(line, "for ") || strstr(line, "try ") || strstr(line, "define ") || 
            strstr(line, "blueprint ")) {
            block_level++;
        }
        if (strstr(line, "end\n") || strcmp(line, "end") == 0) {
            block_level--;
        }

        if (block_level <= 0) {
            block_level = 0;
            if (buffer[0] != '\n') {
                Lexer  *lex  = lexer_new(buffer);
                Parser *par  = parser_new(lex);
                Node   *prog = parse(par);
                if (prog) {
                    interp_exec(in, prog);
                    node_free(prog);
                }
                parser_free(par);
                lexer_free(lex);
            }
            buffer[0] = '\0';
        }
    }
    printf("\n");
    interp_free(in);
}

/* build output path: store in _secache folder, like python's __pycache__ */
static char *make_sec_path(const char *raw_src_path) {
    const char *src_path = raw_src_path;
    if (src_path[0] == '/' || src_path[0] == '\\') {
        FILE *chk = fopen(raw_src_path, "rb");
        if (chk) fclose(chk);
        else src_path = raw_src_path + 1;
    }
    const char *slash = strrchr(src_path, '/');
    const char *backslash = strrchr(src_path, '\\');
    if (backslash > slash) slash = backslash;
    char dir[1024] = {0};
    const char *fname = src_path;

    if (slash) {
        size_t dlen = (size_t)(slash - src_path);
        if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
        memcpy(dir, src_path, dlen);
        fname = slash + 1;
    }

    /* determine base name without .se */
    char base[256] = {0};
    size_t flen = strlen(fname);
    if (flen > 3 && strcmp(fname + flen - 3, ".se") == 0) {
        size_t blen = flen - 3;
        if (blen >= sizeof(base)) blen = sizeof(base) - 1;
        memcpy(base, fname, blen);
    } else {
        strncpy(base, fname, sizeof(base) - 1);
    }

    /* create _secache in the same directory as the source */
    char cache_dir[1024];
    if (dir[0]) {
        snprintf(cache_dir, sizeof(cache_dir), "%s/_secache", dir);
    } else {
        strncpy(cache_dir, "_secache", sizeof(cache_dir) - 1);
    }

    mkdir(cache_dir, 0755);

    /* final path: <dir>/_secache/<base>.sec */
    char *out = (char *)xmalloc(strlen(cache_dir) + strlen(base) + 10);
    sprintf(out, "%s/%s.sec", cache_dir, base);
    return out;
}

/* check if a cached .sec is up to date and valid */
static int is_cache_valid(const char *cache_path, const char *src_path) {
    struct stat s_cache, s_src;
    if (stat(cache_path, &s_cache) != 0) return 0;
    if (stat(src_path, &s_src) != 0) {
        if ((src_path[0] == '/' || src_path[0] == '\\') && stat(src_path + 1, &s_src) == 0) {
            /* valid relative path */
        } else {
            return 0;
        }
    }
    if (s_cache.st_mtime < s_src.st_mtime) return 0;

    FILE *f = fopen(cache_path, "rb");
    if (!f) return 0;
    char magic[5] = {0};
    if (fread(magic, 1, 4, f) != 4) { fclose(f); return 0; }
    uint8_t ver = 0;
    if (fread(&ver, 1, 1, f) != 1) { fclose(f); return 0; }
    fclose(f);
    
    return (strcmp(magic, SEC_MAGIC) == 0 && ver == SEC_VERSION);
}

/* Run source file through interpreter */
static void run_source(const char *path) {
    char *src = read_file(path);
    if (!src) fatal("cannot open file '%s'", path);

    Lexer  *lex  = lexer_new(src);
    Parser *par  = parser_new(lex);
    Node   *prog = parse(par);
    parser_free(par);
    lexer_free(lex);
    free(src);

    Interp *interp = interp_new();
    interp_exec(interp, prog);
    interp_free(interp);
    node_free(prog);
}

/* Compile source file to .sec */
static void compile_source(const char *path) {
    char *src = read_file(path);
    if (!src) fatal("cannot open file '%s'", path);

    Lexer  *lex  = lexer_new(src);
    Parser *par  = parser_new(lex);
    Node   *prog = parse(par);
    parser_free(par);
    lexer_free(lex);
    free(src);

    char *out = make_sec_path(path);
    compile_to_sec(prog, out);
    printf("Compiled '%s' → '%s'\n", path, out);
    free(out);
    node_free(prog);
}

int main(int argc, char *argv[]) {
    pkg_set_args(argc, argv);
    if (argc < 2) { print_usage(); return 0; }

    int trace = 0;
    int start_idx = 1;
    if (strcmp(argv[1], "--trace") == 0) {
        trace = 1;
        start_idx = 2;
    }

    if (argc <= start_idx) { print_usage(); return 0; }
    const char *arg1 = argv[start_idx];

    /* seng help */
    if (strcmp(arg1, "help") == 0 || strcmp(arg1, "--help") == 0
        || strcmp(arg1, "-h") == 0) {
        print_usage();
        return 0;
    }

    /* seng repl */
    if (strcmp(arg1, "repl") == 0) {
        run_repl();
        return 0;
    }

    /* seng compile <file.se> */
    if (strcmp(arg1, "compile") == 0) {
        if (argc < start_idx + 2) fatal("usage: seng compile <file.se>");
        compile_source(argv[start_idx + 1]);
        return 0;
    }

    /* seng run <file.sec> */
    if (strcmp(arg1, "run") == 0) {
        if (argc < start_idx + 2) fatal("usage: seng run <file.sec>");
        vm_run_file(argv[start_idx + 1]);
        return 0;
    }

    /* seng disasm <file.sec> */
    if (strcmp(arg1, "disasm") == 0) {
        if (argc < start_idx + 2) fatal("usage: seng disasm <file.sec>");
        vm_disasm(argv[start_idx + 1]);
        return 0;
    }

    /* seng <file.se>  — run source */
    size_t len = strlen(arg1);
    if (len > 4 && strcmp(arg1 + len - 4, ".sec") == 0) {
        vm_run_file(arg1);
    } else if (len > 3 && strcmp(arg1 + len - 3, ".se") == 0) {
        char *cache = make_sec_path(arg1);
        if (is_cache_valid(cache, arg1)) {
            vm_run_file(cache);
        } else {
            char *src = read_file(arg1);
            if (!src) fatal("cannot open file '%s'", arg1);
            Lexer  *lex  = lexer_new(src);
            Parser *par  = parser_new(lex);
            Node   *prog = parse(par);
            parser_free(par);
            lexer_free(lex);
            free(src);

            Interp *interp = interp_new();
            if (trace) interp_set_trace(interp, 1);
            interp_exec(interp, prog);
            interp_free(interp);
            node_free(prog);
        }
        free(cache);
    } else {
        /* fallback to run_source with trace support */
        char *src = read_file(arg1);
        if (!src) fatal("cannot open file '%s'", arg1);
        Lexer  *lex  = lexer_new(src);
        Parser *par  = parser_new(lex);
        Node   *prog = parse(par);
        parser_free(par);
        lexer_free(lex);
        free(src);

        Interp *interp = interp_new();
        if (trace) interp_set_trace(interp, 1);
        interp_exec(interp, prog);
        interp_free(interp);
        node_free(prog);
    }

    return 0;
}
