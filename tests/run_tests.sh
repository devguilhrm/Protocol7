#!/bin/bash

echo "🧪 Iniciando suite de testes do Hackathon HTTP/1.1"
echo "=================================================="

# Iniciar servidor em background COM O ARGUMENTO --config
./servidor --config=server.toml &
SERVER_PID=$!
sleep 2  # Aumentei para 2 segundos para garantir que o servidor subiu

# Teste 1: Site Alpha
echo -n "Teste 1 - Site Alpha... "
RESPONSE=$(curl -s -H "Host: alpha.com" http://127.0.0.1:8080/)
if [[ $RESPONSE == *"Alpha"* ]]; then
    echo "✅ PASS"
else
    echo "❌ FAIL"
fi

# Teste 2: Site Beta
echo -n "Teste 2 - Site Beta... "
RESPONSE=$(curl -s -H "Host: beta.com" http://127.0.0.1:8080/)
if [[ $RESPONSE == *"Beta"* ]]; then
    echo "✅ PASS"
else
    echo "❌ FAIL"
fi

# Teste 3: 404
echo -n "Teste 3 - Host não configurado (404)... "
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" -H "Host: gamma.com" http://127.0.0.1:8080/)
if [[ $HTTP_CODE == "404" ]]; then
    echo "✅ PASS"
else
    echo "❌ FAIL (código: $HTTP_CODE)"
fi

# Teste 4: Directory Traversal
echo -n "Teste 4 - Segurança (Directory Traversal)... "
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" -H "Host: alpha.com" "http://127.0.0.1:8080/../../../etc/passwd")
if [[ $HTTP_CODE == "403" ]]; then
    echo "✅ PASS"
else
    echo "❌ FAIL (código: $HTTP_CODE)"
fi

# Parar servidor
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo "=================================================="
echo "✅ Suite de testes concluída!"