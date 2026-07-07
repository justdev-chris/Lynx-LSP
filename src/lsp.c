#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include "lsp.h"
#include "parser.h"
#include "json.h"

#define BUFFER_SIZE 65536

// ─── DOCUMENT MANAGEMENT ──────────────────────────────────────
typedef struct DocumentEntry {
    char* uri;
    char* content;
    size_t length;
    int version;
    ParseResult* parse;
    struct DocumentEntry* next;
} DocumentEntry;

DocumentEntry* documents = NULL;
DocumentEntry* current_doc = NULL;

static void doc_free(DocumentEntry* doc) {
    if (!doc) return;
    free(doc->uri);
    free(doc->content);
    if (doc->parse) free_parse_result(doc->parse);
    free(doc);
}

static DocumentEntry* doc_find(const char* uri) {
    DocumentEntry* d = documents;
    while (d) {
        if (strcmp(d->uri, uri) == 0) return d;
        d = d->next;
    }
    return NULL;
}

static void doc_add(DocumentEntry* doc) {
    doc->next = documents;
    documents = doc;
}

static void doc_remove(const char* uri) {
    DocumentEntry** p = &documents;
    while (*p) {
        if (strcmp((*p)->uri, uri) == 0) {
            DocumentEntry* to_free = *p;
            *p = (*p)->next;
            if (current_doc == to_free) current_doc = NULL;
            doc_free(to_free);
            return;
        }
        p = &(*p)->next;
    }
}

static void doc_update(DocumentEntry* doc, const char* content, int version) {
    free(doc->content);
    doc->content = strdup(content);
    doc->length = strlen(content);
    doc->version = version;
    if (doc->parse) free_parse_result(doc->parse);
    doc->parse = malloc(sizeof(ParseResult));
    *doc->parse = parse_lynx(doc->content, doc->length);
}

// ─── LSP SEND FUNCTIONS ──────────────────────────────────────
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
    char diag[BUFFER_SIZE * 2] = "";
    if (parse && parse->error_count > 0) {
        for (int i = 0; i < parse->error_count; i++) {
            ParseError* e = &parse->errors[i];
            char entry[512];
            snprintf(entry, sizeof(entry),
                "{\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
                "\"end\":{\"line\":%d,\"character\":%d}},"
                "\"severity\":1,\"message\":\"%s\"},",
                e->line-1, e->col-1,
                e->line-1, e->col + 20,
                e->message);
            strcat(diag, entry);
        }
        if (strlen(diag) > 0) diag[strlen(diag)-1] = '\0';
    }
    char json[BUFFER_SIZE * 2];
    snprintf(json, sizeof(json),
        "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\","
        "\"params\":{\"uri\":\"%s\",\"diagnostics\":[%s]}}",
        uri, diag);
    lsp_send(json);
}

// ─── HELPERS FOR AST TRAVERSAL ──────────────────────────────
static ASTNode* find_function_def(ASTNode* node, const char* name) {
    if (!node) return NULL;
    if (node->type == NODE_FUNC_DEF && strcmp(node->func_def.name, name) == 0)
        return node;
    if (node->type == NODE_PROGRAM) {
        for (int i = 0; i < node->program.statement_count; i++) {
            ASTNode* found = find_function_def(node->program.statements[i], name);
            if (found) return found;
        }
    } else if (node->type == NODE_IF_STMT) {
        ASTNode* found = find_function_def(node->if_stmt.then_body, name);
        if (found) return found;
        if (node->if_stmt.else_body) return find_function_def(node->if_stmt.else_body, name);
    } else if (node->type == NODE_FOR_STMT) {
        return find_function_def(node->for_stmt.body, name);
    } else if (node->type == NODE_WHILE_STMT) {
        return find_function_def(node->while_stmt.body, name);
    }
    return NULL;
}

