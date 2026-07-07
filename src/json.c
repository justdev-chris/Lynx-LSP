#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "json.h"

typedef struct {
    const char* str;
    int pos;
    int len;
} ParseState;

static void skip_whitespace(ParseState* p) {
    while (p->pos < p->len && isspace(p->str[p->pos])) p->pos++;
}

static char peek(ParseState* p) {
    return p->pos < p->len ? p->str[p->pos] : 0;
}

static char advance(ParseState* p) {
    return p->pos < p->len ? p->str[p->pos++] : 0;
}

static int match(ParseState* p, char c) {
    if (peek(p) == c) {
        advance(p);
        return 1;
    }
    return 0;
}

static json_value* parse_value(ParseState* p);

static json_value* parse_string(ParseState* p) {
    advance(p); // skip opening "
    char* start = (char*)p->str + p->pos;
    while (peek(p) != '"' && peek(p) != 0) {
        if (peek(p) == '\\') advance(p);
        advance(p);
    }
    int len = (char*)p->str + p->pos - start;
    advance(p); // skip closing "
    json_value* val = malloc(sizeof(json_value));
    val->type = JSON_STRING;
    val->string = malloc(len + 1);
    memcpy(val->string, start, len);
    val->string[len] = '\0';
    return val;
}

static json_value* parse_number(ParseState* p) {
    char* start = (char*)p->str + p->pos;
    while (isdigit(peek(p)) || peek(p) == '.') advance(p);
    int len = (char*)p->str + p->pos - start;
    char* str = malloc(len + 1);
    memcpy(str, start, len);
    str[len] = '\0';
    json_value* val = malloc(sizeof(json_value));
    val->type = JSON_NUMBER;
    val->number = atof(str);
    free(str);
    return val;
}

static json_value* parse_object(ParseState* p) {
    advance(p); // skip {
    json_value* obj = malloc(sizeof(json_value));
    obj->type = JSON_OBJECT;
    obj->length = 0;
    obj->object = NULL;

    skip_whitespace(p);
    while (peek(p) != '}' && peek(p) != 0) {
        json_pair pair = {0};
        if (peek(p) != '"') break;
        json_value* key = parse_string(p);
        if (!key) break;
        pair.key = key->string;
        free(key);
        skip_whitespace(p);
        if (!match(p, ':')) break;
        skip_whitespace(p);
        pair.value = parse_value(p);
        if (!pair.value) break;
        obj->object = realloc(obj->object, (obj->length + 1) * sizeof(json_pair));
        obj->object[obj->length++] = pair;
        skip_whitespace(p);
        if (!match(p, ',')) break;
        skip_whitespace(p);
    }
    if (peek(p) == '}') advance(p);
    return obj;
}

static json_value* parse_array(ParseState* p) {
    advance(p); // skip [
    json_value* arr = malloc(sizeof(json_value));
    arr->type = JSON_ARRAY;
    arr->length = 0;
    arr->array = NULL;

    skip_whitespace(p);
    while (peek(p) != ']' && peek(p) != 0) {
        json_value* val = parse_value(p);
        if (!val) break;
        arr->array = realloc(arr->array, (arr->length + 1) * sizeof(json_value*));
        arr->array[arr->length++] = val;
        skip_whitespace(p);
        if (!match(p, ',')) break;
        skip_whitespace(p);
    }
    if (peek(p) == ']') advance(p);
    return arr;
}

static json_value* parse_value(ParseState* p) {
    skip_whitespace(p);
    char c = peek(p);
    if (c == '"') return parse_string(p);
    if (c == '{') return parse_object(p);
    if (c == '[') return parse_array(p);
    if (isdigit(c) || c == '-') return parse_number(p);
    if (match(p, 't')) {
        if (match(p, 'r') && match(p, 'u') && match(p, 'e')) {
            json_value* val = malloc(sizeof(json_value));
            val->type = JSON_BOOL;
            val->boolean = 1;
            return val;
        }
    }
    if (match(p, 'f')) {
        if (match(p, 'a') && match(p, 'l') && match(p, 's') && match(p, 'e')) {
            json_value* val = malloc(sizeof(json_value));
            val->type = JSON_BOOL;
            val->boolean = 0;
            return val;
        }
    }
    if (match(p, 'n')) {
        if (match(p, 'u') && match(p, 'l') && match(p, 'l')) {
            json_value* val = malloc(sizeof(json_value));
            val->type = JSON_NULL;
            return val;
        }
    }
    return NULL;
}

json_value* json_parse(const char* str) {
    ParseState p;
    p.str = str;
    p.pos = 0;
    p.len = strlen(str);
    return parse_value(&p);
}

json_value* json_get_object(json_value* obj, const char* key) {
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    for (int i = 0; i < obj->length; i++) {
        if (obj->object[i].key && strcmp(obj->object[i].key, key) == 0) {
            return obj->object[i].value;
        }
    }
    return NULL;
}

void json_free(json_value* val) {
    if (!val) return;
    if (val->type == JSON_STRING) {
        free(val->string);
    } else if (val->type == JSON_OBJECT) {
        for (int i = 0; i < val->length; i++) {
            free(val->object[i].key);
            json_free(val->object[i].value);
        }
        free(val->object);
    } else if (val->type == JSON_ARRAY) {
        for (int i = 0; i < val->length; i++) {
            json_free(val->array[i]);
        }
        free(val->array);
    }
    free(val);
}