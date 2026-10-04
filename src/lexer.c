
#include "lexer.h"

/* ── keyword table ───────────────────────────────────────────── */
static const struct { const char *word; TkType type; } kw_table[] = {
    {"add",     TK_ADD},     {"and",     TK_AND},    {"ask",     TK_ASK},
    {"back",    TK_BACK},    {"blueprint", TK_BLUEPRINT}, {"by",      TK_BY},     {"call",    TK_CALL},
    {"called",  TK_CALLED},  {"create",  TK_CREATE},  {"define",  TK_DEFINE},  {"divided", TK_DIVIDED},{"else",    TK_ELSE},
    {"end",     TK_END},     {"equal",   TK_EQUAL},  {"false",   TK_FALSE},  {"from",    TK_FROM},
    {"for",     TK_FOR},     {"give",    TK_GIVE},   {"greater", TK_GREATER}, {"has",     TK_HAS},    {"hidden",  TK_HIDDEN},
    {"if",      TK_IF},      {"import",  TK_IMPORT}, {"instance", TK_INSTANCE}, {"in",      TK_IN}, {"is",      TK_IS},
    {"item",    TK_ITEM},    {"length",  TK_LENGTH}, {"less",    TK_LESS},
    {"list",    TK_LIST},    {"make",    TK_MAKE},   {"me",      TK_ME},      {"minus",   TK_MINUS},
    {"mod",     TK_MOD},     {"not",     TK_NOT},    {"note",    TK_NOTE},
    {"nothing", TK_NOTHING}, {"of",      TK_OF},     {"or",      TK_OR},
    {"plus",    TK_PLUS},    {"repeat",  TK_REPEAT}, {"result",  TK_RESULT},
    {"say",     TK_SAY},     {"set",     TK_SET},    {"skip",    TK_SKIP},
    {"stop",    TK_STOP},    {"than",    TK_THAN},   {"then",    TK_THEN},
    {"times",   TK_TIMES},   {"to",      TK_TO},     {"true",    TK_TRUE},
    {"while",   TK_WHILE},   {"with",    TK_WITH},   {"try",     TK_TRY},
    {"catch",   TK_CATCH},   {"throw",   TK_THROW},  {"dictionary", TK_DICTIONARY},
    {"each",    TK_EACH},    {"match",   TK_MATCH},  {"case",    TK_CASE},
    {"action",  TK_ACTION},
    {NULL,      TK_EOF}
};

static TkType lookup_kw(const char *word) {
    for (int i = 0; kw_table[i].word; i++)
        if (strcmp(kw_table[i].word, word) == 0)
            return kw_table[i].type;
    return TK_IDENT;
}

/* ── lexer struct ────────────────────────────────────────────── */
struct Lexer {
    const char *src;
    size_t      pos;
    int         line;
    Token       peeked;
    int         has_peeked;
};

Lexer *lexer_new(const char *src) {
    Lexer *l = (Lexer *)xcalloc(1, sizeof(Lexer));
    l->src  = src;
    l->pos  = 0;
    l->line = 1;
    return l;
}

void lexer_free(Lexer *l) {
    free(l);
}

/* helper: skip spaces & tabs (NOT newlines) */
static void skip_spaces(Lexer *l) {
    while (l->src[l->pos] == ' ' || l->src[l->pos] == '\t' || l->src[l->pos] == '\r')
        l->pos++;
}