static void collect_block_ranges(ASTNode* node, char* buffer, size_t buf_size) {
    if (!node) return;
    int start_line, start_col, end_line, end_col;
    int has_body = 0;
    switch (node->type) {
        case NODE_PROGRAM:
            start_line = node->line;
            start_col = node->col;
            end_line = node->end_line;
            end_col = node->end_col;
            has_body = 1;
            break;
        case NODE_IF_STMT:
            if (node->if_stmt.then_body) {
                start_line = node->if_stmt.then_body->line;
                start_col = node->if_stmt.then_body->col;
                end_line = node->if_stmt.then_body->end_line;
                end_col = node->if_stmt.then_body->end_col;
                has_body = 1;
            }
            if (node->if_stmt.else_body) {
                char entry[128];
                snprintf(entry, sizeof(entry),
                    "{\"startLine\":%d,\"startCharacter\":%d,\"endLine\":%d,\"endCharacter\":%d},",
                    node->if_stmt.else_body->line-1, node->if_stmt.else_body->col-1,
                    node->if_stmt.else_body->end_line-1, node->if_stmt.else_body->end_col-1);
                strncat(buffer, entry, buf_size - strlen(buffer) - 1);
            }
            break;
        case NODE_FOR_STMT:
            if (node->for_stmt.body) {
                start_line = node->for_stmt.body->line;
                start_col = node->for_stmt.body->col;
                end_line = node->for_stmt.body->end_line;
                end_col = node->for_stmt.body->end_col;
                has_body = 1;
            }
            break;
        case NODE_WHILE_STMT:
            if (node->while_stmt.body) {
                start_line = node->while_stmt.body->line;
                start_col = node->while_stmt.body->col;
                end_line = node->while_stmt.body->end_line;
                end_col = node->while_stmt.body->end_col;
                has_body = 1;
            }
            break;
        case NODE_FUNC_DEF:
            if (node->func_def.body) {
                start_line = node->func_def.body->line;
                start_col = node->func_def.body->col;
                end_line = node->func_def.body->end_line;
                end_col = node->func_def.body->end_col;
                has_body = 1;
            }
            break;
        default:
            break;
    }
    if (has_body) {
        char entry[128];
        snprintf(entry, sizeof(entry),
            "{\"startLine\":%d,\"startCharacter\":%d,\"endLine\":%d,\"endCharacter\":%d},",
            start_line-1, start_col-1, end_line-1, end_col-1);
        strncat(buffer, entry, buf_size - strlen(buffer) - 1);
    }
    // Recurse
    if (node->type == NODE_PROGRAM) {
        for (int i = 0; i < node->program.statement_count; i++)
            collect_block_ranges(node->program.statements[i], buffer, buf_size);
    } else if (node->type == NODE_IF_STMT) {
        collect_block_ranges(node->if_stmt.then_body, buffer, buf_size);
        collect_block_ranges(node->if_stmt.else_body, buffer, buf_size);
    } else if (node->type == NODE_FOR_STMT) {
        collect_block_ranges(node->for_stmt.body, buffer, buf_size);
    } else if (node->type == NODE_WHILE_STMT) {
        collect_block_ranges(node->while_stmt.body, buffer, buf_size);
    } else if (node->type == NODE_FUNC_DEF) {
        collect_block_ranges(node->func_def.body, buffer, buf_size);
    }
}

// ─── HANDLERS ──────────────────────────────────────────────────

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
        "\"workspaceSymbolProvider\":true,"
        "\"signatureHelpProvider\":{\"triggerCharacters\":[\"(\",\",\"]}}";
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
        json_value* version = json_get_object(td, "version");
        if (uri && uri->type == JSON_STRING && text && text->type == JSON_STRING) {
            DocumentEntry* doc = doc_find(uri->string);
            if (!doc) {
                doc = calloc(1, sizeof(DocumentEntry));
                doc->uri = strdup(uri->string);
                doc_add(doc);
            }
            doc_update(doc, text->string, version ? (int)version->number : 1);
            current_doc = doc;
            lsp_send_diagnostics(doc->uri, doc->parse);
        }
    }
    json_free(p);
}

void lsp_handle_did_change(const char* params) {
    json_value* p = json_parse(params);
    if (!p) return;
    json_value* td = json_get_object(p, "textDocument");
    json_value* changes = json_get_object(p, "contentChanges");
    if (td && changes && changes->type == JSON_ARRAY && changes->length > 0) {
        json_value* uri = json_get_object(td, "uri");
        json_value* version = json_get_object(td, "version");
        json_value* first = changes->array[0];
        json_value* text = json_get_object(first, "text");
        if (uri && uri->type == JSON_STRING && text && text->type == JSON_STRING) {
            DocumentEntry* doc = doc_find(uri->string);
            if (doc) {
                doc_update(doc, text->string, version ? (int)version->number : doc->version + 1);
                if (current_doc == doc || !current_doc) current_doc = doc;
                lsp_send_diagnostics(doc->uri, doc->parse);
            }
        }
    }
    json_free(p);
}

