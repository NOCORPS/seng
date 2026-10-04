
#include "interp.h"
#include "lexer.h"
#include "parser.h"
#include "packages.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ── control-flow signals ────────────────────────────────────── */
typedef enum { SIG_NONE, SIG_RETURN, SIG_STOP, SIG_SKIP, SIG_ERROR } Signal;

struct Interp {
    Env   *globals;
    Signal signal;
    Value *ret_val;
    Value *err_val;      /* value thrown */
    Node **imported;     /* imported ASTs kept alive so body_ref stays valid */
    int    import_count;
    int    import_cap;
    int    trace;
};

/* forward decls */
static Value  *eval (Interp *in, Env *e, Node *n);
static Signal  exec (Interp *in, Env *e, Node *n);
static Signal  exec_block(Interp *in, Env *e, NodeList *bl);

/* ── helpers ─────────────────────────────────────────────────── */

static double num_of(Value *v, int line) {
    if (!v || v->type != VAL_NUM)
        fatal("line %d: expected a number", line);
    return v->num;
}

static char *concat_vals(Value *a, Value *b, int line) {
    (void)line;
    char *sa = val_to_string(a);
    char *sb = val_to_string(b);
    char *r  = (char *)xmalloc(strlen(sa) + strlen(sb) + 1);
    strcpy(r, sa); strcat(r, sb);
    free(sa); free(sb);
    return r;
}

static int val_equal(Value *a, Value *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    if (a->type != b->type) return 0;
    switch (a->type) {
        case VAL_NUM:    return a->num == b->num;
        case VAL_STR:    return strcmp(a->str, b->str) == 0;
        case VAL_BOOL:   return a->bool_val == b->bool_val;
        case VAL_NULL:   return 1;
        default:         return a == b;
    }
}

static SengFunc *make_func(Node *n) {
    SengFunc *fn = (SengFunc *)xcalloc(1, sizeof(SengFunc));
    fn->name        = xstrdup(n->define.name);
    fn->param_count = n->define.param_count;
    fn->params      = (char **)xmalloc(sizeof(char *) *
                      (size_t)(fn->param_count ? fn->param_count : 1));
    for (int i = 0; i < fn->param_count; i++)
        fn->params[i] = xstrdup(n->define.params[i]);
    fn->body_ref = &n->define.body;
    return fn;
}

static Value *call_func_vals(Interp *in, Value *fv, Value **args, int argc, Value *me_val, int line) {
    if (!fv || (fv->type != VAL_FUNC && fv->type != VAL_NATIVE))
        fatal("line %d: value is not a function", line);

    if (fv->type == VAL_NATIVE) {
        SengNative *nat = fv->native;
        if (nat->arity >= 0 && argc != nat->arity)
            fatal("line %d: '%s' expects %d argument(s), got %d", line, nat->name, nat->arity, argc);
        return nat->fn(args, argc);
    }

    SengFunc *fn = fv->func;
    if (argc != fn->param_count)
        fatal("line %d: '%s' expects %d argument(s), got %d", line, fn->name, fn->param_count, argc);

    Env *fenv = env_new(in->globals);
    if (me_val) env_set(fenv, "me", me_val);
    for (int i = 0; i < fn->param_count; i++) {
        env_set(fenv, fn->params[i], args[i]);
    }

    NodeList *body = (NodeList *)fn->body_ref;
    exec_block(in, fenv, body);
    env_free(fenv);

    if (in->signal == SIG_ERROR) return val_null();

    Value *rv = in->ret_val ? in->ret_val : val_null();
    in->ret_val = NULL;
    in->signal  = SIG_NONE;
    return rv;
}

static Value *call_func(Interp *in, Env *e, Value *fv, NodeList *arg_nodes, Value *me_val, int line) {
    int argc = arg_nodes->count;
    Value **args = (Value **)xmalloc(sizeof(Value *) * (size_t)(argc > 0 ? argc : 1));
    for (int i = 0; i < argc; i++) args[i] = eval(in, e, arg_nodes->items[i]);
    Value *rv = call_func_vals(in, fv, args, argc, me_val, line);
    for (int i = 0; i < argc; i++) val_deref(args[i]);
    free(args);
    return rv;
}

