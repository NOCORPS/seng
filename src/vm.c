#include "vm.h"
#include "bytecode.h"
#include "value.h"
#include "env.h"
#include "common.h"
#include "packages.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ── read helpers ────────────────────────────────────────────── */
static uint8_t  read_u8 (FILE *f) { uint8_t  v; fread(&v, 1, 1, f); return v; }
static uint32_t read_u32(FILE *f) { uint32_t v; fread(&v, 4, 1, f); return v; }
static int32_t  read_i32(FILE *f) { int32_t  v; fread(&v, 4, 1, f); return v; }
static double   read_f64(FILE *f) { double   v; fread(&v, 8, 1, f); return v; }

typedef struct { uint8_t op; int32_t arg; } VmInstr;

/* ── vm stack ────────────────────────────────────────────────── */
#define STACK_MAX 1024
typedef struct {
    Value *data[STACK_MAX];
    int    top;
} Stack;

static void push(Stack *s, Value *v) {
    if (s->top >= STACK_MAX) fatal("stack overflow");
    s->data[s->top++] = v;
}
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static Value *pop(Stack *s) {
    if (s->top <= 0) fatal("stack underflow");
    int idx = s->top - 1;
    s->top = idx;
    return s->data[idx];
}

/* ── class record in VM ──────────────────────────────────────── */
typedef struct VmClass {
    char           *name;
    struct VmClass *parent;
    int             field_count;
    char          **fields;
    int            *field_hidden;
    int             method_count;
    struct {
        char *name;
        int   body_start;
        int   param_count;
        int   hidden;
        int  *param_idxs;
    } *methods;
} VmClass;

/* ── function record in VM ───────────────────────────────────── */
typedef struct {
    char    *name;
    int      body_start;  /* instruction index */
    int      param_count;
    int     *param_idxs;  /* constant pool indices for param names */
} VmFunc;

