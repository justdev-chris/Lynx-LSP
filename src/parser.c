#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "parser.h"

typedef struct {
    const char* source;
    size_t length;
    int pos;
    int line;
    int col;
} Parser;

static char peek(Parser* p) {
    return p->pos < (int)p->length ? p->source[p->pos] : 0;
}

static char advance(Parser* p) {
    char c = peek(p);
    if (c == '\n') { p->line++; p->col = 1; }
    else if (c) { p->col++; }
    p->pos++;
    return c;
}

static void add_error(ParseResult* result, int line, int col, const char* msg) {
    if (result->error_count >= result->error_capacity) {
        result->error_capacity = result->error_capacity ? result->error_capacity * 2 : 8;
        result->errors = realloc(result->errors, result->error_capacity * sizeof(ParseError));
    }
    ParseError* err = &result->errors[result->error_count++];
    err->line = line;
    err->col = col;
    err->message = strdup(msg);
}

ParseResult parse_lynx(const char* source, size_t length) {
    ParseResult result = {0};
    Parser p;
    p.source = source;
    p.length = length;
    p.pos = 0;
    p.line = 1;
    p.col = 1;

    while (peek(&p)) {
        char c = peek(&p);
        if (isspace(c)) {
            advance(&p);
            continue;
        }

        if (c == '#') {
            while (peek(&p) && peek(&p) != '\n') advance(&p);
            continue;
        }

        // Check for known keywords
        if (isalpha(c) || c == '_') {
            char word[64];
            int start_line = p.line;
            int start_col = p.col;
            int i = 0;
            while (isalnum(peek(&p)) || peek(&p) == '_') {
                word[i++] = advance(&p);
                if (i >= 63) break;
            }
            word[i] = '\0';

            // Validate known keywords
            const char* keywords[] = {
                "Set", "Roar", "Hunt", "Help", "Stalk_Pack", "Pounce",
                "If", "Else", "Func", "Return", "For", "While",
                "Break", "Continue", "And", "Or", "Not",
                "KittyPort", "KittyWriteFile", "KittyReadFile",
                "KittyFileExists", "Paw", "KittyRemoveFile",
                "KittyListFiles", "KittyReadDir", "Run", "LoadLib",
                "GetError", "KittySplitString", "KittyCheckIfStringContains",
                "KittyReplaceString", "Trim", "Len"
            };
            int found = 0;
            for (int j = 0; j < sizeof(keywords)/sizeof(keywords[0]); j++) {
                if (strcmp(word, keywords[j]) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                char msg[128];
                snprintf(msg, sizeof(msg), "Unknown identifier '%s'", word);
                add_error(&result, start_line, start_col, msg);
            }
            continue;
        }

        // Skip strings
        if (c == '"') {
            advance(&p);
            while (peek(&p) && peek(&p) != '"') {
                if (peek(&p) == '\\') advance(&p);
                advance(&p);
            }
            if (peek(&p) == '"') advance(&p);
            else {
                add_error(&result, p.line, p.col, "Unclosed string literal");
            }
            continue;
        }

        // Skip numbers
        if (isdigit(c) || c == '-') {
            advance(&p);
            while (isdigit(peek(&p)) || peek(&p) == '.') advance(&p);
            continue;
        }

        // Skip operators and braces
        if (strchr("{}()[]=+-*/%!<>:,", c)) {
            advance(&p);
            continue;
        }

        // Unknown character
        char msg[64];
        snprintf(msg, sizeof(msg), "Unexpected character '%c'", c);
        add_error(&result, p.line, p.col, msg);
        advance(&p);
    }

    return result;
}

void parse_result_free(ParseResult* result) {
    if (!result) return;
    for (int i = 0; i < result->error_count; i++) {
        free(result->errors[i].message);
    }
    free(result->errors);
}