/* ── eval ───────────────────────────────────────────────────── */

static Value *eval(Interp *in, Env *e, Node *n) {
    if (!n) return val_null();
    switch (n->kind) {
        case ND_NUMBER:  return val_num(n->num);
        case ND_BOOL:    return val_bool(n->bool_val);
        case ND_NOTHING: return val_null();
        case ND_STRING:  return val_str(n->str);

        case ND_IDENT: {
            Value *v = env_get(e, n->str);
            if (!v) fatal("line %d: '%s' is not defined", n->line, n->str);
            return val_copy(v);
        }

        case ND_NEGATE: {
            Value *v = eval(in, e, n->unary);
            double d = num_of(v, n->line);
            val_deref(v);
            return val_num(-d);
        }

        case ND_NOT: {
            Value *v = eval(in, e, n->unary);
            int    t = val_truthy(v);
            val_deref(v);
            return val_bool(!t);
        }

        case ND_BINOP: {
            Value *l = eval(in, e, n->binop.left);
            Value *r = eval(in, e, n->binop.right);
            Value *res;
            /* string concat with + */
            if (n->binop.op == BINOP_ADD &&
                (l->type == VAL_STR || r->type == VAL_STR)) {
                char *s = concat_vals(l, r, n->line);
                res = val_str(s); free(s);
            } else {
                double a = num_of(l, n->line);
                double b = num_of(r, n->line);
                switch (n->binop.op) {
                    case BINOP_ADD: res = val_num(a + b); break;
                    case BINOP_SUB: res = val_num(a - b); break;
                    case BINOP_MUL: res = val_num(a * b); break;
                    case BINOP_DIV:
                        if (b == 0) fatal("line %d: division by zero", n->line);
                        res = val_num(a / b); break;
                    case BINOP_MOD:
                        if (b == 0) fatal("line %d: modulo by zero", n->line);
                        res = val_num(fmod(a, b)); break;
                    default: res = val_null();
                }
            }
            val_deref(l); val_deref(r);
            return res;
        }

        case ND_CMP: {
            Value *l = eval(in, e, n->cmp.left);
            Value *r = eval(in, e, n->cmp.right);
            int    result;
            /* string comparisons */
            if (l->type == VAL_STR && r->type == VAL_STR) {
                int c = strcmp(l->str, r->str);
                switch (n->cmp.op) {
                    case CMP_EQ:  result = (c == 0); break;
                    case CMP_NEQ: result = (c != 0); break;
                    case CMP_GT:  result = (c  > 0); break;
                    case CMP_LT:  result = (c  < 0); break;
                    case CMP_GTE: result = (c >= 0); break;
                    case CMP_LTE: result = (c <= 0); break;
                    default:      result = 0;
                }
            } else if (l->type == VAL_BOOL && r->type == VAL_BOOL) {
                switch (n->cmp.op) {
                    case CMP_EQ:  result = (l->bool_val == r->bool_val); break;
                    case CMP_NEQ: result = (l->bool_val != r->bool_val); break;
                    default:      result = 0;
                }
            } else if (l->type == VAL_NULL || r->type == VAL_NULL) {
                result = (n->cmp.op == CMP_EQ)
                         ? (l->type == VAL_NULL && r->type == VAL_NULL)
                         : !(l->type == VAL_NULL && r->type == VAL_NULL);
            } else {
                double a = num_of(l, n->line);
                double b = num_of(r, n->line);
                switch (n->cmp.op) {
                    case CMP_EQ:  result = (a == b); break;
                    case CMP_NEQ: result = (a != b); break;
                    case CMP_GT:  result = (a  > b); break;
                    case CMP_LT:  result = (a  < b); break;
                    case CMP_GTE: result = (a >= b); break;
                    case CMP_LTE: result = (a <= b); break;
                    default:      result = 0;
                }
            }
            val_deref(l); val_deref(r);
            return val_bool(result);
        }

        case ND_AND: {
            Value *l = eval(in, e, n->logical.left);
            int    t = val_truthy(l); val_deref(l);
            if (!t) return val_bool(0);
            Value *r = eval(in, e, n->logical.right);
            int    u = val_truthy(r); val_deref(r);
            return val_bool(u);
        }

        case ND_OR: {
            Value *l = eval(in, e, n->logical.left);
            int    t = val_truthy(l); val_deref(l);
            if (t)  return val_bool(1);
            Value *r = eval(in, e, n->logical.right);
            int    u = val_truthy(r); val_deref(r);
            return val_bool(u);
        }

        case ND_LIST_LEN: {
            Value *v = env_get(e, n->list_len);
            if (!v || v->type != VAL_LIST)
                fatal("line %d: '%s' is not a list", n->line, n->list_len);
            return val_num((double)v->list->count);
        }

        case ND_LIST_GET: {
            Value *v = env_get(e, n->list_get.name);
            if (!v) fatal("line %d: undefined collection '%s'", n->line, n->list_get.name);
            Value *idx_v = eval(in, e, n->list_get.index);
            if (v->type == VAL_LIST) {
                int idx = (int)num_of(idx_v, n->line) - 1; /* 1-based */
                val_deref(idx_v);
                if (idx < 0 || idx >= v->list->count)
                    fatal("line %d: list index %d out of range (size %d)",
                          n->line, idx + 1, v->list->count);
                return val_copy(v->list->items[idx]);
            } else if (v->type == VAL_MAP) {
                if (idx_v->type != VAL_STR) fatal("line %d: map key must be a string", n->line);
                Value *res = val_null();
                for (int i = 0; i < v->map->count; i++) {
                    if (strcmp(v->map->keys[i], idx_v->str) == 0) {
                        res = val_copy(v->map->values[i]); break;
                    }
                }
                val_deref(idx_v);
                return res;
            } else {
                fatal("line %d: '%s' is not a list or dictionary", n->line, n->list_get.name);
                return val_null();
            }
        }

        case ND_PROP_GET: {
            Value *obj = eval(in, e, n->prop_get.obj);
            if (!obj || obj->type != VAL_INSTANCE)
                fatal("line %d: expected an instance before 'of'", n->line);
            int idx = find_field(obj->instance->klass, n->prop_get.name);
            if (idx < 0) fatal("line %d: instance of '%s' has no field '%s'",
                               n->line, obj->instance->klass->name, n->prop_get.name);

            if (obj->instance->klass->field_hidden[idx]) {
                Value *me = env_get(e, "me");
                if (!me || me->type != VAL_INSTANCE || me->instance != obj->instance)
                    fatal("line %d: cannot access hidden field '%s' outside of its blueprint",
                          n->line, n->prop_get.name);
            }

            Value *res = val_copy(obj->instance->fields[idx]);
            val_deref(obj);
            return res;
        }

        case ND_ME: {
            Value *v = env_get(e, "me");
            if (!v) fatal("line %d: 'me' can only be used inside a blueprint method", n->line);
            return val_copy(v);
        }

        case ND_LIST_LIT: {
            Value *l = val_list();
            for (int i = 0; i < n->list_lit.count; i++) {
                Value *v = eval(in, e, n->list_lit.items[i]);
                list_push(l, v);
            }
            return l;
        }

        case ND_MAP_LIT: {
            Value *m = val_map();
            for (int i = 0; i < n->map_lit.count; i += 2) {
                Value *k = eval(in, e, n->map_lit.items[i]);
                Value *v = eval(in, e, n->map_lit.items[i+1]);
                if (k->type != VAL_STR) fatal("line %d: map literal key must be a string", n->line);
                map_set(m, k->str, v);
                val_deref(k);
            }
            return m;
        }

        case ND_LAMBDA: {
            SengFunc *fn = (SengFunc *)xcalloc(1, sizeof(SengFunc));
            fn->name        = xstrdup("<action>");
            fn->param_count = n->lambda.param_count;
            fn->params      = (char **)xmalloc(sizeof(char *) *
                              (size_t)(fn->param_count ? fn->param_count : 1));
            for (int i = 0; i < fn->param_count; i++)
                fn->params[i] = xstrdup(n->lambda.params[i]);
            fn->body_ref = &n->lambda.body;
            return val_func(fn);
        }

        case ND_CALL_EXPR: {
            if (!n->call.obj && strcmp(n->call.name, "map") == 0 && n->call.args.count == 2) {
                Value *lst = eval(in, e, n->call.args.items[0]);
                Value *fn  = eval(in, e, n->call.args.items[1]);
                if (!lst || lst->type != VAL_LIST) fatal("line %d: 'map' first argument must be a list", n->line);
                Value *res = val_list();
                for (int i = 0; i < lst->list->count; i++) {
                    Value *arg = lst->list->items[i];
                    Value *out = call_func_vals(in, fn, &arg, 1, NULL, n->line);
                    list_push(res, out);
                }
                val_deref(lst); val_deref(fn);
                return res;
            }
            if (!n->call.obj && strcmp(n->call.name, "filter") == 0 && n->call.args.count == 2) {
                Value *lst = eval(in, e, n->call.args.items[0]);
                Value *fn  = eval(in, e, n->call.args.items[1]);
                if (!lst || lst->type != VAL_LIST) fatal("line %d: 'filter' first argument must be a list", n->line);
                Value *res = val_list();
                for (int i = 0; i < lst->list->count; i++) {
                    Value *arg = lst->list->items[i];
                    Value *out = call_func_vals(in, fn, &arg, 1, NULL, n->line);
                    if (val_truthy(out)) list_push(res, val_copy(arg));
                    val_deref(out);
                }
                val_deref(lst); val_deref(fn);
                return res;
            }
            if (!n->call.obj && strcmp(n->call.name, "reduce") == 0 && n->call.args.count == 3) {
                Value *lst = eval(in, e, n->call.args.items[0]);
                Value *fn  = eval(in, e, n->call.args.items[1]);
                Value *acc = eval(in, e, n->call.args.items[2]);
                if (!lst || lst->type != VAL_LIST) fatal("line %d: 'reduce' first argument must be a list", n->line);
                for (int i = 0; i < lst->list->count; i++) {
                    Value *rargs[2] = { acc, lst->list->items[i] };
                    Value *next_acc = call_func_vals(in, fn, rargs, 2, NULL, n->line);
                    val_deref(acc);
                    acc = next_acc;
                }
                val_deref(lst); val_deref(fn);
                return acc;
            }

            Value *me_val = NULL;
            Value *fv = NULL;
            if (n->call.obj) {
                me_val = eval(in, e, n->call.obj);
                if (!me_val || me_val->type != VAL_INSTANCE)
                    fatal("line %d: expected an instance for method call", n->line);
                
                if (is_method_hidden(me_val->instance->klass, n->call.name)) {
                    Value *cur_me = env_get(e, "me");
                    if (!cur_me || cur_me->type != VAL_INSTANCE || cur_me->instance != me_val->instance)
                        fatal("line %d: cannot call hidden method '%s' outside of its blueprint",
                              n->line, n->call.name);
                }

                fv = find_method(me_val->instance->klass, n->call.name);
                if (!fv) fatal("line %d: blueprint '%s' has no method '%s'",
                               n->line, me_val->instance->klass->name, n->call.name);
            } else {
                fv = env_get(e, n->call.name);
            }
            Value *rv = call_func(in, e, fv, &n->call.args, me_val, n->line);
            if (me_val) val_deref(me_val);
            return rv;
        }

        default:
            fatal("line %d: cannot evaluate node kind %d", n->line, n->kind);
            return val_null();
    }
}

