#ifndef JSON_H
#define JSON_H

#include <stddef.h>

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type;

typedef struct json_value {
    json_type type;
    union {
        int boolean;
        double number;
        char* string;
        struct json_value** array;
        struct json_pair* object;
    };
    int length;
} json_value;

typedef struct json_pair {
    char* key;
    json_value* value;
} json_pair;

json_value* json_parse(const char* str);
char* json_stringify(json_value* val);
json_value* json_get_object(json_value* obj, const char* key);
void json_free(json_value* val);

#endif