void lsp_handle_did_close(const char* params) {
    json_value* p = json_parse(params);
    if (!p) return;
    json_value* td = json_get_object(p, "textDocument");
    if (td) {
        json_value* uri = json_get_object(td, "uri");
        if (uri && uri->type == JSON_STRING) {
            doc_remove(uri->string);
        }
    }
    json_free(p);
}

void lsp_handle_did_save(const char* params) {
    json_value* p = json_parse(params);
    if (!p) return;
    json_value* td = json_get_object(p, "textDocument");
    if (td) {
        json_value* uri = json_get_object(td, "uri");
        if (uri && uri->type == JSON_STRING) {
            DocumentEntry* doc = doc_find(uri->string);
            if (doc) {
                doc_update(doc, doc->content, doc->version);
                if (current_doc == doc || !current_doc) current_doc = doc;
                lsp_send_diagnostics(doc->uri, doc->parse);
            }
        }
    }
    json_free(p);
}

void lsp_handle_completion(const char* id, const char* params) {
    char items[BUFFER_SIZE * 2] = "";
    const char* keywords[] = {
        "Set", "Roar", "If", "Else", "For", "While",
        "Func", "Return", "KittyPort", "Stalk_Pack",
        "Hunt", "Pounce", "Paw", "KittyWriteFile",
        "KittyReadFile", "KittyFileExists", "KittyRemoveFile",
        "KittyListFiles", "Run", "LoadLib", "Help", "GetError",
        "KittySplitString", "KittyCheckIfStringContains",
        "KittyReplaceString", "Trim", "Len"
    };
    for (int i = 0; i < sizeof(keywords)/sizeof(keywords[0]); i++) {
        char entry[256];
        snprintf(entry, sizeof(entry),
            "{\"label\":\"%s\",\"kind\":6,\"detail\":\"Lynx keyword\"},", keywords[i]);
        strcat(items, entry);
    }
    if (current_doc && current_doc->parse) {
        Symbol* sym = current_doc->parse->symbols;
        while (sym) {
            char entry[256];
            snprintf(entry, sizeof(entry),
                "{\"label\":\"%s\",\"kind\":%d,\"detail\":\"%s\",\"data\":\"%s\"},",
                sym->name,
                sym->kind == SYM_FUNCTION ? 3 : 6,
                sym->kind == SYM_FUNCTION ? "function" : "variable",
                sym->kind == SYM_FUNCTION ? "function" : "variable");
            strcat(items, entry);
            sym = sym->next;
        }
    }
    if (strlen(items) > 0) items[strlen(items)-1] = '\0';
    char result[BUFFER_SIZE * 2];
    snprintf(result, sizeof(result), "{\"isIncomplete\":false,\"items\":[%s]}", items);
    lsp_send_response(id, result);
}

void lsp_handle_completion_resolve(const char* id, const char* params) {
    json_value* p = json_parse(params);
    if (!p) { lsp_send_response(id, "{}"); return; }
    json_value* label = json_get_object(p, "label");
    json_value* data = json_get_object(p, "data");
    if (label && label->type == JSON_STRING) {
        const char* detail = (data && data->type == JSON_STRING) ? data->string : "symbol";
        char result[BUFFER_SIZE];
        snprintf(result, sizeof(result),
            "{\"label\":\"%s\",\"kind\":6,\"detail\":\"%s\",\"documentation\":\"Lynx %s\"}",
            label->string, detail, detail);
        lsp_send_response(id, result);
    } else {
        lsp_send_response(id, "{}");
    }
    json_free(p);
}