/* ── exec ──────────────────────────────────────────────────── */

static Signal exec_block(Interp *in, Env *e, NodeList *bl) {
    for (int i = 0; i < bl->count; i++) {
        Signal s = exec(in, e, bl->items[i]);
        if (s != SIG_NONE) return s;
        if (in->signal != SIG_NONE) return in->signal;
    }
    return SIG_NONE;
}

static Signal exec(Interp *in, Env *e, Node *n) {
    if (!n) return SIG_NONE;

    if (in->trace && n->kind != ND_PROGRAM) {
        printf("[trace] line %d: %s\n", n->line, node_kind_str(n->kind));
    }

    switch (n->kind) {
        case ND_SET: {
            Value *v = eval(in, e, n->set.expr);
            env_update(e, n->set.name, v);  /* update existing or create new */
            val_deref(v);
            return SIG_NONE;
        }

        case ND_PROP_SET: {
            Value *obj = eval(in, e, n->prop_set.obj);
            if (!obj || obj->type != VAL_INSTANCE)
                fatal("line %d: expected an instance for property set", n->line);
            int idx = find_field(obj->instance->klass, n->prop_set.name);
            if (idx < 0) fatal("line %d: instance of '%s' has no field '%s'",
                               n->line, obj->instance->klass->name, n->prop_set.name);

            if (obj->instance->klass->field_hidden[idx]) {
                Value *me = env_get(e, "me");
                if (!me || me->type != VAL_INSTANCE || me->instance != obj->instance)
                    fatal("line %d: cannot set hidden field '%s' outside of its blueprint",
                          n->line, n->prop_set.name);
            }

            Value *v = eval(in, e, n->prop_set.expr);
            val_deref(obj->instance->fields[idx]);
            obj->instance->fields[idx] = val_copy(v);
            val_deref(v);
            val_deref(obj);
            return SIG_NONE;
        }

        case ND_SAY: {
            Value *v = eval(in, e, n->say);
            val_print(v);
            val_deref(v);
            return SIG_NONE;
        }

        case ND_ASK: {
            printf("%s", n->ask.prompt);
            fflush(stdout);
            char buf[1024] = {0};
            if (fgets(buf, sizeof buf, stdin)) {
                size_t len = strlen(buf);
                if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
            }
            Value *v = val_str(buf);
            env_set(e, n->ask.name, v);
            val_deref(v);
            return SIG_NONE;
        }

        case ND_IF: {
            for (int i = 0; i < n->if_stmt.count; i++) {
                Node *cond = n->if_stmt.conds.items[i];
                int   take;
                if (cond == NULL) {
                    take = 1;  /* else branch */
                } else {
                    Value *cv = eval(in, e, cond);
                    take      = val_truthy(cv);
                    val_deref(cv);
                }
                if (take) {
                    /* reuse parent env — if/else doesn't introduce new scope */
                    Signal s = exec_block(in, e, &n->if_stmt.blocks[i]);
                    return s;
                }
            }
            return SIG_NONE;
        }

        case ND_MATCH: {
            Value *target = eval(in, e, n->match_stmt.expr);
            int matched = 0;
            for (int i = 0; i < n->match_stmt.cases.count; i++) {
                Node *cnode = n->match_stmt.cases.items[i];
                Value *pat = eval(in, e, cnode->case_stmt.pattern);
                if (val_equal(target, pat)) {
                    val_deref(pat);
                    matched = 1;
                    val_deref(target);
                    return exec_block(in, e, &cnode->case_stmt.body);
                }
                val_deref(pat);
            }
            val_deref(target);
            if (!matched && n->match_stmt.default_body.count > 0) {
                return exec_block(in, e, &n->match_stmt.default_body);
            }
            return SIG_NONE;
        }

        case ND_REPEAT: {
            Value *cv = eval(in, e, n->repeat.count);
            int    cnt = (int)num_of(cv, n->line);
            val_deref(cv);
            for (int i = 0; i < cnt; i++) {
                Signal s = exec_block(in, e, &n->repeat.body);  /* share env */
                if (s == SIG_RETURN) return SIG_RETURN;
                if (s == SIG_STOP)   break;
                /* SIG_SKIP → continue loop */
            }
            return SIG_NONE;
        }

        case ND_WHILE: {
            while (1) {
                Value *cv = eval(in, e, n->while_stmt.cond);
                int    t  = val_truthy(cv);
                val_deref(cv);
                if (!t) break;
                Signal s = exec_block(in, e, &n->while_stmt.body);  /* share env */
                if (s == SIG_RETURN) return SIG_RETURN;
                if (s == SIG_STOP)   break;
            }
            return SIG_NONE;
        }

        case ND_DEFINE: {
            SengFunc *fn = make_func(n);
            Value *fv = val_func(fn);
            env_set(e, fn->name, fv);
            val_deref(fv);
            return SIG_NONE;
        }

        case ND_CLASS: {
            SengClass *c = (SengClass *)xcalloc(1, sizeof(SengClass));
            c->name = xstrdup(n->klass.name);
            if (n->klass.parent_name) {
                Value *pv = env_get(e, n->klass.parent_name);
                if (!pv || pv->type != VAL_CLASS)
                    fatal("line %d: parent blueprint '%s' not found", n->line, n->klass.parent_name);
                c->parent = pv->klass;
                /* inherit fields */
                c->field_count = c->parent->field_count + n->klass.field_count;
                c->fields = (char **)xmalloc(sizeof(char *) * (size_t)(c->field_count ? c->field_count : 1));
                c->field_hidden = (int *)xmalloc(sizeof(int) * (size_t)(c->field_count ? c->field_count : 1));
                for (int i = 0; i < c->parent->field_count; i++) {
                    c->fields[i] = xstrdup(c->parent->fields[i]);
                    c->field_hidden[i] = c->parent->field_hidden[i];
                }
                for (int i = 0; i < n->klass.field_count; i++) {
                    c->fields[c->parent->field_count + i] = xstrdup(n->klass.fields[i]);
                    c->field_hidden[c->parent->field_count + i] = n->klass.field_hidden[i];
                }
            } else {
                c->field_count = n->klass.field_count;
                c->fields = (char **)xmalloc(sizeof(char *) * (size_t)(c->field_count ? c->field_count : 1));
                c->field_hidden = (int *)xmalloc(sizeof(int) * (size_t)(c->field_count ? c->field_count : 1));
                for (int i = 0; i < c->field_count; i++) {
                    c->fields[i] = xstrdup(n->klass.fields[i]);
                    c->field_hidden[i] = n->klass.field_hidden[i];
                }
            }
            
            c->method_count = n->klass.methods.count;
            c->methods = xmalloc(sizeof(*c->methods) * (size_t)(c->method_count ? c->method_count : 1));
            for (int i = 0; i < c->method_count; i++) {
                Node *mn = n->klass.methods.items[i];
                SengFunc *fn = make_func(mn);
                c->methods[i].name = xstrdup(fn->name);
                c->methods[i].method = val_func(fn);
                c->methods[i].hidden = n->klass.method_hidden[i];
            }
            Value *cv = val_class(c);
            env_set(e, c->name, cv);
            val_deref(cv);
            return SIG_NONE;
        }

        case ND_NEW: {
            Value *cv = env_get(e, n->instantiate.class_name);
            if (!cv || cv->type != VAL_CLASS)
                fatal("line %d: '%s' is not a blueprint", n->line, n->instantiate.class_name);
            SengClass *klass = cv->klass;
            SengInstance *inst = (SengInstance *)xcalloc(1, sizeof(SengInstance));
            inst->klass = klass;
            inst->fields = (Value **)xcalloc((size_t)klass->field_count, sizeof(Value *));
            for (int i = 0; i < klass->field_count; i++) inst->fields[i] = val_null();
            Value *iv = val_instance(inst);
            env_set(e, n->instantiate.instance_name, iv);

            /* call init if exists */
            Value *init_fn = find_method(klass, "init");
            if (init_fn) {
                Value *rv = call_func(in, e, init_fn, &n->instantiate.args, iv, n->line);
                val_deref(rv);
            } else if (n->instantiate.args.count > 0) {
                fatal("line %d: blueprint '%s' has no 'init' method but arguments were provided",
                      n->line, klass->name);
            }

            val_deref(iv);
            return SIG_NONE;
        }

        case ND_CALL_STMT: {
            Value *me_val = NULL;
            Value *fv = NULL;
            if (n->call.obj) {
                me_val = eval(in, e, n->call.obj);
                if (!me_val || me_val->type != VAL_INSTANCE)
                    fatal("line %d: expected an instance for method call", n->line);
                fv = find_method(me_val->instance->klass, n->call.name);
                if (!fv) fatal("line %d: blueprint '%s' has no method '%s'",
                               n->line, me_val->instance->klass->name, n->call.name);
            } else {
                fv = env_get(e, n->call.name);
            }
            Value *rv = call_func(in, e, fv, &n->call.args, me_val, n->line);
            if (me_val) val_deref(me_val);
            val_deref(rv);
            return SIG_NONE;
        }

        case ND_RETURN: {
            Value *v = eval(in, e, n->ret);
            if (in->ret_val) val_deref(in->ret_val);
            in->ret_val = v;
            in->signal  = SIG_RETURN;
            return SIG_RETURN;
        }

        case ND_MAKE_LIST: {
            Value *v = val_list();
            env_set(e, n->list_name, v);
            val_deref(v);
            return SIG_NONE;
        }
        case ND_MAKE_MAP: {
            Value *v = val_map();
            env_set(e, n->map_name, v);
            val_deref(v);
            return SIG_NONE;
        }

        case ND_ADD_LIST: {
            Value *lv = env_get(e, n->add_list.name);
            if (!lv || lv->type != VAL_LIST)
                fatal("line %d: '%s' is not a list", n->line, n->add_list.name);
            Value *item = eval(in, e, n->add_list.val);
            SengList *sl = lv->list;
            if (sl->count >= sl->cap) {
                sl->cap   = sl->cap ? sl->cap * 2 : 4;
                sl->items = (Value **)xrealloc(sl->items,
                            sizeof(Value *) * (size_t)sl->cap);
            }
            sl->items[sl->count++] = item;
            return SIG_NONE;
        }

        case ND_SET_ITEM: {
            Value *col = env_get(e, n->set_item.name);
            if (!col) fatal("line %d: undefined collection '%s'", n->line, n->set_item.name);
            Value *idx = eval(in, e, n->set_item.index);
            Value *val = eval(in, e, n->set_item.val);
            if (col->type == VAL_LIST) {
                int i = (int)num_of(idx, n->line) - 1;
                if (i < 0 || i >= col->list->count)
                    fatal("line %d: list index %d out of bounds", n->line, i+1);
                val_deref(col->list->items[i]);
                col->list->items[i] = val;
            } else if (col->type == VAL_MAP) {
                if (idx->type != VAL_STR) fatal("line %d: map key must be a string", n->line);
                int found = 0;
                for (int i = 0; i < col->map->count; i++) {
                    if (strcmp(col->map->keys[i], idx->str) == 0) {
                        val_deref(col->map->values[i]);
                        col->map->values[i] = val;
                        found = 1; break;
                    }
                }
                if (!found) {
                    if (col->map->count >= col->map->cap) {
                        col->map->cap = col->map->cap ? col->map->cap * 2 : 4;
                        col->map->keys = (char **)xrealloc(col->map->keys, sizeof(char *) * (size_t)col->map->cap);
                        col->map->values = (Value **)xrealloc(col->map->values, sizeof(Value *) * (size_t)col->map->cap);
                    }
                    col->map->keys[col->map->count] = xstrdup(idx->str);
                    col->map->values[col->map->count] = val;
                    col->map->count++;
                }
            } else {
                fatal("line %d: '%s' is not a list or dictionary", n->line, n->set_item.name);
            }
            val_deref(idx);
            return SIG_NONE;
        }

        case ND_FOR_EACH: {
            Value *col = eval(in, e, n->for_each.collection);
            if (!col) fatal("line %d: collection is nothing", n->line);
            if (col->type == VAL_LIST) {
                for (int i = 0; i < col->list->count; i++) {
                    env_set(e, n->for_each.var_name, col->list->items[i]);
                    Signal sig = exec_block(in, e, &n->for_each.body);
                    if (sig == SIG_STOP) { in->signal = SIG_NONE; break; }
                    if (sig == SIG_SKIP) { in->signal = SIG_NONE; continue; }
                    if (sig != SIG_NONE) { val_deref(col); return sig; }
                }
            } else if (col->type == VAL_MAP) {
                for (int i = 0; i < col->map->count; i++) {
                    env_set(e, n->for_each.var_name, col->map->values[i]);
                    Signal sig = exec_block(in, e, &n->for_each.body);
                    if (sig == SIG_STOP) { in->signal = SIG_NONE; break; }
                    if (sig == SIG_SKIP) { in->signal = SIG_NONE; continue; }
                    if (sig != SIG_NONE) { val_deref(col); return sig; }
                }
            } else {
                fatal("line %d: expression is not a list or dictionary", n->line);
            }
            val_deref(col);
            return SIG_NONE;
        }

        case ND_IMPORT: {
            char *src = read_file(n->import_path);
            if (!src)
                fatal("line %d: cannot open file '%s'", n->line, n->import_path);
            Lexer  *lex  = lexer_new(src);
            Parser *par  = parser_new(lex);
            Node   *prog = parse(par);
            parser_free(par);
            lexer_free(lex);
            free(src);
            exec_block(in, in->globals, &prog->program);
            /* keep prog alive: function body_refs point into this AST */
            if (in->import_count >= in->import_cap) {
                in->import_cap   = in->import_cap ? in->import_cap * 2 : 4;
                in->imported     = (Node **)xrealloc(in->imported,
                                   sizeof(Node *) * (size_t)in->import_cap);
            }
            in->imported[in->import_count++] = prog;
            return SIG_NONE;
        }

        case ND_IMPORT_PKG: {
            if (!pkg_register(in->globals, n->str))
                fatal("line %d: unknown package '%s'  (available: math, string, io, type, http, sys, json)",
                      n->line, n->str);
            return SIG_NONE;
        }

        case ND_STOP:  in->signal = SIG_STOP; return SIG_STOP;
        case ND_SKIP:  in->signal = SIG_SKIP; return SIG_SKIP;

        case ND_THROW: {
            Value *v = eval(in, e, n->throw_err.expr);
            if (in->signal != SIG_NONE) return in->signal;
            in->err_val = v;
            in->signal  = SIG_ERROR;
            return SIG_ERROR;
        }

        case ND_TRY: {
            Signal sig = exec_block(in, e, &n->try_catch.try_body);
            if (sig == SIG_ERROR) {
                in->signal = SIG_NONE;
                Env *cenv = env_new(e);
                if (n->try_catch.catch_var) {
                    env_set(cenv, n->try_catch.catch_var, in->err_val);
                }
                if (in->err_val) val_deref(in->err_val);
                in->err_val = NULL;
                
                sig = exec_block(in, cenv, &n->try_catch.catch_body);
                env_free(cenv);
                return sig;
            }
            return sig;
        }

        case ND_PROGRAM:
            return exec_block(in, e, &n->program);

        default:
            fatal("line %d: cannot execute node kind %d", n->line, n->kind);
            return SIG_NONE;
    }
}

/* ── public API ──────────────────────────────────────────────── */

Interp *interp_new(void) {
    Interp *in    = (Interp *)xcalloc(1, sizeof(Interp));
    in->globals   = env_new(NULL);
    return in;
}

void interp_free(Interp *in) {
    env_free(in->globals);
    if (in->ret_val) val_deref(in->ret_val);
    for (int i = 0; i < in->import_count; i++) node_free(in->imported[i]);
    free(in->imported);
    free(in);
}

void interp_exec(Interp *in, Node *program) {
    exec_block(in, in->globals, &program->program);
}

void interp_set_trace(Interp *in, int trace) {
    in->trace = trace;
}
