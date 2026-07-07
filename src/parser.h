#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>

typedef struct {
    int line;
    int col;
    char* message;
} ParseError;

typedef struct {
    ParseError* errors;
    int error_count;
    int error_capacity;
} ParseResult;

ParseResult parse_lynx(const char* source, size_t length);
void parse_result_free(ParseResult* result);

#endif