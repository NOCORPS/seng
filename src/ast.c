
#include "ast.h"

void node_list_push(NodeList *nl, Node *n) {
    if (nl->count >= nl->cap) {
        nl->cap = nl->cap ? nl->cap * 2 : 4;
        nl->items = (Node **)xrealloc(nl->items, sizeof(Node *) * (size_t)nl->cap);
    }
    nl->items[nl->count++] = n;
}

Node *node_new(NodeKind kind, int line) {
    Node *n = (Node *)xcalloc(1, sizeof(Node));
    n->kind = kind;
    n->line = line;
    return n;
}

static void node_list_free(NodeList *nl);

void node_free(Node *n) {
    if (!n) return;
    switch (n->kind) {
        case ND_STRING: case ND_IDENT:    free(n->str);  break;
        case ND_SET:    free(n->set.name); node_free(n->set.expr); break;
        case ND_SAY:    node_free(n->say); break;
        case ND_ASK:    free(n->ask.name); free(n->ask.prompt); break;
        case ND_REPEAT: node_free(n->repeat.count); node_list_free(&n->repeat.body); break;
        case ND_WHILE:  node_free(n->while_stmt.cond); node_list_free(&n->while_stmt.body); break;
        case ND_RETURN: node_free(n->ret); break;
        case ND_MAKE_LIST: free(n->list_name); break;
        case ND_ADD_LIST: node_free(n->add_list.val); free(n->add_list.name); break;
        case ND_IMPORT:     free(n->import_path); break;
        case ND_IMPORT_PKG: free(n->str); break;
        case ND_BINOP:  node_free(n->binop.left); node_free(n->binop.right); break;
        case ND_NEGATE: case ND_NOT: node_free(n->unary); break;
        case ND_CMP:    node_free(n->cmp.left); node_free(n->cmp.right); break;
        case ND_AND: case ND_OR:
            node_free(n->logical.left); node_free(n->logical.right); break;
        case ND_LIST_GET: node_free(n->list_get.index); free(n->list_get.name); break;
        case ND_LIST_LEN: free(n->list_len); break;
        case ND_CALL_STMT: case ND_CALL_EXPR:
            free(n->call.name); node_free(n->call.obj); node_list_free(&n->call.args); break;
        case ND_DEFINE:
            free(n->define.name);
            for (int i = 0; i < n->define.param_count; i++) free(n->define.params[i]);
            free(n->define.params);
            node_list_free(&n->define.body);
            break;
        case ND_PROGRAM: node_list_free(&n->program); break;
        case ND_IF:
            node_list_free(&n->if_stmt.conds);
            for (int i = 0; i < n->if_stmt.count; i++)
                node_list_free(&n->if_stmt.blocks[i]);
            free(n->if_stmt.blocks);
            break;
        case ND_CLASS:
            free(n->klass.name);
            free(n->klass.parent_name);
            for (int i = 0; i < n->klass.field_count; i++) free(n->klass.fields[i]);
            free(n->klass.fields);
            free(n->klass.field_hidden);
            node_list_free(&n->klass.methods);
            free(n->klass.method_hidden);
            break;
        case ND_NEW:
            free(n->instantiate.class_name);
            free(n->instantiate.instance_name);
            node_list_free(&n->instantiate.args);
            break;
        case ND_PROP_GET:
            free(n->prop_get.name);
            node_free(n->prop_get.obj);
            break;
        case ND_PROP_SET:
            free(n->prop_set.name);
            node_free(n->prop_set.obj);
            node_free(n->prop_set.expr);
            break;
        case ND_TRY:
            node_list_free(&n->try_catch.try_body);
            free(n->try_catch.catch_var);
            node_list_free(&n->try_catch.catch_body);
            break;
        case ND_THROW:
            node_free(n->throw_err.expr);
            break;
        case ND_MAKE_MAP:
            free(n->map_name);
            break;
        case ND_FOR_EACH:
            free(n->for_each.var_name);
            node_free(n->for_each.collection);
            node_list_free(&n->for_each.body);
            break;
        case ND_SET_ITEM:
            free(n->set_item.name);
            node_free(n->set_item.index);
            node_free(n->set_item.val);
            break;
        case ND_ME: break;
        default: break;
    }
    free(n);
}

static void node_list_free(NodeList *nl) {
    for (int i = 0; i < nl->count; i++) node_free(nl->items[i]);
    free(nl->items);
    nl->items = NULL; nl->count = 0; nl->cap = 0;
}

const char *node_kind_str(NodeKind k) {
    switch (k) {
        case ND_PROGRAM:   return "PROGRAM";
        case ND_SET:       return "SET";
        case ND_SAY:       return "SAY";
        case ND_ASK:       return "ASK";
        case ND_IF:        return "IF";
        case ND_REPEAT:    return "REPEAT";
        case ND_WHILE:     return "WHILE";
        case ND_DEFINE:    return "DEFINE";
        case ND_CALL_STMT: return "CALL_STMT";
        case ND_RETURN:    return "RETURN";
        case ND_MAKE_LIST: return "MAKE_LIST";
        case ND_ADD_LIST:  return "ADD_LIST";
        case ND_IMPORT:    return "IMPORT";
        case ND_IMPORT_PKG:return "IMPORT_PKG";
        case ND_STOP:      return "STOP";
        case ND_SKIP:      return "SKIP";
        case ND_TRY:       return "TRY";
        case ND_THROW:     return "THROW";
        case ND_MAKE_MAP:  return "MAKE_MAP";
        case ND_FOR_EACH:  return "FOR_EACH";
        case ND_SET_ITEM:  return "SET_ITEM";
        case ND_NUMBER:    return "NUMBER";
        case ND_STRING:    return "STRING";
        case ND_BOOL:      return "BOOL";
        case ND_NOTHING:   return "NOTHING";
        case ND_IDENT:     return "IDENT";
        case ND_BINOP:     return "BINOP";
        case ND_NEGATE:    return "NEGATE";
        case ND_NOT:       return "NOT";
        case ND_CMP:       return "CMP";
        case ND_AND:       return "AND";
        case ND_OR:        return "OR";
        case ND_CALL_EXPR: return "CALL_EXPR";
        case ND_LIST_GET:  return "LIST_GET";
        case ND_LIST_LEN:  return "LIST_LEN";
        case ND_CLASS:     return "CLASS";
        case ND_NEW:       return "NEW";
        case ND_PROP_GET:  return "PROP_GET";
        case ND_PROP_SET:  return "PROP_SET";
        case ND_ME:        return "ME";
        default:           return "UNKNOWN";
    }
}
