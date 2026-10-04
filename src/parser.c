
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Parser {
    Lexer *lex;
    Token  cur;      /* current (just consumed)  */
    Token  peek;     /* look-ahead               */
    int    had_peek; /* peek is valid            */
};

/* ── helpers ─────────────────────────────────────────────────── */

static Token p_peek(Parser *p) {
    if (!p->had_peek) {
        p->peek     = lexer_advance(p->lex);
        p->had_peek = 1;
    }
    return p->peek;
}

static Token p_advance(Parser *p) {
    if (p->had_peek) {
        p->cur      = p->peek;
        p->had_peek = 0;
    } else {
        p->cur = lexer_advance(p->lex);
    }
    return p->cur;
}

static int p_check(Parser *p, TkType t) {
    return p_peek(p).type == t;
}

static int p_match(Parser *p, TkType t) {
    if (p_check(p, t)) { p_advance(p); return 1; }
    return 0;
}

static Token p_expect(Parser *p, TkType t) {
    Token tk = p_peek(p);
    if (tk.type != t) {
        fatal("line %d: expected '%s' but got '%s'%s%s",
              tk.line, tk_name(t), tk_name(tk.type),
              tk.value ? " " : "", tk.value ? tk.value : "");
    }
    p_advance(p);
    return p->cur;
}

/* skip blank lines */
static void skip_newlines(Parser *p) {
    while (p_check(p, TK_NEWLINE)) p_advance(p);
}

/* consume a statement-terminating newline (or EOF) */
static void eat_newline(Parser *p) {
    if (p_check(p, TK_NEWLINE)) { p_advance(p); return; }
    if (p_check(p, TK_EOF))     return;
    Token tk = p_peek(p);
    fatal("line %d: expected end of line", tk.line);
}

/* forward declarations */
static Node  *parse_stmt    (Parser *p);
static Node  *parse_expr    (Parser *p);
static Node  *parse_cond    (Parser *p);
static NodeList parse_block (Parser *p); /* until end / else / EOF */

/* ── expression parsing ───────────────────────────────────────── */

