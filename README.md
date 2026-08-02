# Protocol7

Servidor HTTP/1.1 multi-threaded escrito em C puro, sem frameworks ou bibliotecas externas de HTTP. Implementa o protocolo do zero usando apenas a biblioteca padrão do C e syscalls POSIX.

## Funcionalidades

- Servidor TCP multi-threaded (POSIX threads)
- Suporte ao método GET para arquivos estáticos
- Virtual Hosts baseados no cabeçalho Host
- Configuração via arquivo TOML
- Transferência de arquivos com sendfile() (zero-copy I/O)
- Proteção contra Directory Traversal (realpath + validação de prefixo)
- Tratamento de sinais (SIGINT, SIGTERM) para encerramento limpo
- Timeout de conexão (SO_RCVTIMEO) para prevenir threads ociosas
- Logs em stdout e stderr

## Estrutura do Projeto

```text
.
├── src/
│   ├── main.c          # Entry point e argumentos
│   ├── config.c/h      # Parser TOML nativo
│   ├── server.c/h      # Listener e gerenciamento de sockets
│   ├── worker.c/h      # Handler de conexão e thread logic
│   ├── parse.c/h       # Parsing de protocolo HTTP
│   ├── router.c/h      # Roteamento e resolução de caminhos
│   ├── http.c/h        # Construção de respostas e I/O
│   ├── logger.c/h      # Sistema de logs
│   └── utils.c/h       # Funções auxiliares (url_decode, MIME types)
├── tests/
│   ├── run_tests.sh    # Testes de integração
│   └── test_config.c   # Testes unitários do parser TOML
├── sites/              # Diretórios raiz dos sites virtuais
├── Makefile
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

### Execução
```bash
./servidor --config=server.toml
```

O servidor inicia na porta configurada em `server.toml` (padrão: 9090). Para encerrar, pressione `Ctrl+C` ou envie SIGTERM.

## Configuração (server.toml)

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

### Testes de Integração
```bash
make test
```

Executa testes de integração com curl verificando respostas HTTP, roteamento de Virtual Hosts e proteção contra Directory Traversal.

### Testes Unitários
```bash
gcc -Isrc -o test_runner tests/test_config.c src/config.c src/logger.c
./test_runner
```

Testa o parser TOML isoladamente com arquivos temporários.

### Avaliador Oficial
O projeto passa na suite de conformidade oficial do hackathon (15/15 testes). Para rodar:


1. Iniciar servidor:
```bash
./servidor --config=fixtures/server.toml
```

2. Rodar avaliador (em outro terminal):
```bash
python3 evaluator.py --port 9090
```

## Arquitetura

O projeto segue uma arquitetura em camadas com separação clara de responsabilidades:

**Camada de Transporte (server.c)**
Gerencia sockets TCP, accept loop e criação de threads. Não conhece HTTP nem arquivos.

**Camada de Protocolo (parse.c)**
Transforma bytes brutos do socket em uma struct HttpRequest. Valida headers e extrai método, URI e versão HTTP.

**Camada de Aplicação (router.c)**
Recebe a HttpRequest, valida segurança, encontra o Virtual Host correspondente e resolve o caminho absoluto do arquivo.

**Camada de I/O (http.c, worker.c)**
Constrói respostas HTTP e envia arquivos usando sendfile() para zero-copy I/O.

Essa separação permite modificar cada camada independentemente. Por exemplo, adicionar suporte a HTTP/2 exigiria apenas reescrever o parse.c, sem alterar as outras camadas.

## Decisões Técnicas

**Parsing Manual**
O parser HTTP é implementado do zero usando funções da biblioteca padrão (strstr, sscanf, strncasecmp). Nenhuma biblioteca externa de parsing é utilizada.

**Segurança de Arquivos**
Caminhos solicitados passam por url_decode e são verificados contra sequências "..". O caminho final é resolvido com realpath() e comparado com o diretório raiz do Virtual Host para garantir que o arquivo esteja dentro dos limites permitidos.

**Zero-Copy I/O**
Arquivos estáticos são servidos com sendfile(), transferindo dados diretamente do descritor de arquivo para o socket no espaço do kernel, evitando cópias para user space.

**Timeout de Conexão**
Cada socket de cliente recebe SO_RCVTIMEO de 10 segundos para prevenir threads presas em conexões ociosas ou lentas.

**Thread-per-Connection**
Cada conexão aceita gera uma nova thread (pthread_create). A configuração é passada por valor para evitar race conditions. Threads são desanexadas (pthread_detach) para liberação automática de recursos.

## Licença

Projeto desenvolvido para submissão no Hackathon HTTP/1.1.

**Autor:** DevGuilhrm  
**Linguagem:** C (C11 / POSIX)  
**Ambiente:** Linux x86-64
```