void lsp_handle_signature_help(const char* id, const char* params) {
    json_value* p = json_parse(params);
    if (!p) { lsp_send_response(id, "{\"signatures\":[],\"activeSignature\":0,\"activeParameter\":0}"); return; }
    json_value* pos = json_get_object(p, "position");
    json_value* td = json_get_object(p, "textDocument");
    if (!pos || !td) { json_free(p); lsp_send_response(id, "{\"signatures\":[],\"activeSignature\":0,\"activeParameter\":0}"); return; }
    int line = 0, col = 0;
    json_value* linev = json_get_object(pos, "line");
    json_value* colv = json_get_object(pos, "character");
    if (linev) line = (int)linev->number;
    if (colv) col = (int)colv->number;
    json_free(p);

    if (!current_doc || !current_doc->content) {
        lsp_send_response(id, "{\"signatures\":[],\"activeSignature\":0,\"activeParameter\":0}");
        return;
    }

    const char* src = current_doc->content;
    int pos_index = 0;
    int line_start = 0;
    for (int i = 0; i < line; i++) {
        while (src[pos_index] && src[pos_index] != '\n') pos_index++;
        if (src[pos_index] == '\n') pos_index++;
    }
    line_start = pos_index;
    int cursor = line_start + col;
    int paren_pos = -1;
    for (int i = cursor - 1; i >= line_start; i--) {
        if (src[i] == '(') { paren_pos = i; break; }
        if (src[i] == ')' || src[i] == '{' || src[i] == '}') break;
    }
    if (paren_pos == -1) {
        lsp_send_response(id, "{\"signatures\":[],\"activeSignature\":0,\"activeParameter\":0}");
        return;
    }
    int name_start = paren_pos - 1;
    while (name_start >= line_start && (isalnum(src[name_start]) || src[name_start] == '_')) name_start--;
    name_start++;
    int name_len = paren_pos - name_start;
    if (name_len <= 0) {
        lsp_send_response(id, "{\"signatures\":[],\"activeSignature\":0,\"activeParameter\":0}");
        return;
    }
    char func_name[64];
    memcpy(func_name, src + name_start, name_len);
    func_name[name_len] = '\0';

    // Find function definition in AST
    ASTNode* func_def = NULL;
    if (current_doc->parse && current_doc->parse->root) {
        func_def = find_function_def(current_doc->parse->root, func_name);
    }
    if (!func_def) {
        lsp_send_response(id, "{\"signatures\":[],\"activeSignature\":0,\"activeParameter\":0}");
        return;
    }

    // Build signature with parameters
    char params_list[256] = "";
    if (func_def->func_def.param_count > 0) {
        for (int i = 0; i < func_def->func_def.param_count; i++) {
            if (i > 0) strcat(params_list, ", ");
            strcat(params_list, func_def->func_def.params[i]);
        }
    }
    char signature[BUFFER_SIZE];
    snprintf(signature, sizeof(signature),
        "{\"label\":\"%s(%s)\",\"parameters\":[",
        func_name, params_list);
    // Add parameter labels
    for (int i = 0; i < func_def->func_def.param_count; i++) {
        char param[64];
        snprintf(param, sizeof(param), "{\"label\":\"%s\"}%s",
                 func_def->func_def.params[i],
                 i == func_def->func_def.param_count - 1 ? "" : ",");
        strcat(signature, param);
    }
    strcat(signature, "]}");
    char result[BUFFER_SIZE];
    snprintf(result, sizeof(result),
        "{\"signatures\":[%s],\"activeSignature\":0,\"activeParameter\":0}",
        signature);
    lsp_send_response(id, result);
}

void lsp_handle_hover(const char* id, const char* params) {
    json_value* p = json_parse(params);
    if (!p) { lsp_send_response(id, "{\"contents\":\"\"}"); return; }
    json_value* pos = json_get_object(p, "position");
    json_value* td = json_get_object(p, "textDocument");
    if (!pos || !td) { json_free(p); lsp_send_response(id, "{\"contents\":\"\"}"); return; }
    json_value* uri = json_get_object(td, "uri");
    if (!uri || uri->type != JSON_STRING) { json_free(p); lsp_send_response(id, "{\"contents\":\"\"}"); return; }
    int line = 0, col = 0;
    json_value* linev = json_get_object(pos, "line");
    json_value* colv = json_get_object(pos, "character");
    if (linev) line = (int)linev->number;
    if (colv) col = (int)colv->number;
    json_free(p);

    DocumentEntry* doc = current_doc;
    if (!doc || !doc->parse) { lsp_send_response(id, "{\"contents\":\"\"}"); return; }
    Symbol* found = NULL;
    Symbol* sym = doc->parse->symbols;
    while (sym) {
        if (sym->line-1 == line && sym->col-1 <= col && sym->col-1 + (int)strlen(sym->name) > col) {
            found = sym; break;
        }
        sym = sym->next;
    }
    if (found) {
        char info[BUFFER_SIZE];
        snprintf(info, sizeof(info),
            "{\"contents\":\"```lynx\\n%s\\n```\\n\\n**%s**\"}",
            found->name,
            found->kind == SYM_FUNCTION ? "Function" : "Variable");
        lsp_send_response(id, info);
    } else {
        lsp_send_response(id, "{\"contents\":\"\"}");
    }
}