/* produce one raw token from the source stream */
static Token scan(Lexer *l) {
    skip_spaces(l);
    int   line = l->line;
    char  c    = l->src[l->pos];
    Token t;

    /* ── EOF ─────── */
    if (c == '\0') { t.type = TK_EOF; t.value = NULL; t.line = line; return t; }

    /* ── comment: # ── */
    if (c == '#') {
        while (l->src[l->pos] && l->src[l->pos] != '\n') l->pos++;
        return scan(l);
    }

    /* ── newline ─────── */
    if (c == '\n') {
        l->pos++; l->line++;
        t.type = TK_NEWLINE; t.value = NULL; t.line = line; return t;
    }

    /* ── string literal (supports \" \\ \n \t \r escapes) ─ */
    if (c == '"') {
        l->pos++;
        size_t cap = 64, len = 0;
        char *buf = (char *)xmalloc(cap);
        while (l->src[l->pos] && l->src[l->pos] != '"' && l->src[l->pos] != '\n') {
            char ch = l->src[l->pos++];
            if (ch == '\\' && l->src[l->pos]) {
                char e = l->src[l->pos++];
                switch (e) {
                    case '"':  ch = '"';  break;
                    case '\\': ch = '\\'; break;
                    case 'n':  ch = '\n'; break;
                    case 't':  ch = '\t'; break;
                    case 'r':  ch = '\r'; break;
                    default:   ch = e;   break;
                }
            }
            if (len + 2 > cap) { cap *= 2; buf = (char *)xrealloc(buf, cap); }
            buf[len++] = ch;
        }
        buf[len] = '\0';
        if (l->src[l->pos] == '"') l->pos++;
        t.type = TK_STRING; t.value = buf; t.line = line; return t;
    }

    /* ── number ─── */
    if (isdigit((unsigned char)c)) {
        size_t start = l->pos;
        while (isdigit((unsigned char)l->src[l->pos])) l->pos++;
        if (l->src[l->pos] == '.') {
            l->pos++;
            while (isdigit((unsigned char)l->src[l->pos])) l->pos++;
        }
        t.type = TK_NUMBER; t.value = xstrndup(l->src + start, l->pos - start);
        t.line = line; return t;
    }

    /* ── identifier / keyword ─── */
    if (isalpha((unsigned char)c) || c == '_') {
        size_t start = l->pos;
        while (isalnum((unsigned char)l->src[l->pos]) || l->src[l->pos] == '_')
            l->pos++;
        char  *word = xstrndup(l->src + start, l->pos - start);
        TkType tp   = lookup_kw(word);

        /* 'note' → rest of line is a comment */
        if (tp == TK_NOTE) {
            free(word);
            while (l->src[l->pos] && l->src[l->pos] != '\n') l->pos++;
            return scan(l);
        }
        /* for non-literal keywords, value is not needed */
        if (tp != TK_IDENT && tp != TK_NUMBER && tp != TK_STRING) {
            free(word);
            t.type = tp; t.value = NULL; t.line = line; return t;
        }
        t.type = TK_IDENT; t.value = word; t.line = line; return t;
    }

    /* ── operators ─── */
    l->pos++;
    t.value = NULL; t.line = line;
    switch (c) {
        case '+': t.type = TK_PLUS_OP;  return t;
        case '-': t.type = TK_MINUS_OP; return t;
        case '*': t.type = TK_STAR;     return t;
        case '/': t.type = TK_SLASH;    return t;
        case '%': t.type = TK_PERCENT;  return t;
        case '(': t.type = TK_LPAREN;   return t;
        case ')': t.type = TK_RPAREN;   return t;
        case '[': t.type = TK_LBRACK;   return t;
        case ']': t.type = TK_RBRACK;   return t;
        case '{': t.type = TK_LBRACE;   return t;
        case '}': t.type = TK_RBRACE;   return t;
        case ':': t.type = TK_COLON;    return t;
        case ',': t.type = TK_COMMA;    return t;
        default:
            t.type = TK_ERROR; return t;
    }
}

Token lexer_advance(Lexer *l) {
    if (l->has_peeked) {
        l->has_peeked = 0;
        return l->peeked;
    }
    return scan(l);
}

Token lexer_peek(Lexer *l) {
    if (!l->has_peeked) {
        l->peeked = scan(l);
        l->has_peeked = 1;
    }
    return l->peeked;
}

/* ── debug name ─── */
const char *tk_name(TkType t) {
    static const char *names[] = {
        "NUMBER","STRING","TRUE","FALSE","NOTHING","IDENT",
        "add","and","ask","back","blueprint","by","call","called","create","define","divided",
        "else","end","equal","from","for","each","in","give","greater","has","if","import",
        "instance","is","item","hidden","length","less","list","make","me","minus","mod",
        "not","note","dictionary", "of","or","plus","repeat","result","say","set",
        "skip","stop","than","then","times","to","while","with","try","catch","throw",
        "match","case","action",
        "+","-","*","/","%","(",")","[","]","{","}",":",",",
        "NEWLINE","EOF","ERROR"
    };
    if (t < TK_COUNT) return names[t];
    return "?";
}
