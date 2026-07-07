#ifndef LSP_H
#define LSP_H

#include <stddef.h>

#define LSP_VERSION "0.1.0"

typedef struct {
    char* uri;
    char* content;
    size_t length;
} Document;

void lsp_server_run(void);
void lsp_handle_initialize(const char* id);
void lsp_handle_did_open(const char* params);
void lsp_handle_completion(const char* id, const char* params);
void lsp_handle_hover(const char* id, const char* params);
void lsp_handle_definition(const char* id, const char* params);
void lsp_send_response(const char* id, const char* result);
void lsp_send_error(const char* id, int code, const char* message);

#endif