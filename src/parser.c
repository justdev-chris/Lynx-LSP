#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "parser.h"

// ─── PARSER STATE ──────────────────────────────────────────────
typedef struct {
    const char* source;
    size_t len;
    int pos;
    int line;
    int col;
    Symbol* symbols;
    int symbol_count;
    ParseError* errors;
    int error_count;
    int error_cap;
} Parser;

static char peek(Parser* p) {
    return p->pos < (int)p->len ? p->source[p->pos] : 0;
}

static char advance(Parser* p) {
    char c = peek(p);
    if (c == '\n') { p->line++; p->col = 1; }
    else if (c) p->col++;
    p->pos++;
    return c;
}

static void skip_ws(Parser* p) {
    while (isspace(peek(p))) advance(p);
}

static int match_keyword(Parser* p, const char* kw) {
    int start = p->pos;
    const char* s = p->source + p->pos;
    while (*kw) {
        if (*s != *kw) { p->pos = start; return 0; }
        s++; kw++;
    }
    if (isalnum(*s) || *s == '_') { p->pos = start; return 0; }
    p->pos = start + (s - (p->source + start));
    return 1;
}

static char* parse_identifier(Parser* p) {
    int start = p->pos;
    while (isalnum(peek(p)) || peek(p) == '_') advance(p);
    int len = p->pos - start;
    char* name = malloc(len + 1);
    memcpy(name, p->source + start, len);
    name[len] = '\0';
    return name;
}