void lsp_handle_definition(const char* id, const char* params) {
    json_value* p = json_parse(params);
    if (!p) { lsp_send_response(id, "[]"); return; }
    json_value* pos = json_get_object(p, "position");
    json_value* td = json_get_object(p, "textDocument");
    if (!pos || !td) { json_free(p); lsp_send_response(id, "[]"); return; }
    int line = 0, col = 0;
    json_value* linev = json_get_object(pos, "line");
    json_value* colv = json_get_object(pos, "character");
    if (linev) line = (int)linev->number;
    if (colv) col = (int)colv->number;
    json_free(p);

    DocumentEntry* doc = current_doc;
    if (!doc || !doc->parse) { lsp_send_response(id, "[]"); return; }
    Symbol* found = NULL;
    Symbol* sym = doc->parse->symbols;
    while (sym) {
        if (sym->line-1 == line && sym->col-1 <= col && sym->col-1 + (int)strlen(sym->name) > col) {
            found = sym; break;
        }
        sym = sym->next;
    }
    if (found) {
        char result[BUFFER_SIZE];
        snprintf(result, sizeof(result),
            "[{\"uri\":\"%s\",\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
            "\"end\":{\"line\":%d,\"character\":%d}}}]",
            doc->uri, found->line-1, found->col-1,
            found->line-1, found->col + (int)strlen(found->name));
        lsp_send_response(id, result);
    } else {
        lsp_send_response(id, "[]");
    }
}

void lsp_handle_references(const char* id, const char* params) {
    json_value* p = json_parse(params);
    if (!p) { lsp_send_response(id, "[]"); return; }
    json_value* pos = json_get_object(p, "position");
    json_value* td = json_get_object(p, "textDocument");
    if (!pos || !td) { json_free(p); lsp_send_response(id, "[]"); return; }
    int line = 0, col = 0;
    json_value* linev = json_get_object(pos, "line");
    json_value* colv = json_get_object(pos, "character");
    if (linev) line = (int)linev->number;
    if (colv) col = (int)colv->number;
    json_free(p);

    DocumentEntry* doc = current_doc;
    if (!doc || !doc->parse) { lsp_send_response(id, "[]"); return; }
    Symbol* found = NULL;
    Symbol* sym = doc->parse->symbols;
    while (sym) {
        if (sym->line-1 == line && sym->col-1 <= col && sym->col-1 + (int)strlen(sym->name) > col) {
            found = sym; break;
        }
        sym = sym->next;
    }
    if (found) {
        char refs[BUFFER_SIZE * 2] = "";
        char entry[256];
        snprintf(entry, sizeof(entry),
            "{\"uri\":\"%s\",\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
            "\"end\":{\"line\":%d,\"character\":%d}}},",
            doc->uri, found->line-1, found->col-1,
            found->line-1, found->col + (int)strlen(found->name));
        strcat(refs, entry);
        for (int i = 0; i < found->usage_count; i++) {
            snprintf(entry, sizeof(entry),
                "{\"uri\":\"%s\",\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
                "\"end\":{\"line\":%d,\"character\":%d}}},",
                doc->uri,
                found->usages[i].line-1, found->usages[i].col-1,
                found->usages[i].line-1, found->usages[i].col + (int)strlen(found->name));
            strcat(refs, entry);
        }
        if (strlen(refs) > 0) refs[strlen(refs)-1] = '\0';
        char result[BUFFER_SIZE * 2];
        snprintf(result, sizeof(result), "[%s]", refs);
        lsp_send_response(id, result);
    } else {
        lsp_send_response(id, "[]");
    }
}

void lsp_handle_document_symbols(const char* id, const char* params) {
    char symbols[BUFFER_SIZE * 2] = "";
    DocumentEntry* doc = current_doc;
    if (doc && doc->parse) {
        Symbol* sym = doc->parse->symbols;
        while (sym) {
            char entry[256];
            snprintf(entry, sizeof(entry),
                "{\"name\":\"%s\",\"kind\":%d,\"location\":{\"uri\":\"%s\","
                "\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
                "\"end\":{\"line\":%d,\"character\":%d}}}},",
                sym->name,
                sym->kind == SYM_FUNCTION ? 12 : 13,
                doc->uri,
                sym->line-1, sym->col-1,
                sym->line-1, sym->col + (int)strlen(sym->name));
            strcat(symbols, entry);
            sym = sym->next;
        }
    }
    if (strlen(symbols) > 0) symbols[strlen(symbols)-1] = '\0';
    char result[BUFFER_SIZE * 2];
    snprintf(result, sizeof(result), "[%s]", symbols);
    lsp_send_response(id, result);
}