static Node *parse_primary(Parser *p) {
    Token tk = p_peek(p);
    int   ln = tk.line;

    /* number */
    if (tk.type == TK_NUMBER) {
        p_advance(p);
        Node *n  = node_new(ND_NUMBER, ln);
        n->num   = atof(p->cur.value);
        free(p->cur.value);
        return n;
    }
    /* string */
    if (tk.type == TK_STRING) {
        p_advance(p);
        Node *n  = node_new(ND_STRING, ln);
        n->str   = p->cur.value;   /* ownership transferred */
        return n;
    }
    /* true / false */
    if (tk.type == TK_TRUE)  { p_advance(p); Node *n = node_new(ND_BOOL, ln); n->bool_val = 1; return n; }
    if (tk.type == TK_FALSE) { p_advance(p); Node *n = node_new(ND_BOOL, ln); n->bool_val = 0; return n; }
    /* nothing */
    if (tk.type == TK_NOTHING) { p_advance(p); return node_new(ND_NOTHING, ln); }

    /* me */
    if (tk.type == TK_ME) { p_advance(p); return node_new(ND_ME, ln); }

    /* ( expr ) */
    if (tk.type == TK_LPAREN) {
        p_advance(p);
        Node *n = parse_cond(p);
        p_expect(p, TK_RPAREN);
        return n;
    }

    /* [ list literal ] */
    if (tk.type == TK_LBRACK) {
        p_advance(p);
        Node *n = node_new(ND_LIST_LIT, ln);
        if (!p_check(p, TK_RBRACK)) {
            node_list_push(&n->list_lit, parse_expr(p));
            while (p_match(p, TK_COMMA))
                node_list_push(&n->list_lit, parse_expr(p));
        }
        p_expect(p, TK_RBRACK);
        return n;
    }

    /* { map literal } */
    if (tk.type == TK_LBRACE) {
        p_advance(p);
        Node *n = node_new(ND_MAP_LIT, ln);
        if (!p_check(p, TK_RBRACE)) {
            // key
            node_list_push(&n->map_lit, parse_expr(p));
            p_expect(p, TK_COLON);
            // value
            node_list_push(&n->map_lit, parse_expr(p));
            while (p_match(p, TK_COMMA)) {
                node_list_push(&n->map_lit, parse_expr(p));
                p_expect(p, TK_COLON);
                node_list_push(&n->map_lit, parse_expr(p));
            }
        }
        p_expect(p, TK_RBRACE);
        return n;
    }

    /* action [with p1 [and p2]*] [then] ... end */
    if (tk.type == TK_ACTION) {
        p_advance(p);
        Node *n = node_new(ND_LAMBDA, ln);
        if (p_match(p, TK_WITH)) {
            Token p1 = p_expect(p, TK_IDENT);
            n->lambda.params = (char **)xmalloc(sizeof(char *));
            n->lambda.params[0] = p1.value;
            n->lambda.param_count = 1;
            while (p_match(p, TK_AND)) {
                Token px = p_expect(p, TK_IDENT);
                n->lambda.params = (char **)xrealloc(n->lambda.params,
                    sizeof(char *) * (size_t)(n->lambda.param_count + 1));
                n->lambda.params[n->lambda.param_count++] = px.value;
            }
        }
        p_match(p, TK_THEN);
        eat_newline(p);
        n->lambda.body = parse_block(p);
        p_expect(p, TK_END);
        return n;
    }

    /* length of <list> */
    if (tk.type == TK_LENGTH) {
        p_advance(p);
        p_expect(p, TK_OF);
        Token nm = p_expect(p, TK_IDENT);
        Node *n  = node_new(ND_LIST_LEN, ln);
        n->list_len = nm.value;
        return n;
    }

    /* item <expr> of <list> */
    if (tk.type == TK_ITEM) {
        p_advance(p);
        Node *idx = parse_expr(p);
        char *list_name = NULL;
        if (p_match(p, TK_OF)) {
            Token nm = p_expect(p, TK_IDENT);
            list_name = nm.value;
        } else if (idx->kind == ND_PROP_GET) {
            /* Ambiguity: 'item name of me of list' was parsed as idx=(name of (me of list)).
               We need to extract the rightmost identifier as the list name. */
            Node *parent = NULL;
            Node *curr = idx;
            while (curr->prop_get.obj->kind == ND_PROP_GET) {
                parent = curr;
                curr = curr->prop_get.obj;
            }
            if (curr->prop_get.obj->kind == ND_IDENT) {
                list_name = curr->prop_get.obj->str;
                if (parent) {
                    Node *bottom_idx = node_new(ND_IDENT, curr->line);
                    bottom_idx->str = curr->prop_get.name;
                    parent->prop_get.obj = bottom_idx;
                } else {
                    Node *real_idx = node_new(ND_IDENT, idx->line);
                    real_idx->str = idx->prop_get.name;
                    idx = real_idx;
                }
            } else {
                fatal("line %d: expected 'of' after item index", ln);
            }
        } else {
            fatal("line %d: expected 'of' after item index", ln);
        }
        Node *n = node_new(ND_LIST_GET, ln);
        n->list_get.index = idx;
        n->list_get.name  = list_name;
        return n;
    }

    /* result of <func> [with <arg> [and <arg>]*] */
    if (tk.type == TK_RESULT) {
        p_advance(p);
        p_expect(p, TK_OF);
        Token nm = p_expect(p, TK_IDENT);
        Node *obj = NULL;
        if (p_match(p, TK_OF)) {
            obj = parse_primary(p);
        }
        Node *n  = node_new(ND_CALL_EXPR, ln);
        n->call.name = nm.value;
        n->call.obj  = obj;
        if (p_match(p, TK_WITH)) {
            node_list_push(&n->call.args, parse_expr(p));
            while (p_match(p, TK_AND))
                node_list_push(&n->call.args, parse_expr(p));
        }
        return n;
    }

    /* identifier [of <obj>] */
    if (tk.type == TK_IDENT) {
        p_advance(p);
        char *nm = p->cur.value;
        if (p_match(p, TK_OF)) {
            Node *obj = parse_primary(p);
            Node *n   = node_new(ND_PROP_GET, ln);
            n->prop_get.name = nm;
            n->prop_get.obj  = obj;
            return n;
        }
        Node *n = node_new(ND_IDENT, ln);
        n->str  = nm;
        return n;
    }

    fatal("line %d: unexpected token '%s'%s%s in expression",
          ln, tk_name(tk.type),
          tk.value ? " '" : "", tk.value ? tk.value : "");
    return NULL; /* unreachable */
}

