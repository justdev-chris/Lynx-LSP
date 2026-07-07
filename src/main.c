#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lsp.h"

int main(int argc, char* argv[]) {
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        printf("🐱 Lynx-LSP v1.0.0\n");
        printf("Language Server Protocol for Lynx\n\n");
        printf("Usage:\n");
        printf("  lynx-lsp --stdio    Run as LSP server\n");
        printf("  lynx-lsp --version  Show version\n");
        printf("  lynx-lsp --help     Show this help\n");
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        printf("Lynx-LSP 1.0.0\n");
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "--stdio") == 0) {
        lsp_server_run();
        return 0;
    }

    printf("🐱 Lynx-LSP v1.0.0\n");
    printf("Run with --stdio to start LSP server, or --help for usage.\n");
    return 0;
}