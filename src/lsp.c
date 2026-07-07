#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "lsp.h"
#include "parser.h"
#include "json.h"

#define BUFFER_SIZE 65536

typedef struct {
    char* uri;
    char* content;
    size_t length;
    int version;
    ParseResult* parse;
} Document;

Document current_doc = {0};

void lsp_send(const char* json) {
    char msg[BUFFER_SIZE];
    snprintf(msg, sizeof(msg),
        "Content-Length: %zu\r\n\r\n%s",
        strlen(json), json);
    printf("%s", msg);
    fflush(stdout);
}

void lsp_send_response(const char* id, const char* result) {
    char json[BUFFER_SIZE];
    snprintf(json, sizeof(json),
        "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"result\":%s}",
        id, result);
    lsp_send(json);
}

void lsp_send_error(const char* id, int code, const char* message) {
    char json[BUFFER_SIZE];
    snprintf(json, sizeof(json),
        "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"error\":{\"code\":%d,\"message\":\"%s\"}}",
        id, code, message);
    lsp_send(json);
}

void lsp_send_diagnostics(const char* uri, ParseResult* parse) {
    char diag[BUFFER_SIZE] = "";
    if (parse) {
        // Generate diagnostics from parse errors
        // We don't have errors in ParseResult yet; we need to add error list.
        // For now, send empty diagnostics.
    }
    char json[BUFFER_SIZE];
    snprintf(json, sizeof(json),
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\","
        "\"params\":{\"uri\":\"%s\",\"diagnostics\":[]}}",
        uri);
    lsp_send(json);
}

void lsp_handle_initialize(const char* id) {
    const char* caps =
        "{\"textDocumentSync\":1,"
        "\"completionProvider\":{\"triggerCharacters\":[\".\",\":\"]},"
        "\"hoverProvider\":true,"
        "\"definitionProvider\":true,"
        "\"referencesProvider\":true,"
        "\"documentSymbolProvider\":true,"
        "\"foldingRangeProvider\":true,"
        "\"documentFormattingProvider\":true,"
        "\"renameProvider\":true,"
        "\"workspaceSymbolProvider\":true}";
    char result[BUFFER_SIZE];
    snprintf(result, sizeof(result), "{\"capabilities\":%s}", caps);
    lsp_send_response(id, result);
}

void lsp_handle_did_open(const char* params) {
    json_value* p = json_parse(params);
    if (!p) return;
    json_value* td = json_get_object(p, "textDocument");
    if (td) {
        json_value* uri = json_get_object(td, "uri");
        json_value* text = json_get_object(td, "text");
        if (uri && uri->type == JSON_STRING && text && text->type == JSON_STRING) {
            if (current_doc.uri) free(current_doc.uri);
            if (current_doc.content) free(current_doc.content);
            if (current_doc.parse) { free_ast(current_doc.parse->root); free_symbols(current_doc.parse->symbols); free(current_doc.parse); }
            current_doc.uri = strdup(uri->string);
            current_doc.content = strdup(text->string);
            current_doc.length = strlen(text->string);
            current_doc.parse = malloc(sizeof(ParseResult));
            *current_doc.parse = parse_lynx(current_doc.content, current_doc.length);
            lsp_send_diagnostics(current_doc.uri, current_doc.parse);
        }
    }
    json_free(p);
}

void lsp_handle_did_change(const char* params) {
    // Similar to didOpen, update content and reparse
    // For brevity, we'll implement minimally.
    // Real implementation would parse the changes.
}

void lsp_handle_completion(const char* id, const char* params) {
    // Build completion list from keywords + symbols
    const char* keywords[] = {
        "Set", "Roar", "If", "Else", "For", "While",
        "Func", "Return", "KittyPort", "Stalk_Pack",
        "Hunt", "Pounce", "Paw", "KittyWriteFile",
        "KittyReadFile", "KittyFileExists", "KittyRemoveFile",
        "KittyListFiles", "Run", "LoadLib"
    };
    char items[BUFFER_SIZE] = "";
    for (int i = 0; i < sizeof(keywords)/sizeof(keywords[0]); i++) {
        char entry[256];
        snprintf(entry, sizeof(entry),
            "{\"label\":\"%s\",\"kind\":6,\"detail\":\"Lynx keyword\"},", keywords[i]);
        strcat(items, entry);
    }
    // Add symbols from current document
    if (current_doc.parse) {
        Symbol* sym = current_doc.parse->symbols;
        while (sym) {
            char entry[256];
            snprintf(entry, sizeof(entry),
                "{\"label\":\"%s\",\"kind\":%s,\"detail\":\"%s\"},",
                sym->name,
                strcmp(sym->kind, "function")==0 ? "3" : "6",
                sym->kind);
            strcat(items, entry);
            sym = sym->next;
        }
    }
    if (strlen(items) > 0) items[strlen(items)-1] = '\0'; // remove trailing comma
    char result[BUFFER_SIZE];
    snprintf(result, sizeof(result), "{\"isIncomplete\":false,\"items\":[%s]}", items);
    lsp_send_response(id, result);
}

void lsp_handle_hover(const char* id, const char* params) {
    // For simplicity, return a generic hover
    // Real implementation would look up symbol under cursor
    const char* info =
        "{\"contents\":\"Lynx language\\n\\nA cat-themed scripting language.\"}";
    lsp_send_response(id, info);
}