static Node *parse_unary(Parser *p) {
    Token tk = p_peek(p);
    if (tk.type == TK_MINUS_OP) {
        p_advance(p);
        Node *n = node_new(ND_NEGATE, tk.line);
        n->unary = parse_unary(p);
        return n;
    }
    return parse_primary(p);
}

/* term: * / % (also keyword 'times' 'divided by' 'mod') */
static Node *parse_term(Parser *p) {
    Node *left = parse_unary(p);
    while (1) {
        Token tk = p_peek(p);
        BinOp op;
        if      (tk.type == TK_STAR || tk.type == TK_TIMES) op = BINOP_MUL;
        else if (tk.type == TK_SLASH)                             op = BINOP_DIV;
        else if (tk.type == TK_PERCENT || tk.type == TK_MOD)     op = BINOP_MOD;
        else if (tk.type == TK_DIVIDED) {
            p_advance(p); p_expect(p, TK_BY);
            Node *right = parse_unary(p);
            Node *n = node_new(ND_BINOP, tk.line);
            n->binop.op = BINOP_DIV; n->binop.left = left; n->binop.right = right;
            left = n; continue;
        }
        else break;
        p_advance(p);
        Node *right = parse_unary(p);
        Node *n = node_new(ND_BINOP, tk.line);
        n->binop.op = op; n->binop.left = left; n->binop.right = right;
        left = n;
    }
    return left;
}

/* expr: + - (also keyword 'plus' 'minus') */
static Node *parse_expr(Parser *p) {
    Node *left = parse_term(p);
    while (1) {
        Token tk = p_peek(p);
        BinOp op;
        if      (tk.type == TK_PLUS_OP  || tk.type == TK_PLUS)  op = BINOP_ADD;
        else if (tk.type == TK_MINUS_OP || tk.type == TK_MINUS)  op = BINOP_SUB;
        else break;
        p_advance(p);
        Node *right = parse_term(p);
        Node *n = node_new(ND_BINOP, tk.line);
        n->binop.op = op; n->binop.left = left; n->binop.right = right;
        left = n;
    }
    return left;
}

/* comparison: expr [is cmp_op expr] */
static Node *parse_comparison(Parser *p) {
    Node *left = parse_expr(p);
    if (!p_check(p, TK_IS)) return left;
    p_advance(p);   /* consume 'is' */
    Token tk = p_peek(p);
    int   ln = tk.line;
    CmpOp op;

    if (tk.type == TK_EQUAL) {                       /* is equal to */
        p_advance(p); p_expect(p, TK_TO);            op = CMP_EQ;
    } else if (tk.type == TK_NOT) {                  /* is not equal to */
        p_advance(p); p_expect(p, TK_EQUAL); p_expect(p, TK_TO); op = CMP_NEQ;
    } else if (tk.type == TK_GREATER) {              /* is greater than [or equal to] */
        p_advance(p); p_expect(p, TK_THAN);
        if (p_match(p, TK_OR)) { p_expect(p, TK_EQUAL); p_expect(p, TK_TO); op = CMP_GTE; }
        else op = CMP_GT;
    } else if (tk.type == TK_LESS) {                 /* is less than [or equal to] */
        p_advance(p); p_expect(p, TK_THAN);
        if (p_match(p, TK_OR)) { p_expect(p, TK_EQUAL); p_expect(p, TK_TO); op = CMP_LTE; }
        else op = CMP_LT;
    } else {
        fatal("line %d: expected comparison operator after 'is'", ln);
        return NULL;
    }
    Node *right = parse_expr(p);
    Node *n = node_new(ND_CMP, ln);
    n->cmp.op = op; n->cmp.left = left; n->cmp.right = right;
    return n;
}