void vm_run_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f && (path[0] == '/' || path[0] == '\\')) {
        f = fopen(path + 1, "rb");
    }
    if (!f) fatal("cannot open '%s'", path);

    /* ── read header ── */
    char magic[5] = {0};
    fread(magic, 1, 4, f);
    if (strcmp(magic, SEC_MAGIC) != 0) fatal("not a valid .sec file");
    uint8_t ver = read_u8(f);
    if (ver != SEC_VERSION) fatal("unsupported .sec version %d", ver);

    /* ── read constant pool ── */
    uint32_t pool_count = read_u32(f);
    char   **pool_str   = (char   **)xcalloc(pool_count, sizeof(char *));
    double  *pool_num   = (double  *)xcalloc(pool_count, sizeof(double));
    int     *pool_is_str= (int     *)xcalloc(pool_count, sizeof(int));

    for (uint32_t i = 0; i < pool_count; i++) {
        uint8_t tp = read_u8(f);
        pool_is_str[i] = tp;
        if (tp) {
            uint32_t len = read_u32(f);
            char *s = (char *)xmalloc(len + 1);
            fread(s, 1, len, f);
            s[len]       = '\0';
            pool_str[i]  = s;
        } else {
            pool_num[i] = read_f64(f);
        }
    }

    /* ── read instructions ── */
    uint32_t code_count = read_u32(f);
    VmInstr *code = (VmInstr *)xmalloc(sizeof(VmInstr) * code_count);
    for (uint32_t i = 0; i < code_count; i++) {
        code[i].op  = read_u8(f);
        code[i].arg = read_i32(f);
    }
    fclose(f);

    /* ── scan for definitions ── */
    int      func_count = 0, func_cap = 0;
    VmFunc  *funcs = NULL;
    int      class_count = 0, class_cap = 0;
    VmClass *classes = NULL;

    for (uint32_t i = 0; i < code_count; i++) {
        if (code[i].op == OP_DEF_FUNC) {
            int name_idx   = code[i].arg;      i++;
            int body_start = code[i].arg;       i++;
            int pcount     = code[i].arg;

            if (func_count >= func_cap) {
                func_cap = func_cap ? func_cap * 2 : 4;
                funcs = (VmFunc *)xrealloc(funcs, sizeof(VmFunc) * (size_t)func_cap);
            }
            funcs[func_count].name        = pool_str[name_idx];
            funcs[func_count].body_start  = body_start;
            funcs[func_count].param_count = pcount;
            funcs[func_count].param_idxs  = (int *)xmalloc(sizeof(int) * (size_t)(pcount ? pcount : 1));
            for (int pi = 0; pi < pcount; pi++) {
                i++;
                funcs[func_count].param_idxs[pi] = code[i].arg;
            }
            func_count++;
        } else if (code[i].op == OP_CLASS_DEF) {
            int name_idx = code[i].arg; i++;
            int parent_idx = code[i].arg; i++;
            int fcount = code[i].arg;
            if (class_count >= class_cap) {
                class_cap = class_cap ? class_cap * 2 : 4;
                classes = (VmClass *)xrealloc(classes, sizeof(VmClass) * (size_t)class_cap);
            }
            VmClass *c = &classes[class_count++];
            memset(c, 0, sizeof(*c));
            c->name = pool_str[name_idx];
            if (parent_idx >= 0) {
                const char *pname = pool_str[parent_idx];
                for (int j = 0; j < class_count - 1; j++) {
                    if (strcmp(classes[j].name, pname) == 0) { c->parent = &classes[j]; break; }
                }
            }
            c->field_count = fcount;
            c->fields = (char **)xmalloc(sizeof(char *) * (size_t)(fcount ? fcount : 1));
            c->field_hidden = (int *)xmalloc(sizeof(int) * (size_t)(fcount ? fcount : 1));
            for (int fi = 0; fi < fcount; fi++) {
                i++; c->fields[fi] = pool_str[code[i].arg];
                i++; c->field_hidden[fi] = code[i].arg;
            }
            i++; c->method_count = code[i].arg;
            c->methods = xcalloc((size_t)(c->method_count ? c->method_count : 1), sizeof(*c->methods));
            for (int mi = 0; mi < c->method_count; mi++) {
                i++; c->methods[mi].name = pool_str[code[i].arg];
                i++; c->methods[mi].body_start = code[i].arg;
                i++; c->methods[mi].param_count = code[i].arg;
                i++; c->methods[mi].hidden = code[i].arg;
                c->methods[mi].param_idxs = xmalloc(sizeof(int) * (size_t)(c->methods[mi].param_count ? c->methods[mi].param_count : 1));
                for (int pi = 0; pi < c->methods[mi].param_count; pi++) { i++; c->methods[mi].param_idxs[pi] = code[i].arg; }
            }
        }
    }

    /* ── execution ── */
    Stack  stk  = {0};
    Env   *globals = env_new(NULL);

    /* register functions */
    for (int i = 0; i < func_count; i++) {
        SengFunc *sf = (SengFunc *)xcalloc(1, sizeof(SengFunc));
        sf->name        = xstrdup(funcs[i].name);
        sf->param_count = funcs[i].param_count;
        sf->params      = (char **)xmalloc(sizeof(char *) *
                          (size_t)(sf->param_count ? sf->param_count : 1));
        for (int pi = 0; pi < sf->param_count; pi++)
            sf->params[pi] = xstrdup(pool_str[funcs[i].param_idxs[pi]]);
        sf->body_ref = (void *)(intptr_t)funcs[i].body_start;
        Value *fv = val_func(sf);
        env_set(globals, sf->name, fv);
        val_deref(fv);
    }
    
    /* register classes */
    for (int i = 0; i < class_count; i++) {
        SengClass *sc = (SengClass *)xcalloc(1, sizeof(SengClass));
        sc->name = xstrdup(classes[i].name);
        if (classes[i].parent) {
            /* find registered parent */
            Value *pv = env_get(globals, classes[i].parent->name);
            if (pv && pv->type == VAL_CLASS) sc->parent = pv->klass;
            sc->field_count = sc->parent->field_count + classes[i].field_count;
            sc->fields = xmalloc(sizeof(char *) * (size_t)(sc->field_count ? sc->field_count : 1));
            sc->field_hidden = xmalloc(sizeof(int) * (size_t)(sc->field_count ? sc->field_count : 1));
            for (int j = 0; j < sc->parent->field_count; j++) {
                sc->fields[j] = xstrdup(sc->parent->fields[j]);
                sc->field_hidden[j] = sc->parent->field_hidden[j];
            }
            for (int j = 0; j < classes[i].field_count; j++) {
                sc->fields[sc->parent->field_count + j] = xstrdup(classes[i].fields[j]);
                sc->field_hidden[sc->parent->field_count + j] = classes[i].field_hidden[j];
            }
        } else {
            sc->field_count = classes[i].field_count;
            sc->fields = xmalloc(sizeof(char *) * (size_t)(sc->field_count ? sc->field_count : 1));
            sc->field_hidden = xmalloc(sizeof(int) * (size_t)(sc->field_count ? sc->field_count : 1));
            for (int j = 0; j < sc->field_count; j++) {
                sc->fields[j] = xstrdup(classes[i].fields[j]);
                sc->field_hidden[j] = classes[i].field_hidden[j];
            }
        }
        sc->method_count = classes[i].method_count;
        sc->methods = xcalloc((size_t)(sc->method_count ? sc->method_count : 1), sizeof(*sc->methods));
        for (int j = 0; j < sc->method_count; j++) {
            sc->methods[j].name = xstrdup(classes[i].methods[j].name);
            sc->methods[j].hidden = classes[i].methods[j].hidden;
            SengFunc *sf = (SengFunc *)xcalloc(1, sizeof(SengFunc));
            sf->name = xstrdup(sc->methods[j].name);
            sf->param_count = classes[i].methods[j].param_count;
            sf->params = xmalloc(sizeof(char *) * (size_t)(sf->param_count ? sf->param_count : 1));
            for (int k = 0; k < sf->param_count; k++) sf->params[k] = xstrdup(pool_str[classes[i].methods[j].param_idxs[k]]);
            sf->body_ref = (void *)(intptr_t)classes[i].methods[j].body_start;
            sc->methods[j].method = val_func(sf);
        }
        Value *cv = val_class(sc);
        env_set(globals, sc->name, cv);
        val_deref(cv);
    }

    /* ── call stack for functions ── */
    typedef struct { int ret_pc; Env *ret_env; int ret_sp; } CallFrame;
    CallFrame call_stack[256];
    int       call_top = 0;
    Env      *cur_env  = globals;

    /* ── exception stack ── */
    typedef struct { int catch_pc; int call_depth; int stack_top; } CatchFrame;
    CatchFrame catch_stack[64];
    int        catch_top = 0;

    int32_t pc = 0;
    while ((uint32_t)pc < code_count) {
        uint8_t op  = code[pc].op;
        int32_t arg = code[pc].arg;
        pc++;

        switch (op) {
            case OP_PUSH_NUM:  push(&stk, val_num(pool_num[arg])); break;
            case OP_PUSH_STR:  push(&stk, val_str(pool_str[arg])); break;
            case OP_PUSH_TRUE: push(&stk, val_bool(1)); break;
            case OP_PUSH_FALSE:push(&stk, val_bool(0)); break;
            case OP_PUSH_NULL: push(&stk, val_null()); break;

            case OP_LOAD: {
                const char *nm = pool_str[arg];
                Value *v = env_get(cur_env, nm);
                if (!v) fatal("'%s' is not defined", nm);
                push(&stk, val_copy(v));
                break;
            }
            case OP_STORE: {
                Value *v = pop(&stk);
                env_set(cur_env, pool_str[arg], v);
                val_deref(v);
                break;
            }
            case OP_POP: {
                Value *v = pop(&stk);
                val_deref(v);
                break;
            }

            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: {
                Value *b = pop(&stk);
                Value *a = pop(&stk);
                Value *res;
                if (op == OP_ADD && (a->type == VAL_STR || b->type == VAL_STR)) {
                    char *sa = val_to_string(a), *sb = val_to_string(b);
                    char *s  = (char *)xmalloc(strlen(sa) + strlen(sb) + 1);
                    strcpy(s, sa); strcat(s, sb);
                    res = val_str(s); free(s); free(sa); free(sb);
                } else {
                    double da = a->num, db = b->num;
                    switch (op) {
                        case OP_ADD: res = val_num(da + db); break;
                        case OP_SUB: res = val_num(da - db); break;
                        case OP_MUL: res = val_num(da * db); break;
                        case OP_DIV:
                            if (db == 0) fatal("division by zero");
                            res = val_num(da / db); break;
                        case OP_MOD:
                            if (db == 0) fatal("modulo by zero");
                            res = val_num(fmod(da, db)); break;
                        default: res = val_null();
                    }
                }
                val_deref(a); val_deref(b);
                push(&stk, res);
                break;
            }

            case OP_NEG: {
                Value *v = pop(&stk);
                push(&stk, val_num(-v->num));
                val_deref(v);
                break;
            }
            case OP_NOT: {
                Value *v = pop(&stk);
                push(&stk, val_bool(!val_truthy(v)));
                val_deref(v);
                break;
            }

            case OP_CMP_EQ: case OP_CMP_NEQ: case OP_CMP_GT:
            case OP_CMP_LT: case OP_CMP_GTE: case OP_CMP_LTE: {
                Value *b = pop(&stk), *a = pop(&stk);
                int r;
                if (a->type == VAL_STR && b->type == VAL_STR) {
                    int c = strcmp(a->str, b->str);
                    switch (op) {
                        case OP_CMP_EQ:  r = (c == 0); break;
                        case OP_CMP_NEQ: r = (c != 0); break;
                        case OP_CMP_GT:  r = (c  > 0); break;
                        case OP_CMP_LT:  r = (c  < 0); break;
                        case OP_CMP_GTE: r = (c >= 0); break;
                        case OP_CMP_LTE: r = (c <= 0); break;
                        default: r = 0;
                    }
                } else {
                    double da = a->num, db = b->num;
                    switch (op) {
                        case OP_CMP_EQ:  r = (da == db); break;
                        case OP_CMP_NEQ: r = (da != db); break;
                        case OP_CMP_GT:  r = (da  > db); break;
                        case OP_CMP_LT:  r = (da  < db); break;
                        case OP_CMP_GTE: r = (da >= db); break;
                        case OP_CMP_LTE: r = (da <= db); break;
                        default: r = 0;
                    }
                }
                val_deref(a); val_deref(b);
                push(&stk, val_bool(r));
                break;
            }

            case OP_AND: {
                Value *b = pop(&stk), *a = pop(&stk);
                push(&stk, val_bool(val_truthy(a) && val_truthy(b)));
                val_deref(a); val_deref(b);
                break;
            }
            case OP_OR: {
                Value *b = pop(&stk), *a = pop(&stk);
                push(&stk, val_bool(val_truthy(a) || val_truthy(b)));
                val_deref(a); val_deref(b);
                break;
            }

            case OP_PRINT: {
                Value *v = pop(&stk);
                val_print(v);
                val_deref(v);
                break;
            }
            case OP_INPUT: {
                Value *prompt = pop(&stk);
                char *ps = val_to_string(prompt);
                printf("%s", ps); free(ps);
                fflush(stdout);
                val_deref(prompt);
                char buf[1024] = {0};
                if (fgets(buf, sizeof buf, stdin)) {
                    size_t len = strlen(buf);
                    if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
                }
                Value *v = val_str(buf);
                env_set(cur_env, pool_str[arg], v);
                val_deref(v);
                break;
            }

            case OP_JUMP:       pc = arg; break;
            case OP_JUMP_FALSE: {
                Value *v = pop(&stk);
                if (!val_truthy(v)) pc = arg;
                val_deref(v);
                break;
            }

            case OP_CALL: {
                /* top of stack = function value, below = args (arg = count) */
                Value *fv = pop(&stk);
                if (!fv) fatal("attempted to call a null value");
                
                if (fv->type == VAL_NATIVE) {
                    SengNative *nat = fv->native;
                    if (nat->arity != -1 && arg != nat->arity)
                        fatal("'%s': expected %d args, got %d", nat->name, nat->arity, arg);
                    
                    /* Collect args */
                    Value **args = (Value **)xmalloc(sizeof(Value *) * (size_t)(arg ? arg : 1));
                    for (int i = arg - 1; i >= 0; i--) args[i] = pop(&stk);
                    
                    Value *res = nat->fn(args, arg);
                    for (int i = 0; i < arg; i++) val_deref(args[i]);
                    free(args);
                    
                    push(&stk, res);
                    val_deref(fv);
                    break;
                }

                if (fv->type != VAL_FUNC)
                    fatal("attempted to call a non-function");
                SengFunc *fn = fv->func;
                if (arg != fn->param_count)
                    fatal("'%s': expected %d args, got %d",
                          fn->name, fn->param_count, arg);

                /* find body start */
                int body = -1;
                /* If body_ref is set (e.g. from blueprint method), use it directly */
                if (fn->body_ref) {
                    body = (int)(intptr_t)fn->body_ref;
                } else {
                    for (int fi = 0; fi < func_count; fi++) {
                        if (strcmp(funcs[fi].name, fn->name) == 0) {
                            body = funcs[fi].body_start; break;
                        }
                    }
                }
                if (body < 0) fatal("function '%s' body not found", fn->name);

                /* save state */
                if (call_top >= 256) fatal("call stack overflow");
                call_stack[call_top++] = (CallFrame){pc, cur_env, stk.top - arg};

                /* new scope */
                Env *fenv = env_new(globals);
                /* bind params (they are in order on stack) */
                for (int pi = fn->param_count - 1; pi >= 0; pi--) {
                    Value *av = pop(&stk);
                    env_set(fenv, fn->params[pi], av);
                    val_deref(av);
                }
                cur_env = fenv;
                pc = body;
                val_deref(fv);
                break;
            }

            case OP_RET: {
                Value *rv = pop(&stk);
                
                /* clean up any active catch frames in this function */
                while (catch_top > 0 && catch_stack[catch_top-1].call_depth == call_top) {
                    catch_top--;
                }

                if (call_top <= 0) { push(&stk, rv); goto done; }
                CallFrame fr = call_stack[--call_top];
                
                /* clean up arguments from stack */
                while (stk.top > fr.ret_sp) {
                    Value *v = pop(&stk);
                    val_deref(v);
                }
                
                env_free(cur_env);
                cur_env = fr.ret_env;
                pc      = fr.ret_pc;
                push(&stk, rv);
                break;
            }

            case OP_HALT: goto done;

            case OP_LIST_NEW: {
                Value *v = val_list();
                const char *nm = pool_str[arg];
                if (nm[0] == '\0') {
                    push(&stk, v);
                } else {
                    env_set(cur_env, nm, v);
                    val_deref(v);
                }
                break;
            }
            case OP_LIST_PUSH: {
                Value *item = pop(&stk);
                Value *lv   = env_get(cur_env, pool_str[arg]);
                if (!lv || lv->type != VAL_LIST)
                    fatal("'%s' is not a list", pool_str[arg]);
                SengList *sl = lv->list;
                if (sl->count >= sl->cap) {
                    sl->cap   = sl->cap ? sl->cap * 2 : 4;
                    sl->items = (Value **)xrealloc(sl->items,
                                sizeof(Value *) * (size_t)sl->cap);
                }
                sl->items[sl->count++] = item;
                break;
            }
            case OP_LIST_GET: {
                Value *idx_v = pop(&stk);
                Value *lv = env_get(cur_env, pool_str[arg]);
                if (!lv) fatal("undefined collection '%s'", pool_str[arg]);
                if (lv->type == VAL_LIST) {
                    int idx = (int)idx_v->num - 1;
                    if (idx < 0 || idx >= lv->list->count) fatal("list index out of range");
                    push(&stk, val_copy(lv->list->items[idx]));
                } else if (lv->type == VAL_MAP) {
                    if (idx_v->type != VAL_STR) fatal("map key must be a string");
                    Value *res = val_null();
                    for (int i = 0; i < lv->map->count; i++) {
                        if (strcmp(lv->map->keys[i], idx_v->str) == 0) {
                            res = val_copy(lv->map->values[i]); break;
                        }
                    }
                    push(&stk, res);
                } else fatal("not a list or dictionary");
                val_deref(idx_v);
                break;
            }
            case OP_LIST_LEN: {
                Value *lv = env_get(cur_env, pool_str[arg]);
                if (!lv || lv->type != VAL_LIST)
                    fatal("'%s' is not a list", pool_str[arg]);
                push(&stk, val_num((double)lv->list->count));
                break;
            }

            case OP_DEF_FUNC:
            case OP_CLASS_DEF:
                /* already scanned; skip metadata */
                {
                    if (op == OP_DEF_FUNC) {
                        /* OP_DEF_FUNC: name, body_start, pcount, params... */
                        int pcount = code[pc+1].arg;
                        pc += 2 + pcount;
                    } else {
                        /* OP_CLASS_DEF: name, parent, fcount, (fname, fhidden)..., mcount, (mname, mbody, mpcount, mhidden, mparams...)... */
                        pc++; /* skip parent */
                        int fcount = code[pc].arg; pc++;
                        pc += fcount * 2;
                        int mcount = code[pc].arg; pc++;
                        for (int mi=0; mi<mcount; mi++) {
                            pc += 2; /* skip mname, mbody */
                            int mpcount = code[pc].arg; pc++;
                            pc++; /* skip mhidden */
                            pc += mpcount;
                        }
                    }
                }
                break;

            case OP_NEW_INST: {
                const char *cname = pool_str[arg];
                const char *iname = pool_str[code[pc].arg]; pc++;
                int acount = code[pc].arg; pc++;
                Value *cv = env_get(globals, cname);
                if (!cv || cv->type != VAL_CLASS) fatal("'%s' is not a blueprint", cname);
                SengClass *klass = cv->klass;
                SengInstance *inst = (SengInstance *)xcalloc(1, sizeof(SengInstance));
                inst->klass = klass;
                inst->fields = (Value **)xcalloc((size_t)klass->field_count, sizeof(Value *));
                for (int i=0; i<klass->field_count; i++) inst->fields[i] = val_null();
                Value *iv = val_instance(inst);
                env_set(cur_env, iname, iv);
                
                /* constructor? */
                Value *init_fn = find_method(klass, "init");
                if (init_fn) {
                    if (call_top >= 256) fatal("call stack overflow");
                    call_stack[call_top++] = (CallFrame){pc, cur_env, stk.top - acount};
                    Env *fenv = env_new(globals);
                    SengFunc *fn = init_fn->func;
                    for (int pi = fn->param_count - 1; pi >= 0; pi--) {
                        Value *av = pop(&stk);
                        env_set(fenv, fn->params[pi], av);
                        val_deref(av);
                    }
                    env_set(fenv, "me", iv);
                    cur_env = fenv;
                    pc = (int)(intptr_t)fn->body_ref;
                }
                val_deref(iv);
                break;
            }

            case OP_GET_PROP: {
                Value *obj = pop(&stk);
                if (!obj || obj->type != VAL_INSTANCE) fatal("expected an instance for property get");
                int idx = find_field(obj->instance->klass, pool_str[arg]);
                if (idx < 0) fatal("instance of '%s' has no field '%s'", obj->instance->klass->name, pool_str[arg]);
                
                if (obj->instance->klass->field_hidden[idx]) {
                    Value *me = env_get(cur_env, "me");
                    if (!me || me->type != VAL_INSTANCE || me->instance != obj->instance)
                        fatal("cannot access hidden field '%s' outside of its blueprint", pool_str[arg]);
                }

                push(&stk, val_copy(obj->instance->fields[idx]));
                val_deref(obj);
                break;
            }

            case OP_SET_PROP: {
                Value *obj = pop(&stk);
                Value *val = pop(&stk);
                if (!obj || obj->type != VAL_INSTANCE) fatal("expected an instance for property set");
                int idx = find_field(obj->instance->klass, pool_str[arg]);
                if (idx < 0) fatal("instance of '%s' has no field '%s'", obj->instance->klass->name, pool_str[arg]);

                if (obj->instance->klass->field_hidden[idx]) {
                    Value *me = env_get(cur_env, "me");
                    if (!me || me->type != VAL_INSTANCE || me->instance != obj->instance)
                        fatal("cannot set hidden field '%s' outside of its blueprint", pool_str[arg]);
                }

                val_deref(obj->instance->fields[idx]);
                obj->instance->fields[idx] = val_copy(val);
                val_deref(val);
                val_deref(obj);
                break;
            }

            case OP_ME: {
                Value *v = env_get(cur_env, "me");
                if (!v) fatal("'me' is not defined in this context");
                push(&stk, val_copy(v));
                break;
            }

            case OP_METHOD_CALL: {
                const char *mname = pool_str[arg];
                int acount = code[pc].arg; pc++;
                Value *obj = pop(&stk);
                if (!obj || obj->type != VAL_INSTANCE) fatal("expected an instance for method call");

                if (is_method_hidden(obj->instance->klass, mname)) {
                    Value *me = env_get(cur_env, "me");
                    if (!me || me->type != VAL_INSTANCE || me->instance != obj->instance)
                        fatal("cannot call hidden method '%s' outside of its blueprint", mname);
                }

                Value *mv = find_method(obj->instance->klass, mname);
                if (!mv || mv->type != VAL_FUNC) fatal("instance has no method '%s'", mname);
                
                SengFunc *fn = mv->func;
                if (call_top >= 256) fatal("call stack overflow");
                call_stack[call_top++] = (CallFrame){pc, cur_env, stk.top - acount};
                Env *fenv = env_new(globals);
                for (int pi = fn->param_count - 1; pi >= 0; pi--) {
                    Value *av = pop(&stk);
                    env_set(fenv, fn->params[pi], av);
                    val_deref(av);
                }
                env_set(fenv, "me", obj);
                cur_env = fenv;
                pc = (int)(intptr_t)fn->body_ref;
                val_deref(obj);
                break;
            }

            case OP_TRY: {
                if (catch_top >= 64) fatal("exception stack overflow");
                catch_stack[catch_top++] = (CatchFrame){arg, call_top, stk.top};
                break;
            }

            case OP_THROW: {
                Value *err = pop(&stk);
                if (catch_top <= 0) {
                    char *s = val_to_string(err);
                    fprintf(stderr, "seng error: uncaught throw: %s\n", s);
                    free(s);
                    val_deref(err);
                    goto done;
                }
                CatchFrame cf = catch_stack[--catch_top];
                /* unwind call stack */
                while (call_top > cf.call_depth) {
                    CallFrame fr = call_stack[--call_top];
                    env_free(cur_env);
                    cur_env = fr.ret_env;
                }
                /* unwind value stack */
                while (stk.top > cf.stack_top) {
                    Value *v = pop(&stk);
                    val_deref(v);
                }
                push(&stk, err); /* error value for catch */
                pc = cf.catch_pc;
                break;
            }

            case OP_END_TRY: {
                if (catch_top > 0) catch_top--;
                break;
            }

            case OP_IMPORT: {
                const char *pkg_name = pool_str[arg];
                if (!pkg_register(globals, pkg_name)) {
                    fatal("unknown package '%s'", pkg_name);
                }
                break;
            }
            case OP_MAP_NEW: {
                Value *m = val_map();
                const char *nm = pool_str[arg];
                if (nm[0] == '\0') {
                    push(&stk, m);
                } else {
                    env_set(cur_env, nm, m);
                    val_deref(m);
                }
                break;
            }
            case OP_SET_ITEM: {
                Value *val = pop(&stk);
                Value *idx = pop(&stk);
                Value *col = pop(&stk);
                if (!col) fatal("undefined collection");
                if (col->type == VAL_LIST) {
                    int i = (int)idx->num - 1;
                    if (i < 0 || i >= col->list->count) fatal("list index out of bounds");
                    val_deref(col->list->items[i]);
                    col->list->items[i] = val;
                } else if (col->type == VAL_MAP) {
                    if (idx->type != VAL_STR) fatal("map key must be a string");
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
                } else fatal("not a list or dictionary");
                val_deref(idx);
                val_deref(col);
                break;
            }
            case OP_ITER_START: {
                push(&stk, val_num(0.0));
                break;
            }
            case OP_ITER_NEXT: {
                Value *idx_v = pop(&stk);
                Value *col   = stk.data[stk.top - 1];
                int i = (int)idx_v->num;
                int len = 0;
                if (col->type == VAL_LIST) len = col->list->count;
                else if (col->type == VAL_MAP) len = col->map->count;
                else fatal("ITER_NEXT on non-collection type");

                if (i >= len) {
                    pc = arg;
                    val_deref(idx_v);
                } else {
                    Value *val = (col->type == VAL_LIST) ? col->list->items[i] : col->map->values[i];
                    push(&stk, val_num((double)(i + 1)));
                    push(&stk, val_copy(val));
                    val_deref(idx_v);
                }
                break;
            }
            case OP_LIST_PUSH_STACK: {
                Value *val = pop(&stk);
                Value *col = stk.data[stk.top - 1];
                if (!col || col->type != VAL_LIST) fatal("not a list");
                list_push(col, val);
                break;
            }
            case OP_MAP_SET_STACK: {
                Value *val = pop(&stk);
                Value *key = pop(&stk);
                Value *col = stk.data[stk.top - 1];
                if (!col || col->type != VAL_MAP) fatal("not a dictionary");
                if (key->type != VAL_STR) fatal("key must be a string");
                map_set(col, key->str, val);
                val_deref(key);
                break;
            }

            default:
                fatal("unknown opcode %d", op);
        }
    }
