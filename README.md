# HTTP/1.1 Server in C

Servidor HTTP/1.1 multi-threaded escrito em C puro. Implementa o protocolo do zero, sem uso de frameworks, bibliotecas de HTTP ou parsers externos.

## Funcionalidades

- Servidor TCP multi-threaded (POSIX threads).
- Suporte ao método `GET` para arquivos estáticos.
- Virtual Hosts baseados no cabeçalho `Host`.
- Leitura de configuração via arquivo TOML.
- Transferência de arquivos otimizada com `sendfile()` (Linux).
- Proteção contra Directory Traversal (`realpath` + validação de prefixo).
- Tratamento de sinais (`SIGINT`, `SIGTERM`) para encerramento limpo.
- Logs de requisições e erros em `stdout` e `stderr`.

## Estrutura do Projeto

```text
.
├── src/
│   ├── main.c          # Entry point e parse de argumentos
│   ├── config.c / .h   # Parser TOML nativo
│   ├── http.c / .h     # Construção de respostas e I/O de arquivos
│   ├── server.c / .h   # Sockets, accept loop e parsing de requisição
│   ├── logger.c / .h   # Sistema de logs
│   └── utils.c / .h    # Funções auxiliares (url_decode, content-type)
├── tests/
│   └── run_tests.sh    # Script de testes de integração
├── fixtures/           # Arquivos para o avaliador oficial
├── sites/              # Diretórios de exemplo para os sites
├── Makefile            # Build system
├── server.toml         # Configuração padrão
└── README.md
```

## Compilação e Execução

### Pré-requisitos
- Linux x86-64
- GCC ou Clang
- Make
- pthreads (libc)

### Build
```bash
make
```
Isso gera o binário `servidor` com otimizações (`-O2`) e suporte a threads (`-pthread`).

### Execução
```bash
./servidor --config=./server.toml
```

**Saída esperada:**
```text
[INFO] Servidor iniciado em 127.0.0.1:9090
```

Para encerrar, pressione `Ctrl+C` (`SIGINT`) ou envie `SIGTERM`.

## Configuração (`server.toml`)

```toml
[server]
listen = "127.0.0.1"
port = 9090

[[sites]]
host = "alpha.com"
root = "./sites/alpha"

[[sites]]
host = "beta.com"
root = "./sites/beta"
```

## Testes

O projeto inclui testes de integração para verificar a corretude das respostas:

```bash
make test
```

## Detalhes de Implementação

### Parsing de Requisição
O servidor lê do socket em um loop até encontrar o delimitador `\r\n\r\n`, garantindo suporte a pacotes TCP fragmentados. A primeira linha é isolada e validada via `sscanf`. Os cabeçalhos são lidos linha a linha; o servidor exige exatamente um cabeçalho `Host` (retorna `400 Bad Request` se faltar ou se houver duplicidade).

### Segurança de Arquivos
Caminhos solicitados passam por `url_decode`. Sequências contendo `..` são rejeitadas imediatamente com `403 Forbidden`. O caminho final é resolvido com `realpath()` e comparado com o diretório raiz (`root`) do Virtual Host correspondente para garantir que o arquivo esteja estritamente dentro dos limites permitidos.

### I/O e Performance
Arquivos estáticos são servidos utilizando a syscall `sendfile()`, transferindo dados diretamente do descritor de arquivo para o socket no espaço do kernel, evitando cópias desnecessárias para o user space.

### Concorrência
Cada conexão TCP aceita gera uma nova thread (`pthread_create`). A estrutura de configuração é passada por valor para a thread, prevenindo race conditions. As threads são desanexadas (`pthread_detach`) para permitir a liberação automática de recursos ao finalizar.

## Licença

Este projeto foi desenvolvido para fins educacionais e para submissão no Hackathon HTTP/1.1.

**Autor:** DevGuilhrm  
**Linguagem:** C (C11 / POSIX)  
**Environment:** Linux x86-64
```

Feito com empenho e muito café ;)