/* not */
static Node *parse_not(Parser *p) {
    Token tk = p_peek(p);
    if (tk.type == TK_NOT) {
        p_advance(p);
        Node *n = node_new(ND_NOT, tk.line);
        n->unary = parse_not(p);
        return n;
    }
    return parse_comparison(p);
}

/* and / or  */
static Node *parse_cond(Parser *p) {
    Node *left = parse_not(p);
    while (1) {
        Token tk = p_peek(p);
        NodeKind kind;
        if      (tk.type == TK_AND) kind = ND_AND;
        else if (tk.type == TK_OR)  kind = ND_OR;
        else break;
        p_advance(p);
        Node *right = parse_not(p);
        Node *n = node_new(kind, tk.line);
        n->logical.left = left; n->logical.right = right;
        left = n;
    }
    return left;
}

/* ── statement parsing ───────────────────────────────────────── */

/* block: reads statements until end / else / EOF */
static NodeList parse_block(Parser *p) {
    NodeList bl = {0};
    skip_newlines(p);
    while (1) {
        TkType t = p_peek(p).type;
        if (t == TK_END || t == TK_ELSE || t == TK_CASE || t == TK_EOF) break;
        Node *s = parse_stmt(p);
        if (s) node_list_push(&bl, s);
        skip_newlines(p);
    }
    return bl;
}