void lsp_handle_workspace_symbols(const char* id, const char* params) {
    char symbols[BUFFER_SIZE * 2] = "";
    DocumentEntry* doc = documents;
    while (doc) {
        if (doc->parse) {
            Symbol* sym = doc->parse->symbols;
            while (sym) {
                char entry[256];
                snprintf(entry, sizeof(entry),
                    "{\"name\":\"%s\",\"kind\":%d,\"location\":{\"uri\":\"%s\","
                    "\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
                    "\"end\":{\"line\":%d,\"character\":%d}}}},",
                    sym->name,
                    sym->kind == SYM_FUNCTION ? 12 : 13,
                    doc->uri,
                    sym->line-1, sym->col-1,
                    sym->line-1, sym->col + (int)strlen(sym->name));
                strcat(symbols, entry);
                sym = sym->next;
            }
        }
        doc = doc->next;
    }
    if (strlen(symbols) > 0) symbols[strlen(symbols)-1] = '\0';
    char result[BUFFER_SIZE * 2];
    snprintf(result, sizeof(result), "[%s]", symbols);
    lsp_send_response(id, result);
}

void lsp_handle_folding_ranges(const char* id, const char* params) {
    if (!current_doc || !current_doc->parse || !current_doc->parse->root) {
        lsp_send_response(id, "[]");
        return;
    }
    char ranges[BUFFER_SIZE * 2] = "";
    collect_block_ranges(current_doc->parse->root, ranges, sizeof(ranges));
    if (strlen(ranges) > 0) ranges[strlen(ranges)-1] = '\0';
    char result[BUFFER_SIZE * 2];
    snprintf(result, sizeof(result), "[%s]", ranges);
    lsp_send_response(id, result);
}

void lsp_handle_formatting(const char* id, const char* params) {
    if (!current_doc || !current_doc->content) {
        lsp_send_response(id, "[]");
        return;
    }
    char* src = current_doc->content;
    char* formatted = malloc(strlen(src) * 2 + 1);
    formatted[0] = '\0';
    int indent = 0;
    int line_start = 1;
    for (int i = 0; src[i]; i++) {
        char c = src[i];
        if (c == '\n') {
            strcat(formatted, "\n");
            line_start = 1;
        } else if (line_start) {
            for (int j = 0; j < indent * 2; j++) strcat(formatted, " ");
            line_start = 0;
            for (int j = i; src[j] && src[j] != '\n'; j++) {
                if (src[j] == '{') indent++;
                else if (src[j] == '}') indent--;
            }
            strcat(formatted, &src[i]);
            while (src[i] && src[i] != '\n') i++;
            if (src[i]) i--;
        } else {
            // already appended the line
        }
    }
    if (strlen(formatted) > 0 && formatted[strlen(formatted)-1] != '\n')
        strcat(formatted, "\n");
    char result[BUFFER_SIZE * 2];
    snprintf(result, sizeof(result),
        "[{\"newText\":\"%s\",\"range\":{\"start\":{\"line\":0,\"character\":0},"
        "\"end\":{\"line\":9999,\"character\":9999}}}]",
        formatted);
    lsp_send_response(id, result);
    free(formatted);
}

