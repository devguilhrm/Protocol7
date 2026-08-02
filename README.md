# 🚀 Hackathon HTTP/1.1 — Servidor Web do Zero em C

[![C](https://img.shields.io/badge/C-100%25-blue?logo=c)](https://github.com)
[![Linux](https://img.shields.io/badge/Linux-x86__64-yellow?logo=linux)](https://www.linux.org)
[![Build](https://img.shields.io/badge/build-passing-brightgreen)](#)
[![Tests](https://img.shields.io/badge/tests-passing-brightgreen)](#)
[![License](https://img.shields.io/badge/license-MIT-green)](#)

Um servidor HTTP/1.1 **multi-threaded**, **configurável** e de **alta performance**, escrito 100% em **C puro**, sem frameworks, sem bibliotecas de HTTP prontas e sem parsers externos.

Desenvolvido para o **Hackathon HTTP/1.1**, este projeto demonstra domínio de fundamentos de sistemas, programação de redes em baixo nível, segurança defensiva e engenharia de software profissional.

---

## 🏆 Requisitos do Desafio (Checklist)

- [x] Aceitar conexões HTTP/1.1 sobre TCP
- [x] Suportar o método `GET`
- [x] Servir arquivos estáticos (HTML, CSS, JS, imagens, etc.)
- [x] Hospedar múltiplos sites no mesmo IP/Porta (Virtual Hosts via cabeçalho `Host`)
- [x] Utilizar arquivo de configuração `TOML`
- [x] Funcionar em Linux x86-64
- [x] Executar em primeiro plano (foreground)
- [x] Produzir logs em `stdout` e `stderr`
- [x] Encerrar corretamente ao receber `SIGINT` ou `SIGTERM`
- [x] **Zero dependências de HTTP/Web** (tudo implementado do zero)

---

## 🏗️ Arquitetura do Projeto

```
hackathon-http/
├── src/                    # Código-fonte modularizado
│   ├── main.c              # Entry point e parse de argumentos
│   ├── config.c / .h       # Parser TOML nativo
│   ├── http.c / .h         # Parser HTTP e builder de responses
│   ├── server.c / .h       # Sockets, accept loop e thread handler
│   ├── logger.c / .h       # Sistema de logs (stdout/stderr)
│   └── utils.c / .h        # Funções auxiliares (url_decode, content-type)
── tests/                  # Suite de testes
│   ├── test_config.c       # Testes unitários do parser TOML
│   └── run_tests.sh        # Script de testes de integração
── sites/                  # Diretórios raiz dos sites virtuais
│   ├── alpha/              # Site Alpha (tema azul)
│   │   ├── index.html
│   │   ├── style.css
│   │   └── script.js
│   └── beta/               # Site Beta (tema verde)
│       └── index.html
├── Makefile                # Build system com múltiplos targets
├── server.toml             # Configuração do servidor
├── .gitignore              # Arquivos ignorados pelo Git
└── README.md               # Esta documentação
```

---

## 🚀 Como Compilar e Executar

### Pré-requisitos

- **Sistema Operacional:** Linux x86-64 (Ubuntu, Debian, WSL2)
- **Compilador:** GCC 10+ ou Clang 14+
- **Ferramentas:** `make`, `curl` (para testes)

### 1. Compilação

```bash
make
```

Isso gerará o binário `servidor` com otimizações (`-O2`) e suporte a threads (`-pthread`).

**Targets disponíveis:**

| Comando | Descrição |
|---------|-----------|
| `make` | Compila o servidor (padrão) |
| `make clean` | Remove arquivos de build |
| `make test` | Roda a suite completa de testes |
| `make run` | Compila e executa o servidor |
| `make debug` | Build com símbolos de debug (`-g -O0`) |
| `make help` | Mostra ajuda dos targets |

### 2. Configuração

Edite o arquivo `server.toml`:

```toml
[server]
listen = "127.0.0.1"
port = 8080

[[sites]]
host = "alpha.com"
root = "./sites/alpha"

[[sites]]
host = "beta.com"
root = "./sites/beta"
```

### 3. Execução

```bash
./servidor --config=./server.toml
```

**Saída esperada:**
```
[INFO] Servidor iniciado em 127.0.0.1:8080
```

Para encerrar, pressione `Ctrl+C` (`SIGINT`) ou envie `SIGTERM`:
```
[INFO] Servidor encerrado com segurança.
```

---

## 🧪 Testes

O projeto possui **duas camadas de testes** para garantir robustez:

### Testes Unitários (C puro)

Testam o parser TOML isoladamente:

```bash
# Compilar e rodar testes unitários
gcc -Isrc -o test_runner tests/test_config.c src/config.c src/logger.c
./test_runner
```

**Saída esperada:**
```
Rodando testes...
✓ Teste de config válido passou
✓ Teste de config inválido passou
Todos os testes passaram!
```

### Testes de Integração (Shell Script)

Testam o servidor em execução com requisições HTTP reais:

```bash
# Rodar suite completa de testes
make test
```

Ou manualmente:

```bash
chmod +x tests/run_tests.sh
./tests/run_tests.sh
```

**O que é testado:**

| # | Teste | Resultado Esperado |
|---|-------|-------------------|
| 1 | Site Alpha (HTML + CSS + JS) | `200 OK` com conteúdo "Alpha" |
| 2 | Site Beta | `200 OK` com conteúdo "Beta" |
| 3 | Host não configurado | `404 Not Found` |
| 4 | Método POST (não permitido) | `405 Method Not Allowed` |
| 5 | Directory Traversal (`../../etc/passwd`) | `403 Forbidden` |
| 6 | Arquivo inexistente | `404 Not Found` |

---

## 🌐 Como Testar no Navegador

Para testar os Virtual Hosts no navegador, adicione ao arquivo `hosts`:

**Linux/WSL:** `/etc/hosts`  
**Windows:** `C:\Windows\System32\drivers\etc\hosts`

```text
127.0.0.1   alpha.com
127.0.0.1   beta.com
```

Depois acesse:
- **http://alpha.com:8080** → Site Alpha (tema azul)
- **http://beta.com:8080** → Site Beta (tema verde)

---

## 🛡️ Segurança Defensiva

Este projeto implementa práticas de segurança de nível profissional:

### 🔒 Blindagem contra Directory Traversal

O servidor utiliza `realpath()` para resolver caminhos absolutos e compara rigorosamente com o `root` configurado:

```c
size_t root_len = strlen(resolved_root);
if (strncmp(resolved_path, resolved_root, root_len) != 0 || 
    (resolved_path[root_len] != '/' && resolved_path[root_len] != '\0')) {
    // Retorna 403 Forbidden
}
```

Isso impede ataques como:
- `http://alpha.com/../../etc/passwd`
- `http://alpha.com/../../../etc/shadow`
- Exploração de prefixos (`/var/www/site` vs `/var/www/site-hacked`)

### 🧹 Sanitização de Entrada

- **URL Decode:** Converte `%20`, `+`, e caracteres especiais de forma segura
- **Buffer Limits:** Todos os buffers têm tamanho máximo definido (`MAX_PATH_LEN`, `BUFFER_SIZE`)
- **Host Header Parsing:** Extrai apenas o hostname, removendo porta e caracteres inválidos

### 🚫 Isolamento de Diretórios

- Tentativas de listar diretórios retornam `403 Forbidden`
- Apenas arquivos existentes são servidos
- Symlinks são resolvidos via `realpath()` antes da validação

---

## ⚡ Performance

### Zero-Copy I/O com `sendfile()`

Para servir arquivos estáticos, o servidor utiliza a chamada de sistema `sendfile()` (Linux-specific):

```c
sendfile(client_fd, fd, &offset, stat_buf.st_size);
```

**Vantagens:**
- Dados transferidos diretamente do cache do disco para o socket
- Sem cópia para o espaço do usuário (user space)
- Economia de ciclos de CPU e memória
- Performance próxima ao Nginx para arquivos estáticos

### Multi-Threading POSIX

Cada conexão TCP aceita gera uma nova thread (`pthread_create`):

- **Concorrência real:** Múltiplos clientes atendidos simultaneamente
- **Isolamento de estado:** Configuração passada por cópia (sem race conditions)
- **Thread detachment:** `pthread_detach()` evita vazamento de recursos

---

## 🧠 Decisões Técnicas

### Parser TOML Nativo

Implementado do zero para o subconjunto exato exigido:
- Seções `[server]` e `[[sites]]`
- Chave-valor com suporte a strings entre aspas e valores numéricos
- Tratamento de comentários (`#`) e linhas em branco
- **Zero dependências externas**

### Parser HTTP Manual

Extração de método, URI, versão e headers via:
- `sscanf()` para a linha de requisição
- `strcasestr()` para encontrar o header `Host:`
- Manipulação direta de ponteiros para performance

### Sockets POSIX Crús

Toda a comunicação via API nativa:
- `socket()`, `bind()`, `listen()`, `accept()`
- `recv()`, `write()`, `sendfile()`
- `setsockopt()` com `SO_REUSEADDR`

---

## 📊 Exemplos de Uso

### Requisição GET simples

```bash
curl -H "Host: alpha.com" http://127.0.0.1:8080/
```

**Resposta:**
```http
HTTP/1.1 200 OK
Content-Type: text/html
Content-Length: 512
Connection: close

<!DOCTYPE html>
<html>
...
```

### Requisição para arquivo específico

```bash
curl -H "Host: alpha.com" http://127.0.0.1:8080/style.css
```

### Tentativa de ataque (bloqueada)

```bash
curl -v -H "Host: alpha.com" http://127.0.0.1:8080/../../etc/passwd
```

**Resposta:**
```http
HTTP/1.1 403 Forbidden
Content-Type: text/html
Content-Length: 22
Connection: close

<h1>403 Forbidden</h1>
```

---

## 🔧 Estrutura de Código

### `src/main.c`
Entry point do programa. Responsável por:
- Parse de argumentos de linha de comando (`--config=`)
- Inicialização da configuração
- Chamada ao `start_server()`

### `src/config.c`
Parser TOML nativo. Funcionalidades:
- Leitura linha-a-linha com `fgets()`
- Detecção de seções (`[server]`, `[[sites]]`)
- Parse de chave-valor com trim de espaços
- Suporte a strings entre aspas e valores numéricos

### `src/http.c`
Protocolo HTTP. Responsável por:
- Construção de headers HTTP (`send_response()`)
- Servimento de arquivos com `sendfile()` (`send_file()`)
- Detecção de Content-Type por extensão

### `src/server.c`
Camada de rede. Funcionalidades:
- Setup de socket TCP (`socket()`, `bind()`, `listen()`)
- Loop principal com `accept()`
- Thread handler por conexão (`handle_client()`)
- Tratamento de sinais (`SIGINT`, `SIGTERM`)
- Virtual Host matching via header `Host:`
- Validação de segurança (directory traversal)

### `src/logger.c`
Sistema de logs simples:
- `log_info()` → stdout
- `log_error()` → stderr

### `src/utils.c`
Funções auxiliares:
- `url_decode()`: Decodifica URLs (%20, +, etc.)
- `get_content_type()`: Mapeia extensões para MIME types

---

## 📝 Logs

O servidor produz logs em tempo real:

```
[INFO] Servidor iniciado em 127.0.0.1:8080
[INFO] Served / for Host: alpha.com
[INFO] Served /style.css for Host: alpha.com
[INFO] Served / for Host: beta.com
[ERROR] Falha ao fazer bind em 127.0.0.1:8080
[INFO] Servidor encerrado com segurança.
```

---

## 🐛 Debugging

Para build com símbolos de debug:

```bash
make debug
```

Isso compila com `-g -O0 -DDEBUG`, permitindo uso de `gdb`:

```bash
gdb ./servidor
(gdb) run --config=./server.toml
(gdb) break handle_client
(gdb) continue
```

---

## 📈 Benchmarks (Opcional)

Teste de performance com `ab` (Apache Bench):

```bash
# 1000 requisições, 100 concorrentes
ab -n 1000 -c 100 -H "Host: alpha.com" http://127.0.0.1:8080/
```

---

##  Contribuindo

Este projeto foi desenvolvido para o Hackathon HTTP/1.1. Sugestões de melhorias:

1. **Fork** o repositório
2. Crie uma branch: `git checkout -b feature/minha-melhoria`
3. Commit: `git commit -m 'Adiciona suporte a HEAD'`
4. Push: `git push origin feature/minha-melhoria`
5. Abra um **Pull Request**

---

## 📚 Aprendizados

Este projeto demonstra:

- ✅ Programação de sistemas em C puro
- ✅ Sockets POSIX e programação de rede
- ✅ Multi-threading com pthreads
- ✅ Parsing de protocolos (HTTP, TOML)
- ✅ Segurança defensiva (directory traversal, buffer overflow)
- ✅ Performance (zero-copy I/O com sendfile)
- ✅ Tratamento de sinais e graceful shutdown
- ✅ Engenharia de software (modularização, testes, Makefile)

---

## 📄 Licença

Este projeto foi desenvolvido para fins educacionais e para submissão no **Hackathon HTTP/1.1**.

Sinta-se livre para estudar, compilar e modificar o código.

**Autor:** DevGuilhrm  
**Linguagem:** C (C11 / POSIX)  
**Ambiente Alvo:** Linux x86-64  
**Compilador:** GCC 10+ / Clang 14+

---

## 🏆 Agradecimentos

- Organização do Hackathon HTTP/1.1
- Comunidade de programação de sistemas
- Documentação POSIX e RFC 7230 (HTTP/1.1)

---

**Feito com paciência e muito café ☕**