#include <stdio.h>
#include <assert.h>
#include "../src/config.h"

void test_parse_valid_config() {
    Config config;
    int result = parse_config("server.toml", &config);
    assert(result == 0);
    assert(config.port == 8080);
    printf("✓ Teste de config válido passou\n");
}

void test_parse_invalid_path() {
    Config config;
    int result = parse_config("inexistente.toml", &config);
    assert(result == -1);
    printf("✓ Teste de config inválido passou\n");
}

int main() {
    printf("Rodando testes...\n");
    test_parse_valid_config();
    test_parse_invalid_path();
    printf("Todos os testes passaram!\n");
    return 0;
}