void lsp_handle_range_formatting(const char* id, const char* params) {
    json_value* p = json_parse(params);
    if (!p) { lsp_send_response(id, "[]"); return; }
    json_value* range = json_get_object(p, "range");
    json_value* td = json_get_object(p, "textDocument");
    if (!range || !td) { json_free(p); lsp_send_response(id, "[]"); return; }
    json_value* uri = json_get_object(td, "uri");
    if (!uri || uri->type != JSON_STRING) { json_free(p); lsp_send_response(id, "[]"); return; }
    json_value* start = json_get_object(range, "start");
    json_value* end = json_get_object(range, "end");
    if (!start || !end) { json_free(p); lsp_send_response(id, "[]"); return; }
    int start_line = (int)json_get_object(start, "line")->number;
    int start_char = (int)json_get_object(start, "character")->number;
    int end_line = (int)json_get_object(end, "line")->number;
    int end_char = (int)json_get_object(end, "character")->number;
    json_free(p);

    DocumentEntry* doc = current_doc;
    if (!doc || !doc->content) { lsp_send_response(id, "[]"); return; }

    const char* src = doc->content;
    int len = strlen(src);
    int start_pos = 0, end_pos = 0;
    int line = 0, col = 0;
    for (int i = 0; i < len; i++) {
        if (line == start_line && col == start_char) start_pos = i;
        if (line == end_line && col == end_char) end_pos = i;
        if (src[i] == '\n') { line++; col = 0; }
        else col++;
    }
    if (start_pos == end_pos) { lsp_send_response(id, "[]"); return; }

    char* selected = malloc(end_pos - start_pos + 1);
    memcpy(selected, src + start_pos, end_pos - start_pos);
    selected[end_pos - start_pos] = '\0';

    char* formatted = malloc(strlen(selected) * 2 + 1);
    formatted[0] = '\0';
    int indent = 0;
    int line_start = 1;
    for (int i = 0; selected[i]; i++) {
        char c = selected[i];
        if (c == '\n') {
            strcat(formatted, "\n");
            line_start = 1;
        } else if (line_start) {
            for (int j = 0; j < indent * 2; j++) strcat(formatted, " ");
            line_start = 0;
            for (int j = i; selected[j] && selected[j] != '\n'; j++) {
                if (selected[j] == '{') indent++;
                else if (selected[j] == '}') indent--;
            }
            strcat(formatted, &selected[i]);
            while (selected[i] && selected[i] != '\n') i++;
            if (selected[i]) i--;
        }
    }
    if (strlen(formatted) > 0 && formatted[strlen(formatted)-1] != '\n')
        strcat(formatted, "\n");

    char result[BUFFER_SIZE * 2];
    snprintf(result, sizeof(result),
        "[{\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
        "\"end\":{\"line\":%d,\"character\":%d}},\"newText\":\"%s\"}]",
        start_line, start_char, end_line, end_char, formatted);
    lsp_send_response(id, result);

    free(selected);
    free(formatted);
}

void lsp_handle_code_actions(const char* id, const char* params) {
    lsp_send_response(id, "[]");
}

void lsp_handle_rename(const char* id, const char* params) {
    json_value* p = json_parse(params);
    if (!p) { lsp_send_response(id, "[]"); return; }
    json_value* pos = json_get_object(p, "position");
    json_value* new_name = json_get_object(p, "newName");
    if (!pos || !new_name || new_name->type != JSON_STRING) {
        json_free(p); lsp_send_response(id, "[]"); return;
    }
    int line = 0, col = 0;
    json_value* linev = json_get_object(pos, "line");
    json_value* colv = json_get_object(pos, "character");
    if (linev) line = (int)linev->number;
    if (colv) col = (int)colv->number;
    json_free(p);

    DocumentEntry* doc = current_doc;
    if (!doc || !doc->parse) { lsp_send_response(id, "[]"); return; }
    Symbol* found = NULL;
    Symbol* sym = doc->parse->symbols;
    while (sym) {
        if (sym->line-1 == line && sym->col-1 <= col && sym->col-1 + (int)strlen(sym->name) > col) {
            found = sym; break;
        }
        sym = sym->next;
    }
    if (found) {
        char edits[BUFFER_SIZE * 2] = "";
        char entry[256];
        snprintf(entry, sizeof(entry),
            "{\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
            "\"end\":{\"line\":%d,\"character\":%d}},\"newText\":\"%s\"},",
            found->line-1, found->col-1,
            found->line-1, found->col + (int)strlen(found->name),
            new_name->string);
        strcat(edits, entry);
        for (int i = 0; i < found->usage_count; i++) {
            snprintf(entry, sizeof(entry),
                "{\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
                "\"end\":{\"line\":%d,\"character\":%d}},\"newText\":\"%s\"},",
                found->usages[i].line-1, found->usages[i].col-1,
                found->usages[i].line-1, found->usages[i].col + (int)strlen(found->name),
                new_name->string);
            strcat(edits, entry);
        }
        if (strlen(edits) > 0) edits[strlen(edits)-1] = '\0';
        char result[BUFFER_SIZE * 2];
        snprintf(result, sizeof(result),
            "[{\"changes\":{\"%s\":[%s]}}]",
            doc->uri, edits);
        lsp_send_response(id, result);
    } else {
        lsp_send_response(id, "[]");
    }
}