static void add_error(Parser* p, int line, int col, const char* fmt, ...) {
    if (p->error_count >= p->error_cap) {
        p->error_cap = p->error_cap ? p->error_cap * 2 : 8;
        p->errors = realloc(p->errors, p->error_cap * sizeof(ParseError));
    }
    ParseError* err = &p->errors[p->error_count++];
    err->line = line;
    err->col = col;
    va_list args;
    va_start(args, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    err->message = strdup(buf);
}

static Symbol* find_symbol(Parser* p, const char* name) {
    Symbol* s = p->symbols;
    while (s) {
        if (strcmp(s->name, name) == 0) return s;
        s = s->next;
    }
    return NULL;
}

static void add_symbol(Parser* p, char* name, SymbolKind kind, int line, int col) {
    Symbol* sym = find_symbol(p, name);
    if (sym) return; // already exists
    sym = malloc(sizeof(Symbol));
    sym->name = name;
    sym->kind = kind;
    sym->line = line;
    sym->col = col;
    sym->usage_count = 0;
    sym->usage_cap = 0;
    sym->usages = NULL;
    sym->next = p->symbols;
    p->symbols = sym;
    p->symbol_count++;
}

static void add_usage(Parser* p, Symbol* sym, int line, int col) {
    if (!sym) return;
    if (sym->usage_count >= sym->usage_cap) {
        sym->usage_cap = sym->usage_cap ? sym->usage_cap * 2 : 8;
        sym->usages = realloc(sym->usages, sym->usage_cap * sizeof(Usage));
    }
    sym->usages[sym->usage_count].line = line;
    sym->usages[sym->usage_count].col = col;
    sym->usage_count++;
}

static ASTNode* new_node(NodeType type, int line, int col) {
    ASTNode* n = calloc(1, sizeof(ASTNode));
    n->type = type;
    n->line = line;
    n->col = col;
    n->end_line = line;
    n->end_col = col;
    return n;
}

static ASTNode* parse_expression(Parser* p);
static ASTNode* parse_statement(Parser* p);

static ASTNode* parse_block(Parser* p, int* end_line, int* end_col) {
    int start_line = p->line, start_col = p->col;
    if (!match_keyword(p, "{")) {
        ASTNode* stmt = parse_statement(p);
        if (stmt) {
            *end_line = stmt->end_line;
            *end_col = stmt->end_col;
        }
        return stmt;
    }
    ASTNode* block = new_node(NODE_PROGRAM, start_line, start_col);
    block->program.statements = NULL;
    block->program.statement_count = 0;
    skip_ws(p);
    while (!match_keyword(p, "}") && peek(p)) {
        ASTNode* stmt = parse_statement(p);
        if (stmt) {
            block->program.statements = realloc(block->program.statements,
                (block->program.statement_count + 1) * sizeof(ASTNode*));
            block->program.statements[block->program.statement_count++] = stmt;
            if (stmt->end_line > block->end_line) {
                block->end_line = stmt->end_line;
                block->end_col = stmt->end_col;
            }
        }
        skip_ws(p);
    }
    if (peek(p) == '}') {
        advance(p);
        block->end_line = p->line;
        block->end_col = p->col;
    }
    *end_line = block->end_line;
    *end_col = block->end_col;
    return block;
}

static ASTNode* parse_expression(Parser* p) {
    int line = p->line, col = p->col;
    skip_ws(p);
    char c = peek(p);
    if (isalpha(c) || c == '_') {
        char* name = parse_identifier(p);
        ASTNode* n = new_node(NODE_IDENTIFIER, line, col);
        n->identifier.name = name;
        n->end_line = p->line;
        n->end_col = p->col;
        Symbol* sym = find_symbol(p, name);
        if (!sym) {
            add_error(p, line, col, "Undefined symbol '%s'", name);
        } else {
            add_usage(p, sym, line, col);
        }
        return n;
    }
    if (isdigit(c) || c == '-') {
        char* start = (char*)p->source + p->pos;
        while (isdigit(peek(p)) || peek(p) == '.') advance(p);
        int len = p->pos - (start - p->source);
        char* num = malloc(len + 1);
        memcpy(num, start, len);
        num[len] = '\0';
        ASTNode* n = new_node(NODE_NUMBER, line, col);
        n->number.value = atof(num);
        n->end_line = p->line;
        n->end_col = p->col;
        free(num);
        return n;
    }
    if (c == '"') {
        advance(p);
        int start = p->pos;
        while (peek(p) && peek(p) != '"') {
            if (peek(p) == '\\') advance(p);
            advance(p);
        }
        int len = p->pos - start;
        char* str = malloc(len + 1);
        memcpy(str, p->source + start, len);
        str[len] = '\0';
        if (peek(p) == '"') advance(p);
        else add_error(p, line, col, "Unclosed string");
        ASTNode* n = new_node(NODE_STRING, line, col);
        n->string.value = str;
        n->end_line = p->line;
        n->end_col = p->col;
        return n;
    }
    add_error(p, line, col, "Unexpected character '%c'", c);
    advance(p);
    return NULL;
}

static ASTNode* parse_statement(Parser* p) {
    int line = p->line, col = p->col;
    skip_ws(p);
    if (match_keyword(p, "Set")) {
        ASTNode* n = new_node(NODE_SET_STMT, line, col);
        n->set.name = parse_identifier(p);
        Symbol* sym = find_symbol(p, n->set.name);
        if (!sym) {
            add_symbol(p, strdup(n->set.name), SYM_VARIABLE, line, col);
        }
        skip_ws(p);
        if (peek(p) == '=') advance(p);
        n->set.value = parse_expression(p);
        n->end_line = p->line;
        n->end_col = p->col;
        return n;
    }
    if (match_keyword(p, "Func")) {
        ASTNode* n = new_node(NODE_FUNC_DEF, line, col);
        n->func_def.name = parse_identifier(p);
        add_symbol(p, strdup(n->func_def.name), SYM_FUNCTION, line, col);
        skip_ws(p);
        if (peek(p) == '(') advance(p);
        n->func_def.param_count = 0;
        n->func_def.params = NULL;
        while (peek(p) && peek(p) != ')') {
            char* pname = parse_identifier(p);
            n->func_def.params = realloc(n->func_def.params,
                (n->func_def.param_count + 1) * sizeof(char*));
            n->func_def.params[n->func_def.param_count++] = pname;
            skip_ws(p);
            if (peek(p) == ',') advance(p);
            skip_ws(p);
        }
        if (peek(p) == ')') advance(p);
        int end_line, end_col;
        n->func_def.body = parse_block(p, &end_line, &end_col);
        n->end_line = end_line;
        n->end_col = end_col;
        return n;
    }
    if (match_keyword(p, "Roar")) {
        ASTNode* n = new_node(NODE_ROAR_STMT, line, col);
        n->roar.expr = parse_expression(p);
        n->end_line = p->line;
        n->end_col = p->col;
        return n;
    }
    if (match_keyword(p, "If")) {
        ASTNode* n = new_node(NODE_IF_STMT, line, col);
        n->if_stmt.cond = parse_expression(p);
        int then_end_line, then_end_col;
        n->if_stmt.then_body = parse_block(p, &then_end_line, &then_end_col);
        n->end_line = then_end_line;
        n->end_col = then_end_col;
        n->if_stmt.else_body = NULL;
        skip_ws(p);
        if (match_keyword(p, "Else")) {
            int else_end_line, else_end_col;
            n->if_stmt.else_body = parse_block(p, &else_end_line, &else_end_col);
            n->end_line = else_end_line;
            n->end_col = else_end_col;
        }
        return n;
    }
    if (match_keyword(p, "For")) {
        ASTNode* n = new_node(NODE_FOR_STMT, line, col);
        char* var = parse_identifier(p);
        add_symbol(p, strdup(var), SYM_VARIABLE, line, col);
        n->for_stmt.var = var;
        skip_ws(p);
        if (peek(p) == '=') advance(p);
        n->for_stmt.start = parse_expression(p);
        skip_ws(p);
        if (match_keyword(p, "To")) {
            n->for_stmt.end = parse_expression(p);
        }
        int body_end_line, body_end_col;
        n->for_stmt.body = parse_block(p, &body_end_line, &body_end_col);
        n->end_line = body_end_line;
        n->end_col = body_end_col;
        return n;
    }
    if (match_keyword(p, "While")) {
        ASTNode* n = new_node(NODE_WHILE_STMT, line, col);
        n->while_stmt.cond = parse_expression(p);
        int body_end_line, body_end_col;
        n->while_stmt.body = parse_block(p, &body_end_line, &body_end_col);
        n->end_line = body_end_line;
        n->end_col = body_end_col;
        return n;
    }
    if (match_keyword(p, "Return")) {
        ASTNode* n = new_node(NODE_RETURN_STMT, line, col);
        n->ret.expr = parse_expression(p);
        n->end_line = p->line;
        n->end_col = p->col;
        return n;
    }
    // Ignore other keywords (KittyPort, Stalk_Pack, etc.)
    while (peek(p) && peek(p) != '\n' && peek(p) != ';') advance(p);
    return NULL;
}

static void free_ast_internal(ASTNode* node) {
    if (!node) return;
    switch (node->type) {
        case NODE_FUNC_DEF:
            free(node->func_def.name);
            for (int i = 0; i < node->func_def.param_count; i++)
                free(node->func_def.params[i]);
            free(node->func_def.params);
            free_ast_internal(node->func_def.body);
            break;
        case NODE_SET_STMT:
            free(node->set.name);
            free_ast_internal(node->set.value);
            break;
        case NODE_ROAR_STMT:
            free_ast_internal(node->roar.expr);
            break;
        case NODE_IF_STMT:
            free_ast_internal(node->if_stmt.cond);
            free_ast_internal(node->if_stmt.then_body);
            free_ast_internal(node->if_stmt.else_body);
            break;
        case NODE_FOR_STMT:
            free(node->for_stmt.var);
            free_ast_internal(node->for_stmt.start);
            free_ast_internal(node->for_stmt.end);
            free_ast_internal(node->for_stmt.body);
            break;
        case NODE_WHILE_STMT:
            free_ast_internal(node->while_stmt.cond);
            free_ast_internal(node->while_stmt.body);
            break;
        case NODE_RETURN_STMT:
            free_ast_internal(node->ret.expr);
            break;
        case NODE_IDENTIFIER:
            free(node->identifier.name);
            break;
        case NODE_STRING:
            free(node->string.value);
            break;
        case NODE_PROGRAM:
            for (int i = 0; i < node->program.statement_count; i++)
                free_ast_internal(node->program.statements[i]);
            free(node->program.statements);
            break;
        default:
            break;
    }
    free(node);
}

ParseResult parse_lynx(const char* source, size_t length) {
    ParseResult result = {0};
    result.source = strdup(source);
    result.source_len = length;
    result.root = NULL;
    result.symbols = NULL;
    result.symbol_count = 0;
    result.errors = NULL;
    result.error_count = 0;

    Parser p;
    p.source = source;
    p.len = length;
    p.pos = 0;
    p.line = 1;
    p.col = 1;
    p.symbols = NULL;
    p.symbol_count = 0;
    p.errors = NULL;
    p.error_count = 0;
    p.error_cap = 0;

    skip_ws(&p);
    if (peek(&p)) {
        int end_line, end_col;
        result.root = parse_block(&p, &end_line, &end_col);
    }

    result.symbols = p.symbols;
    result.symbol_count = p.symbol_count;
    result.errors = p.errors;
    result.error_count = p.error_count;

    return result;
}

void free_ast(ASTNode* node) {
    free_ast_internal(node);
}

void free_symbols(Symbol* sym) {
    while (sym) {
        Symbol* next = sym->next;
        free(sym->name);
        free(sym->usages);
        free(sym);
        sym = next;
    }
}

void free_parse_result(ParseResult* result) {
    if (!result) return;
    free(result->source);
    free_ast(result->root);
    free_symbols(result->symbols);
    for (int i = 0; i < result->error_count; i++) {
        free(result->errors[i].message);
    }
    free(result->errors);
    free(result);
}