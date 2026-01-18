#!/bin/bash

# Script de Teste - ESP32 Controle Remoto
# Verifica o código antes de compilar e fazer upload

echo "╔════════════════════════════════════════╗"
echo "║   TESTE DE CÓDIGO - ESP32 Controle     ║"
echo "╚════════════════════════════════════════╝"
echo ""

ERRORS=0
WARNINGS=0

# Cores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Função para verificar se arquivo existe
check_file() {
    if [ ! -f "$1" ]; then
        echo -e "${RED}✗ ERRO: Arquivo não encontrado: $1${NC}"
        ((ERRORS++))
        return 1
    else
        echo -e "${GREEN}✓ Arquivo encontrado: $1${NC}"
        return 0
    fi
}

# Função para verificar padrões no código
check_pattern() {
    local pattern="$1"
    local description="$2"
    local file="$3"
    
    if grep -q "$pattern" "$file"; then
        echo -e "${GREEN}✓ $description${NC}"
        return 0
    else
        echo -e "${YELLOW}⚠ $description (não encontrado)${NC}"
        ((WARNINGS++))
        return 1
    fi
}

# Função para verificar se padrão NÃO existe (erro se existir)
check_not_pattern() {
    local pattern="$1"
    local description="$2"
    local file="$3"
    
    if grep -q "$pattern" "$file"; then
        echo -e "${RED}✗ ERRO: $description${NC}"
        ((ERRORS++))
        return 1
    else
        echo -e "${GREEN}✓ $description${NC}"
        return 0
    fi
}

echo "📁 Verificando arquivos necessários..."
check_file "src/main.cpp"
check_file "platformio.ini"
check_file "ANALISE_ARQUITETURAL.md"
echo ""

echo "🔍 Verificando estrutura do código..."
check_pattern "void setup\\(\\)" "Função setup() encontrada" "src/main.cpp"
check_pattern "void loop\\(\\)" "Função loop() encontrada" "src/main.cpp"
check_pattern "WebServer server" "Servidor Web configurado" "src/main.cpp"
check_pattern "Preferences" "Preferences configurado" "src/main.cpp"
check_pattern "LittleFS" "LittleFS incluído" "src/main.cpp"
echo ""

echo "🔒 Verificando segurança (Fase 1)..."
check_not_pattern "const char\\* ssid = \"Work" "Sem credenciais hardcoded" "src/main.cpp"
check_pattern "saveWiFiCredentials" "Função de salvar credenciais WiFi" "src/main.cpp"
check_pattern "loadWiFiCredentials" "Função de carregar credenciais WiFi" "src/main.cpp"
check_pattern "checkWiFiConnection" "Reconexão WiFi automática" "src/main.cpp"
check_pattern "MAX_CODES" "Validação de limites (MAX_CODES)" "src/main.cpp"
echo ""

echo "⚡ Verificando otimizações (Fase 2)..."
check_pattern "sendJsonError" "Função auxiliar de erro JSON" "src/main.cpp"
check_pattern "sendJsonSuccess" "Função auxiliar de sucesso JSON" "src/main.cpp"
check_pattern "makePrefKey" "Função auxiliar para chaves Preferences" "src/main.cpp"
check_pattern "Serial\\.printf" "Uso de printf (otimização)" "src/main.cpp"
echo ""

echo "🌐 Verificando handlers HTTP..."
check_pattern "handleRoot" "Handler da página principal" "src/main.cpp"
check_pattern "handleStatus" "Handler de status" "src/main.cpp"
check_pattern "handleLearnStart" "Handler de iniciar aprendizado" "src/main.cpp"
check_pattern "handleLearnSave" "Handler de salvar código" "src/main.cpp"
check_pattern "handleCodeSend" "Handler de enviar código" "src/main.cpp"
check_pattern "/api/code/send" "Endpoint de envio de código" "src/main.cpp"
echo ""

echo "📡 Verificando funcionalidades IR..."
check_pattern "IrReceiver" "Receptor IR configurado" "src/main.cpp"
check_pattern "IrSender" "Emissor IR configurado" "src/main.cpp"
check_pattern "IR_RECEIVER_PIN" "Pino do receptor definido" "src/main.cpp"
check_pattern "IR_EMITTER_PIN" "Pino do emissor definido" "src/main.cpp"
echo ""

echo "💾 Verificando persistência..."
check_pattern "saveCodesToPreferences" "Função de salvar códigos" "src/main.cpp"
check_pattern "loadCodesFromPreferences" "Função de carregar códigos" "src/main.cpp"
check_pattern "prefs\\.begin" "Preferences inicializado" "src/main.cpp"
echo ""

echo "🔧 Verificando sintaxe básica..."
# Verificar se há chaves balanceadas (contagem aproximada)
OPEN_BRACES=$(grep -o '{' src/main.cpp | wc -l)
CLOSE_BRACES=$(grep -o '}' src/main.cpp | wc -l)
if [ "$OPEN_BRACES" -eq "$CLOSE_BRACES" ]; then
    echo -e "${GREEN}✓ Chaves balanceadas ($OPEN_BRACES abertas, $CLOSE_BRACES fechadas)${NC}"
else
    echo -e "${RED}✗ ERRO: Chaves desbalanceadas ($OPEN_BRACES abertas, $CLOSE_BRACES fechadas)${NC}"
    ((ERRORS++))
fi

# Verificar se há parênteses balanceados (contagem aproximada)
OPEN_PARENS=$(grep -o '(' src/main.cpp | wc -l)
CLOSE_PARENS=$(grep -o ')' src/main.cpp | wc -l)
if [ "$OPEN_PARENS" -eq "$CLOSE_PARENS" ]; then
    echo -e "${GREEN}✓ Parênteses balanceados ($OPEN_PARENS abertos, $CLOSE_PARENS fechados)${NC}"
else
    echo -e "${YELLOW}⚠ Parênteses podem estar desbalanceados ($OPEN_PARENS abertos, $CLOSE_PARENS fechados)${NC}"
    ((WARNINGS++))
fi
echo ""

echo "════════════════════════════════════════"
echo "📊 RESUMO DO TESTE"
echo "════════════════════════════════════════"

if [ $ERRORS -eq 0 ] && [ $WARNINGS -eq 0 ]; then
    echo -e "${GREEN}✓ TUDO OK! Nenhum erro ou aviso encontrado.${NC}"
    echo -e "${GREEN}✓ Código pronto para compilar e fazer upload.${NC}"
    exit 0
elif [ $ERRORS -eq 0 ]; then
    echo -e "${YELLOW}⚠ $WARNINGS aviso(s) encontrado(s), mas nenhum erro crítico.${NC}"
    echo -e "${GREEN}✓ Código pode ser compilado, mas revise os avisos.${NC}"
    exit 0
else
    echo -e "${RED}✗ $ERRORS erro(s) encontrado(s)!${NC}"
    if [ $WARNINGS -gt 0 ]; then
        echo -e "${YELLOW}⚠ $WARNINGS aviso(s) também encontrado(s).${NC}"
    fi
    echo -e "${RED}✗ Corrija os erros antes de compilar.${NC}"
    exit 1
fi