done:
    /* clean up stack */
    while (stk.top > 0) { Value *v = pop(&stk); val_deref(v); }
    env_free(globals);
    for (int i = 0; i < func_count; i++) free(funcs[i].param_idxs);
    free(funcs);
    for (uint32_t i = 0; i < pool_count; i++) if (pool_is_str[i]) free(pool_str[i]);
    free(pool_str); free(pool_num); free(pool_is_str);
    free(code);
}

static const char *op_names[] = {
    "PUSH_NUM", "PUSH_STR", "PUSH_TRUE", "PUSH_FALSE", "PUSH_NULL",
    "LOAD", "STORE", "ADD", "SUB", "MUL", "DIV", "MOD", "NEG", "NOT",
    "CMP_EQ", "CMP_NEQ", "CMP_GT", "CMP_LT", "CMP_GTE", "CMP_LTE",
    "AND", "OR", "PRINT", "INPUT", "JUMP", "JUMP_FALSE", "CALL", "RET", "HALT",
    "LIST_NEW", "LIST_PUSH", "LIST_GET", "LIST_LEN", "POP",
    "DEF_FUNC", "CLASS_DEF", "NEW_INST", "GET_PROP", "SET_PROP", "ME", "METHOD_CALL",
    "TRY", "THROW", "END_TRY", "IMPORT", "MAP_NEW", "SET_ITEM", "ITER_START", "ITER_NEXT",
    "LIST_PUSH_STACK", "MAP_SET_STACK"
};