void lsp_handle_document_highlight(const char* id, const char* params) {
    json_value* p = json_parse(params);
    if (!p) { lsp_send_response(id, "[]"); return; }
    json_value* pos = json_get_object(p, "position");
    json_value* td = json_get_object(p, "textDocument");
    if (!pos || !td) { json_free(p); lsp_send_response(id, "[]"); return; }
    int line = 0, col = 0;
    json_value* linev = json_get_object(pos, "line");
    json_value* colv = json_get_object(pos, "character");
    if (linev) line = (int)linev->number;
    if (colv) col = (int)colv->number;
    json_free(p);

    DocumentEntry* doc = current_doc;
    if (!doc || !doc->parse) { lsp_send_response(id, "[]"); return; }
    Symbol* found = NULL;
    Symbol* sym = doc->parse->symbols;
    while (sym) {
        if (sym->line-1 == line && sym->col-1 <= col && sym->col-1 + (int)strlen(sym->name) > col) {
            found = sym; break;
        }
        sym = sym->next;
    }
    if (found) {
        char highlights[BUFFER_SIZE * 2] = "";
        char entry[256];
        snprintf(entry, sizeof(entry),
            "{\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
            "\"end\":{\"line\":%d,\"character\":%d}},\"kind\":1},",
            found->line-1, found->col-1,
            found->line-1, found->col + (int)strlen(found->name));
        strcat(highlights, entry);
        for (int i = 0; i < found->usage_count; i++) {
            snprintf(entry, sizeof(entry),
                "{\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
                "\"end\":{\"line\":%d,\"character\":%d}},\"kind\":2},",
                found->usages[i].line-1, found->usages[i].col-1,
                found->usages[i].line-1, found->usages[i].col + (int)strlen(found->name));
            strcat(highlights, entry);
        }
        if (strlen(highlights) > 0) highlights[strlen(highlights)-1] = '\0';
        char result[BUFFER_SIZE * 2];
        snprintf(result, sizeof(result), "[%s]", highlights);
        lsp_send_response(id, result);
    } else {
        lsp_send_response(id, "[]");
    }
}

void lsp_handle_workspace_folders(const char* id, const char* params) {
    lsp_send_response(id, "null");
}

// ─── MAIN LOOP ──────────────────────────────────────────────────

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
                        else if (strcmp(method, "textDocument/didClose") == 0)
                            lsp_handle_did_close(params_str);
                        else if (strcmp(method, "textDocument/didSave") == 0)
                            lsp_handle_did_save(params_str);
                        else if (strcmp(method, "textDocument/completion") == 0)
                            lsp_handle_completion(id, params_str);
                        else if (strcmp(method, "completionItem/resolve") == 0)
                            lsp_handle_completion_resolve(id, params_str);
                        else if (strcmp(method, "textDocument/signatureHelp") == 0)
                            lsp_handle_signature_help(id, params_str);
                        else if (strcmp(method, "textDocument/hover") == 0)
                            lsp_handle_hover(id, params_str);
                        else if (strcmp(method, "textDocument/definition") == 0)
                            lsp_handle_definition(id, params_str);
                        else if (strcmp(method, "textDocument/references") == 0)
                            lsp_handle_references(id, params_str);
                        else if (strcmp(method, "textDocument/documentSymbol") == 0)
                            lsp_handle_document_symbols(id, params_str);
                        else if (strcmp(method, "workspace/symbol") == 0)
                            lsp_handle_workspace_symbols(id, params_str);
                        else if (strcmp(method, "textDocument/foldingRange") == 0)
                            lsp_handle_folding_ranges(id, params_str);
                        else if (strcmp(method, "textDocument/formatting") == 0)
                            lsp_handle_formatting(id, params_str);
                        else if (strcmp(method, "textDocument/rangeFormatting") == 0)
                            lsp_handle_range_formatting(id, params_str);
                        else if (strcmp(method, "textDocument/codeAction") == 0)
                            lsp_handle_code_actions(id, params_str);
                        else if (strcmp(method, "textDocument/rename") == 0)
                            lsp_handle_rename(id, params_str);
                        else if (strcmp(method, "textDocument/documentHighlight") == 0)
                            lsp_handle_document_highlight(id, params_str);
                        else if (strcmp(method, "workspace/didChangeWorkspaceFolders") == 0)
                            lsp_handle_workspace_folders(id, params_str);
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