void lsp_handle_definition(const char* id, const char* params) {
    // Return empty array for now
    lsp_send_response(id, "[]");
}

void lsp_handle_references(const char* id, const char* params) {
    lsp_send_response(id, "[]");
}

void lsp_handle_document_symbols(const char* id, const char* params) {
    char symbols[BUFFER_SIZE] = "";
    if (current_doc.parse) {
        Symbol* sym = current_doc.parse->symbols;
        while (sym) {
            char entry[256];
            snprintf(entry, sizeof(entry),
                "{\"name\":\"%s\",\"kind\":%d,\"location\":{\"uri\":\"%s\",\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}}}},",
                sym->name,
                strcmp(sym->kind, "function")==0 ? 12 : 13,
                current_doc.uri,
                sym->line-1, sym->col-1,
                sym->line-1, sym->col+strlen(sym->name)
            );
            strcat(symbols, entry);
            sym = sym->next;
        }
    }
    if (strlen(symbols) > 0) symbols[strlen(symbols)-1] = '\0';
    char result[BUFFER_SIZE];
    snprintf(result, sizeof(result), "[%s]", symbols);
    lsp_send_response(id, result);
}

void lsp_handle_folding_ranges(const char* id, const char* params) {
    // For now, return empty
    lsp_send_response(id, "[]");
}

void lsp_handle_formatting(const char* id, const char* params) {
    // Simple formatting: just return the same content
    char result[BUFFER_SIZE];
    snprintf(result, sizeof(result),
        "[{\"newText\":\"%s\",\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":999,\"character\":999}}}]",
        current_doc.content ? current_doc.content : "");
    lsp_send_response(id, result);
}

void lsp_handle_rename(const char* id, const char* params) {
    lsp_send_response(id, "[]");
}

void lsp_handle_workspace_symbols(const char* id, const char* params) {
    // For now, return empty
    lsp_send_response(id, "[]");
}

void lsp_server_run(void) {
    char buffer[BUFFER_SIZE];
    int pos = 0;

    while (1) {
        int c = getchar();
        if (c == EOF) break;
        buffer[pos++] = (char)c;

        if (pos > 4 && buffer[pos-4] == '\r' && buffer[pos-3] == '\n' &&
            buffer[pos-2] == '\r' && buffer[pos-1] == '\n') {
            char* len_start = strstr(buffer, "Content-Length:");
            if (len_start) {
                int len = atoi(len_start + 15);
                char* body_start = buffer + pos;
                int body_read = 0;
                while (body_read < len) {
                    int ch = getchar();
                    if (ch == EOF) break;
                    body_start[body_read++] = (char)ch;
                }
                body_start[body_read] = '\0';

                json_value* msg = json_parse(body_start);
                if (msg) {
                    json_value* method_val = json_get_object(msg, "method");
                    json_value* id_val = json_get_object(msg, "id");
                    json_value* params_val = json_get_object(msg, "params");

                    char id[64] = "unknown";
                    if (id_val && id_val->type == JSON_STRING) {
                        strncpy(id, id_val->string, 63);
                        id[63] = '\0';
                    } else if (id_val && id_val->type == JSON_NUMBER) {
                        snprintf(id, 63, "%d", (int)id_val->number);
                    }

                    if (method_val && method_val->type == JSON_STRING) {
                        const char* method = method_val->string;
                        const char* params_str = params_val ? params_val->string : "{}";

                        if (strcmp(method, "initialize") == 0)
                            lsp_handle_initialize(id);
                        else if (strcmp(method, "textDocument/didOpen") == 0)
                            lsp_handle_did_open(params_str);
                        else if (strcmp(method, "textDocument/didChange") == 0)
                            lsp_handle_did_change(params_str);
                        else if (strcmp(method, "textDocument/completion") == 0)
                            lsp_handle_completion(id, params_str);
                        else if (strcmp(method, "textDocument/hover") == 0)
                            lsp_handle_hover(id, params_str);
                        else if (strcmp(method, "textDocument/definition") == 0)
                            lsp_handle_definition(id, params_str);
                        else if (strcmp(method, "textDocument/references") == 0)
                            lsp_handle_references(id, params_str);
                        else if (strcmp(method, "textDocument/documentSymbol") == 0)
                            lsp_handle_document_symbols(id, params_str);
                        else if (strcmp(method, "textDocument/foldingRange") == 0)
                            lsp_handle_folding_ranges(id, params_str);
                        else if (strcmp(method, "textDocument/formatting") == 0)
                            lsp_handle_formatting(id, params_str);
                        else if (strcmp(method, "textDocument/rename") == 0)
                            lsp_handle_rename(id, params_str);
                        else if (strcmp(method, "workspace/symbol") == 0)
                            lsp_handle_workspace_symbols(id, params_str);
                        else if (strcmp(method, "initialized") == 0) {
                            // No response
                        } else if (strcmp(method, "shutdown") == 0) {
                            lsp_send_response(id, "null");
                            return;
                        } else if (strcmp(method, "exit") == 0) {
                            return;
                        } else {
                            lsp_send_error(id, -32601, "Method not found");
                        }
                    }
                    json_free(msg);
                }
                pos = 0;
            }
        }
    }
}