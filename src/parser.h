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
    NODE_IDENTIFIER,
    NODE_NUMBER,
    NODE_STRING,
    NODE_EXPR
} NodeType;

typedef enum {
    SYM_FUNCTION,
    SYM_VARIABLE
} SymbolKind;

typedef struct ASTNode {
    NodeType type;
    int line;
    int col;
    int end_line;
    int end_col;
    union {
        struct {
            char* name;
            char** params;
            int param_count;
            struct ASTNode* body;
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
            struct ASTNode* then_body;
            struct ASTNode* else_body;
        } if_stmt;
        struct {
            char* var;
            struct ASTNode* start;
            struct ASTNode* end;
            struct ASTNode* body;
        } for_stmt;
        struct {
            struct ASTNode* cond;
            struct ASTNode* body;
        } while_stmt;
        struct {
            struct ASTNode* expr;
        } ret;
        struct {
            char* name;
        } identifier;
        struct {
            double value;
        } number;
        struct {
            char* value;
        } string;
        struct {
            char* op;
            struct ASTNode* left;
            struct ASTNode* right;
        } expr;
        struct {
            struct ASTNode** statements;
            int statement_count;
        } program;
    };
} ASTNode;

typedef struct Usage {
    int line;
    int col;
} Usage;

typedef struct Symbol {
    char* name;
    SymbolKind kind;
    int line;
    int col;
    Usage* usages;
    int usage_count;
    int usage_cap;
    struct Symbol* next;
} Symbol;

typedef struct ParseError {
    int line;
    int col;
    char* message;
} ParseError;

typedef struct {
    ASTNode* root;
    Symbol* symbols;
    int symbol_count;
    ParseError* errors;
    int error_count;
    char* source;
    size_t source_len;
} ParseResult;

ParseResult parse_lynx(const char* source, size_t length);
void free_ast(ASTNode* node);
void free_symbols(Symbol* sym);
void free_parse_result(ParseResult* result);

#endif