#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lsp.h"
#include "parser.h"
#include "json.h"

#define BUFFER_SIZE 65536

Document current_doc = {0};

void lsp_send_response(const char* id, const char* result) {
    char msg[BUFFER_SIZE];
    snprintf(msg, sizeof(msg),
        "Content-Length: %zu\r\n\r\n%s",
        strlen(result),
        result
    );
    printf("%s", msg);
    fflush(stdout);
}

void lsp_send_error(const char* id, int code, const char* message) {
    char json[BUFFER_SIZE];
    snprintf(json, sizeof(json),
        "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"error\":{\"code\":%d,\"message\":\"%s\"}}",
        id, code, message
    );
    lsp_send_response(id, json);
}

void lsp_handle_initialize(const char* id) {
    const char* response =
        "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"result\":{"
        "\"capabilities\":{"
        "\"textDocumentSync\":1,"
        "\"completionProvider\":{\"triggerCharacters\":[\".\",\":\"]},"
        "\"hoverProvider\":true,"
        "\"definitionProvider\":true"
        "}}}";
    char full[BUFFER_SIZE];
    snprintf(full, sizeof(full), response, id);
    lsp_send_response(id, full);
}

void lsp_handle_did_open(const char* params) {
    json_value* val = json_parse(params);
    if (!val) return;

    json_value* text_doc = json_get_object(val, "textDocument");
    if (text_doc) {
        json_value* uri = json_get_object(text_doc, "uri");
        json_value* text = json_get_object(text_doc, "text");
        if (uri && uri->type == JSON_STRING && text && text->type == JSON_STRING) {
            if (current_doc.uri) free(current_doc.uri);
            if (current_doc.content) free(current_doc.content);
            current_doc.uri = strdup(uri->string);
            current_doc.content = strdup(text->string);
            current_doc.length = strlen(text->string);

            parse_lynx(current_doc.content, current_doc.length);
        }
    }
    json_free(val);
}

void lsp_handle_completion(const char* id, const char* params) {
    const char* response =
        "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"result\":{"
        "\"items\":["
        "{\"label\":\"Set\",\"kind\":6,\"detail\":\"Set variable\"},"
        "{\"label\":\"Roar\",\"kind\":6,\"detail\":\"Print value\"},"
        "{\"label\":\"If\",\"kind\":6,\"detail\":\"Conditional\"},"
        "{\"label\":\"Else\",\"kind\":6,\"detail\":\"Else block\"},"
        "{\"label\":\"For\",\"kind\":6,\"detail\":\"For loop\"},"
        "{\"label\":\"While\",\"kind\":6,\"detail\":\"While loop\"},"
        "{\"label\":\"Func\",\"kind\":6,\"detail\":\"Define function\"},"
        "{\"label\":\"Return\",\"kind\":6,\"detail\":\"Return from function\"},"
        "{\"label\":\"KittyPort\",\"kind\":6,\"detail\":\"Load package\"},"
        "{\"label\":\"Stalk_Pack\",\"kind\":6,\"detail\":\"Run Lynx file\"},"
        "{\"label\":\"Hunt\",\"kind\":6,\"detail\":\"List variables\"},"
        "{\"label\":\"Pounce\",\"kind\":6,\"detail\":\"Delete variable\"},"
        "{\"label\":\"Paw\",\"kind\":6,\"detail\":\"Create directory\"},"
        "{\"label\":\"KittyWriteFile\",\"kind\":6,\"detail\":\"Write to file\"},"
        "{\"label\":\"KittyReadFile\",\"kind\":6,\"detail\":\"Read file\"},"
        "{\"label\":\"KittyFileExists\",\"kind\":6,\"detail\":\"Check file exists\"},"
        "{\"label\":\"KittyRemoveFile\",\"kind\":6,\"detail\":\"Delete file\"},"
        "{\"label\":\"KittyListFiles\",\"kind\":6,\"detail\":\"List directory\"},"
        "{\"label\":\"Run\",\"kind\":6,\"detail\":\"Run system command\"},"
        "{\"label\":\"LoadLib\",\"kind\":6,\"detail\":\"Load DLL\"}"
        "]}}";
    char full[BUFFER_SIZE];
    snprintf(full, sizeof(full), response, id);
    lsp_send_response(id, full);
}

void lsp_handle_hover(const char* id, const char* params) {
    // For now, return a generic hover message
    // In a real implementation, this would analyze the token at the cursor position
    const char* response =
        "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"result\":{"
        "\"contents\":\"Lynx language\\n\\nA cat-themed scripting language.\""
        "}}";
    char full[BUFFER_SIZE];
    snprintf(full, sizeof(full), response, id);
    lsp_send_response(id, full);
}

void lsp_handle_definition(const char* id, const char* params) {
    // For now, return empty result
    // In a real implementation, this would find the definition of the token at cursor
    const char* response =
        "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"result\":[]}";
    char full[BUFFER_SIZE];
    snprintf(full, sizeof(full), response, id);
    lsp_send_response(id, full);
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
                        snprintf(id, 63, "%d", id_val->number);
                    }

                    if (method_val && method_val->type == JSON_STRING) {
                        const char* method = method_val->string;
                        const char* params_str = params_val ? params_val->string : "{}";

                        if (strcmp(method, "initialize") == 0) {
                            lsp_handle_initialize(id);
                        } else if (strcmp(method, "textDocument/didOpen") == 0) {
                            lsp_handle_did_open(params_str);
                        } else if (strcmp(method, "textDocument/completion") == 0) {
                            lsp_handle_completion(id, params_str);
                        } else if (strcmp(method, "textDocument/hover") == 0) {
                            lsp_handle_hover(id, params_str);
                        } else if (strcmp(method, "textDocument/definition") == 0) {
                            lsp_handle_definition(id, params_str);
                        } else if (strcmp(method, "initialized") == 0) {
                            // No response needed
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