static Node *parse_stmt(Parser *p) {
    skip_newlines(p);
    Token tk = p_peek(p);
    int   ln = tk.line;

    /* ── set ── */
    if (tk.type == TK_SET) {
        p_advance(p);
        
        /* set item <idx> of <name> to <val> */
        if (p_match(p, TK_ITEM)) {
            Node *idx = parse_expr(p);
            p_expect(p, TK_OF);
            Token nm = p_expect(p, TK_IDENT);
            p_expect(p, TK_TO);
            Node *val = parse_expr(p);
            eat_newline(p);
            Node *n = node_new(ND_SET_ITEM, ln);
            n->set_item.name  = nm.value;
            n->set_item.index = idx;
            n->set_item.val   = val;
            return n;
        }

        /* set <ident> [of <obj>] to <expr> */
        Token nm = p_expect(p, TK_IDENT);
        if (p_match(p, TK_OF)) {
            Node *obj = parse_primary(p);
            p_expect(p, TK_TO);
            Node *expr = parse_cond(p);
            eat_newline(p);
            Node *n = node_new(ND_PROP_SET, ln);
            n->prop_set.name = nm.value;
            n->prop_set.obj  = obj;
            n->prop_set.expr = expr;
            return n;
        } else {
            p_expect(p, TK_TO);
            Node *expr = parse_cond(p);
            eat_newline(p);
            Node *n = node_new(ND_SET, ln);
            n->set.name = nm.value; n->set.expr = expr;
            return n;
        }
    }

    /* ── say ── */
    if (tk.type == TK_SAY) {
        p_advance(p);
        Node *expr = parse_cond(p);
        eat_newline(p);
        Node *n = node_new(ND_SAY, ln);
        n->say = expr;
        return n;
    }

    /* ── ask ── */
    if (tk.type == TK_ASK) {
        p_advance(p);
        Token nm     = p_expect(p, TK_IDENT);
        p_expect(p, TK_FOR);
        Token prompt = p_expect(p, TK_STRING);
        eat_newline(p);
        Node *n = node_new(ND_ASK, ln);
        n->ask.name   = nm.value;
        n->ask.prompt = prompt.value;
        return n;
    }

    /* ── if ── */
    if (tk.type == TK_IF) {
        p_advance(p);
        Node *n = node_new(ND_IF, ln);
        n->if_stmt.count  = 0;
        n->if_stmt.blocks = NULL;

        /* first condition */
        Node *cond = parse_cond(p);
        p_expect(p, TK_THEN);
        eat_newline(p);
        NodeList body = parse_block(p);

        node_list_push(&n->if_stmt.conds, cond);
        n->if_stmt.count++;
        n->if_stmt.blocks = (NodeList *)xrealloc(n->if_stmt.blocks,
                            sizeof(NodeList) * (size_t)n->if_stmt.count);
        n->if_stmt.blocks[n->if_stmt.count - 1] = body;

        /* else if / else */
        while (p_check(p, TK_ELSE)) {
            p_advance(p);
            if (p_check(p, TK_IF)) {
                p_advance(p);
                Node *ec = parse_cond(p);
                p_expect(p, TK_THEN);
                eat_newline(p);
                NodeList eb = parse_block(p);
                node_list_push(&n->if_stmt.conds, ec);
                n->if_stmt.count++;
                n->if_stmt.blocks = (NodeList *)xrealloc(n->if_stmt.blocks,
                                    sizeof(NodeList) * (size_t)n->if_stmt.count);
                n->if_stmt.blocks[n->if_stmt.count - 1] = eb;
            } else {
                /* plain else — NULL sentinel in conds */
                eat_newline(p);
                NodeList eb = parse_block(p);
                node_list_push(&n->if_stmt.conds, NULL);
                n->if_stmt.count++;
                n->if_stmt.blocks = (NodeList *)xrealloc(n->if_stmt.blocks,
                                    sizeof(NodeList) * (size_t)n->if_stmt.count);
                n->if_stmt.blocks[n->if_stmt.count - 1] = eb;
            }
        }
        p_expect(p, TK_END);
        eat_newline(p);
        return n;
    }

    /* ── match ── */
    if (tk.type == TK_MATCH) {
        p_advance(p);
        Node *target = parse_expr(p);
        if (p_check(p, TK_WITH)) p_advance(p);
        else if (p_check(p, TK_THEN)) p_advance(p);
        eat_newline(p);
        skip_newlines(p);

        Node *n = node_new(ND_MATCH, ln);
        n->match_stmt.expr = target;

        while (p_check(p, TK_CASE)) {
            p_advance(p);
            int cln = p->cur.line;
            Node *pat = parse_expr(p);
            if (p_check(p, TK_THEN)) p_advance(p);
            eat_newline(p);
            NodeList cbody = parse_block(p);
            Node *cnode = node_new(ND_CASE, cln);
            cnode->case_stmt.pattern = pat;
            cnode->case_stmt.body = cbody;
            node_list_push(&n->match_stmt.cases, cnode);
            skip_newlines(p);
        }
        if (p_check(p, TK_ELSE)) {
            p_advance(p);
            eat_newline(p);
            n->match_stmt.default_body = parse_block(p);
            skip_newlines(p);
        }
        p_expect(p, TK_END);
        eat_newline(p);
        return n;
    }

    /* ── repeat ── */
    if (tk.type == TK_REPEAT) {
        p_advance(p);
        Node *cnt = parse_primary(p);  /* 'repeat 5 times' — just a simple value */
        p_expect(p, TK_TIMES);
        eat_newline(p);
        NodeList body = parse_block(p);
        p_expect(p, TK_END);
        eat_newline(p);
        Node *n = node_new(ND_REPEAT, ln);
        n->repeat.count = cnt; n->repeat.body = body;
        return n;
    }

    /* ── for each ── */
    if (tk.type == TK_FOR) {
        p_advance(p);
        p_expect(p, TK_EACH);
        Token var = p_expect(p, TK_IDENT);
        p_expect(p, TK_IN);
        Node *collection = parse_expr(p);
        p_expect(p, TK_THEN);
        eat_newline(p);
        NodeList body = parse_block(p);
        p_expect(p, TK_END);
        eat_newline(p);
        Node *n = node_new(ND_FOR_EACH, ln);
        n->for_each.var_name = var.value;
        n->for_each.collection = collection;
        n->for_each.body = body;
        return n;
    }

    /* ── while ── */
    if (tk.type == TK_WHILE) {
        p_advance(p);
        Node *cond = parse_cond(p);
        eat_newline(p);
        NodeList body = parse_block(p);
        p_expect(p, TK_END);
        eat_newline(p);
        Node *n = node_new(ND_WHILE, ln);
        n->while_stmt.cond = cond; n->while_stmt.body = body;
        return n;
    }

    /* ── define ── */
    if (tk.type == TK_DEFINE) {
        p_advance(p);
        Token nm = p_expect(p, TK_IDENT);
        Node *n  = node_new(ND_DEFINE, ln);
        n->define.name = nm.value;
        if (p_match(p, TK_WITH)) {
            Token p1 = p_expect(p, TK_IDENT);
            n->define.params = (char **)xmalloc(sizeof(char *));
            n->define.params[0] = p1.value;
            n->define.param_count = 1;
            while (p_match(p, TK_AND)) {
                Token px = p_expect(p, TK_IDENT);
                n->define.params = (char **)xrealloc(n->define.params,
                    sizeof(char *) * (size_t)(n->define.param_count + 1));
                n->define.params[n->define.param_count++] = px.value;
            }
        }
        eat_newline(p);
        n->define.body = parse_block(p);
        p_expect(p, TK_END);
        eat_newline(p);
        return n;
    }

    /* ── call (statement) ── */
    if (tk.type == TK_CALL) {
        p_advance(p);
        Token nm = p_expect(p, TK_IDENT);
        Node *obj = NULL;
        if (p_match(p, TK_OF)) {
            obj = parse_primary(p);
        }
        Node *n  = node_new(ND_CALL_STMT, ln);
        n->call.name = nm.value;
        n->call.obj  = obj;
        if (p_match(p, TK_WITH)) {
            node_list_push(&n->call.args, parse_expr(p));
            while (p_match(p, TK_AND))
                node_list_push(&n->call.args, parse_expr(p));
        }
        eat_newline(p);
        return n;
    }

    /* ── give back ─ */
    if (tk.type == TK_GIVE) {
        p_advance(p);
        p_expect(p, TK_BACK);
        Node *val = parse_cond(p);
        eat_newline(p);
        Node *n = node_new(ND_RETURN, ln);
        n->ret = val;
        return n;
    }

    /* ── make list/dictionary ── */
    if (tk.type == TK_MAKE) {
        p_advance(p);
        if (p_match(p, TK_LIST)) {
            Token nm = p_expect(p, TK_IDENT);
            eat_newline(p);
            Node *n = node_new(ND_MAKE_LIST, ln);
            n->list_name = nm.value;
            return n;
        } else if (p_match(p, TK_DICTIONARY)) {
            Token nm = p_expect(p, TK_IDENT);
            eat_newline(p);
            Node *n = node_new(ND_MAKE_MAP, ln);
            n->map_name = nm.value;
            return n;
        }
        fatal("line %d: expected 'list' or 'dictionary' after 'make'", ln);
    }

    /* ── add <val> to <list> ── */
    if (tk.type == TK_ADD) {
        p_advance(p);
        Node *val = parse_expr(p);
        p_expect(p, TK_TO);
        Token nm  = p_expect(p, TK_IDENT);
        eat_newline(p);
        Node *n = node_new(ND_ADD_LIST, ln);
        n->add_list.val  = val;
        n->add_list.name = nm.value;
        return n;
    }

    /* ── create ── */
    if (tk.type == TK_CREATE) {
        p_advance(p);
        if (p_match(p, TK_BLUEPRINT)) {
            Token nm = p_expect(p, TK_IDENT);
            char *parent = NULL;
            if (p_match(p, TK_FROM)) {
                Token ptk = p_expect(p, TK_IDENT);
                parent = ptk.value;
            }
            eat_newline(p);
            Node *n = node_new(ND_CLASS, ln);
            n->klass.name = nm.value;
            n->klass.parent_name = parent;
            skip_newlines(p);
            while (!p_check(p, TK_END) && !p_check(p, TK_EOF)) {
                int hidden = p_match(p, TK_HIDDEN);
                if (p_match(p, TK_HAS)) {
                    Token f = p_expect(p, TK_IDENT);
                    n->klass.fields = (char **)xrealloc(n->klass.fields,
                        sizeof(char *) * (size_t)(n->klass.field_count + 1));
                    n->klass.field_hidden = (int *)xrealloc(n->klass.field_hidden,
                        sizeof(int) * (size_t)(n->klass.field_count + 1));
                    n->klass.fields[n->klass.field_count] = f.value;
                    n->klass.field_hidden[n->klass.field_count] = hidden;
                    n->klass.field_count++;
                    eat_newline(p);
                } else if (p_check(p, TK_DEFINE)) {
                    node_list_push(&n->klass.methods, parse_stmt(p));
                    n->klass.method_hidden = (int *)xrealloc(n->klass.method_hidden,
                        sizeof(int) * (size_t)n->klass.methods.count);
                    n->klass.method_hidden[n->klass.methods.count - 1] = hidden;
                } else if (p_check(p, TK_NEWLINE)) {
                    p_advance(p);
                } else {
                    fatal("line %d: expected 'has' or 'define' in blueprint", p_peek(p).line);
                }
                skip_newlines(p);
            }
            p_expect(p, TK_END);
            eat_newline(p);
            return n;
        } else if (p_match(p, TK_INSTANCE)) {
            p_expect(p, TK_OF);
            Token cls = p_expect(p, TK_IDENT);
            p_expect(p, TK_CALLED);
            Token nm = p_expect(p, TK_IDENT);
            Node *n = node_new(ND_NEW, ln);
            n->instantiate.class_name = cls.value;
            n->instantiate.instance_name = nm.value;
            if (p_match(p, TK_WITH)) {
                node_list_push(&n->instantiate.args, parse_expr(p));
                while (p_match(p, TK_AND))
                    node_list_push(&n->instantiate.args, parse_expr(p));
            }
            eat_newline(p);
            return n;
        }
    }

    /* ── import ── */
    if (tk.type == TK_IMPORT) {
        p_advance(p);
        if (p_check(p, TK_STRING)) {
            /* import "file.se" */
            Token path = p_expect(p, TK_STRING);
            eat_newline(p);
            Node *n = node_new(ND_IMPORT, ln);
            n->import_path = path.value;
            return n;
        } else {
            /* import math  (built-in package) */
            Token nm = p_expect(p, TK_IDENT);
            eat_newline(p);
            Node *n = node_new(ND_IMPORT_PKG, ln);
            n->str = nm.value;
            return n;
        }
    }

    /* ── stop / skip ── */
    if (tk.type == TK_STOP)  { p_advance(p); eat_newline(p); return node_new(ND_STOP, ln); }
    if (tk.type == TK_SKIP)  { p_advance(p); eat_newline(p); return node_new(ND_SKIP, ln); }

    /* ── try / catch ── */
    if (tk.type == TK_TRY) {
        p_advance(p);
        eat_newline(p);
        Node *n = node_new(ND_TRY, ln);
        while (!p_check(p, TK_CATCH) && !p_check(p, TK_EOF)) {
            node_list_push(&n->try_catch.try_body, parse_stmt(p));
        }
        p_expect(p, TK_CATCH);
        if (p_check(p, TK_IDENT)) {
            Token v = p_expect(p, TK_IDENT);
            n->try_catch.catch_var = v.value;
        }
        eat_newline(p);
        while (!p_check(p, TK_END) && !p_check(p, TK_EOF)) {
            node_list_push(&n->try_catch.catch_body, parse_stmt(p));
        }
        p_expect(p, TK_END);
        eat_newline(p);
        return n;
    }

    /* ── throw ── */
    if (tk.type == TK_THROW) {
        p_advance(p);
        Node *n = node_new(ND_THROW, ln);
        n->throw_err.expr = parse_expr(p);
        eat_newline(p);
        return n;
    }

    /* ── blank line ── */
    if (tk.type == TK_NEWLINE) { p_advance(p); return NULL; }
    if (tk.type == TK_EOF)     return NULL;

    fatal("line %d: unexpected token '%s'%s%s",
          ln, tk_name(tk.type),
          tk.value ? " '" : "", tk.value ? tk.value : "");
    return NULL;
}

/* ── public API ──────────────────────────────────────────────── */

Parser *parser_new(Lexer *lex) {
    Parser *p = (Parser *)xcalloc(1, sizeof(Parser));
    p->lex = lex;
    return p;
}

void parser_free(Parser *p) {
    free(p);
}

Node *parse(Parser *p) {
    Node *root = node_new(ND_PROGRAM, 1);
    skip_newlines(p);
    while (!p_check(p, TK_EOF)) {
        Node *s = parse_stmt(p);
        if (s) node_list_push(&root->program, s);
        skip_newlines(p);
    }
    return root;
}
