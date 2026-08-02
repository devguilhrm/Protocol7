CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -D_GNU_SOURCE -Isrc -g
LDFLAGS = -pthread
SRC_DIR = src
OBJ_DIR = obj
TARGET = servidor

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))
DEPS = $(OBJS:.o=.d)

.PHONY: all clean test run debug install help

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "🔗 Linkando $(TARGET)..."
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "✅ Build completo!"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@echo "📦 Compilando $<..."
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

# Testes
test: $(TARGET)
	@echo " Rodando testes..."
	@./tests/run_tests.sh || echo "❌ Alguns testes falharam"

# Executar
run: $(TARGET)
	@echo " Iniciando servidor..."
	./$(TARGET) --config=server.toml

# Debug build
debug: CFLAGS += -g -O0 -DDEBUG
debug: clean $(TARGET)

# Limpar
clean:
	@echo "🧹 Limpando build..."
	rm -rf $(OBJ_DIR) $(TARGET) test_runner
	@echo "✅ Limpo!"

# Instalar
install: $(TARGET)
	@echo " Instalando em /usr/local/bin..."
	sudo cp $(TARGET) /usr/local/bin/
	sudo chmod +x /usr/local/bin/$(TARGET)

# Ajuda
help:
	@echo "Uso: make [target]"
	@echo ""
	@echo "Targets disponíveis:"
	@echo "  all      - Compilar o servidor (padrão)"
	@echo "  test     - Rodar testes"
	@echo "  run      - Compilar e executar"
	@echo "  debug    - Build com debug symbols"
	@echo "  clean    - Remover arquivos de build"
	@echo "  install  - Instalar em /usr/local/bin"
	@echo "  help     - Mostrar esta ajuda"

-include $(DEPS)