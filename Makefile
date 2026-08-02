CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -D_GNU_SOURCE -Isrc -g
LDFLAGS = -pthread
SRC_DIR = src
OBJ_DIR = obj
TARGET = servidor

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))
DEPS = $(OBJS:.o=.d)

.PHONY: all clean test run debug help

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

test: $(TARGET)
	@echo "🧪 Rodando testes..."
	@chmod +x tests/run_tests.sh
	@./tests/run_tests.sh

run: $(TARGET)
	@echo "🚀 Iniciando servidor..."
	./$(TARGET) --config=server.toml

clean:
	@echo "🧹 Limpando build..."
	rm -rf $(OBJ_DIR) $(TARGET)
	@echo "✅ Limpo!"

help:
	@echo "Targets: all, clean, test, run, debug"

-include $(DEPS)