void vm_disasm(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) fatal("cannot open '%s'", path);
    char magic[4]; fread(magic, 1, 4, f);
    if (memcmp(magic, SEC_MAGIC, 4) != 0) fatal("not a seng bytecode file");
    uint8_t ver = read_u8(f);
    if (ver != SEC_VERSION) fatal("bytecode version mismatch");

    uint32_t cp_count = read_u32(f);
    printf("--- Constant Pool (%u) ---\n", cp_count);
    for (uint32_t i = 0; i < cp_count; i++) {
        uint8_t type = read_u8(f);
        if (type == 0) {
            double v = read_f64(f);
            printf("%4u: NUMBER %g\n", i, v);
        } else {
            uint32_t len = read_u32(f);
            char *s = (char *)xmalloc(len + 1);
            fread(s, 1, len, f); s[len] = '\0';
            printf("%4u: STRING \"%s\"\n", i, s);
            free(s);
        }
    }

    uint32_t code_count = read_u32(f);
    printf("\n--- Instructions (%u) ---\n", code_count);
    for (uint32_t i = 0; i < code_count; i++) {
        uint8_t op = read_u8(f);
        int32_t arg = read_i32(f);
        printf("%4u: %-12s %d\n", i, (op < OP_COUNT) ? op_names[op] : "???", arg);
    }
    fclose(f);
}
