#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>

typedef enum {
    NODE_PROGRAM,
    NODE_FUNC_DEF,
    NODE_SET_STMT,
    NODE_ROAR_STMT,
    NODE_IF_STMT,
    NODE_FOR_STMT,
    NODE_WHILE_STMT,
    NODE_RETURN_STMT,
    NODE_EXPR
} NodeType;

typedef struct ASTNode {
    NodeType type;
    int line;
    int col;
    union {
        struct {
            char* name;
            char** params;
            int param_count;
            struct ASTNode** body;
            int body_count;
        } func_def;
        struct {
            char* name;
            struct ASTNode* value;
        } set;
        struct {
            struct ASTNode* expr;
        } roar;
        struct {
            struct ASTNode* cond;
            struct ASTNode** then_body;
            int then_count;
            struct ASTNode** else_body;
            int else_count;
        } if_stmt;
        struct {
            char* var;
            struct ASTNode* start;
            struct ASTNode* end;
            struct ASTNode** body;
            int body_count;
        } for_stmt;
        struct {
            struct ASTNode* cond;
            struct ASTNode** body;
            int body_count;
        } while_stmt;
        struct {
            struct ASTNode* expr;
        } ret;
        struct {
            char* op;
            struct ASTNode* left;
            struct ASTNode* right;
        } expr;
    };
} ASTNode;

typedef struct Symbol {
    char* name;
    char* kind; // "function" or "variable"
    int line;
    int col;
    ASTNode* node;
    struct Symbol* next;
} Symbol;

typedef struct {
    ASTNode* root;
    Symbol* symbols;
    int symbol_count;
    char* source;
    size_t source_len;
} ParseResult;

ParseResult parse_lynx(const char* source, size_t length);
void free_ast(ASTNode* node);
void free_symbols(Symbol* sym);

#endif