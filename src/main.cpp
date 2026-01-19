#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// ============================================================================
// CONFIGURAÇÕES
// ============================================================================

// Credenciais WiFi agora são armazenadas no Preferences (segurança)
// Se não houver credenciais salvas, o ESP32 cria um Access Point para configuração
const char* AP_SSID = "ESP32-ControleRemoto";
const char* AP_PASSWORD = "";  // Sem senha no AP (configuração inicial)

// Configuração de rede do AP - padrão para Access Points
// O Mac precisa se conectar ao AP do ESP32, então usa faixa padrão 192.168.4.x
const uint8_t AP_IP_OCTET_1 = 192;
const uint8_t AP_IP_OCTET_2 = 168;
const uint8_t AP_IP_OCTET_3 = 4;   // Faixa padrão para APs
const uint8_t AP_IP_OCTET_4 = 1;   // IP do ESP32 no modo AP

const int IR_RECEIVER_PIN = 14;
const int IR_EMITTER_PIN = 2;   // Pino do emissor IR. Alterar aqui ao trocar de GPIO.
const int BUTTON_LEARNING = 32;

// Constantes de validação
const int MAX_CODES = 50;
const int MAX_SSID_LENGTH = 32;
const int MAX_PASSWORD_LENGTH = 64;
const int MAX_DEVICE_NAME = 19;
const int MAX_BUTTON_NAME = 29;

// Define o pino de envio IR antes de incluir a biblioteca
#define IR_SEND_PIN IR_EMITTER_PIN
#include <IRremote.hpp>

// ============================================================================
// STRUCTS E VARIÁVEIS GLOBAIS
// ============================================================================

// Enum de protocolos IR suportados
enum IRProtocol {
  PROTOCOL_UNKNOWN = 0,
  PROTOCOL_NEC = 1,
  PROTOCOL_SAMSUNG = 2,
  PROTOCOL_SONY = 3,
  PROTOCOL_RC5 = 4,
  PROTOCOL_RC6 = 5,
  PROTOCOL_PANASONIC = 6,
  PROTOCOL_LG = 7,
  PROTOCOL_BOSE = 8,  // BoseWave protocol
  PROTOCOL_RAW = 99   // Para protocolos não suportados
};

struct IRCode {
  char device[20];      // Nome do equipamento (ex: "TV Samsung", "AC Daikin")
  char button[30];      // Nome do botão/função (ex: "Power On", "Ligar")
  uint64_t code;        // Código IR raw (para compatibilidade)
  uint8_t bits;         // Número de bits
  IRProtocol protocol;  // Protocolo detectado
  uint16_t address;     // Address (para protocolos que usam)
  uint16_t command;     // Command (para protocolos que usam)
  uint8_t repeats;      // Número de repetições (padrão: 0)
};

IRCode storedCodes[MAX_CODES];
int codeCount = 0;

WebServer server(80);
Preferences prefs;
Preferences wifiPrefs;  // Namespace separado para credenciais WiFi

// Variáveis para gerenciamento WiFi
bool wifiConfigured = false;
unsigned long lastWiFiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 30000;  // Verificar a cada 30 segundos

bool isLearning = false;
uint64_t lastReceivedCode = 0;  // Atualizado para uint64_t
uint8_t lastReceivedBits = 0;
bool codeProcessed = true;  // Flag para rastrear se o código foi processado pela interface

// Variáveis globais para armazenar dados do código capturado
IRProtocol lastReceivedProtocol = PROTOCOL_UNKNOWN;
uint16_t lastReceivedAddress = 0;
uint16_t lastReceivedCommand = 0;

// ============================================================================
// FUNÇÕES - STORAGE / PREFERENCES
// ============================================================================

// Função auxiliar para criar chaves de Preferences sem usar String
void makePrefKey(char* buffer, size_t size, const char* prefix, int index) {
  snprintf(buffer, size, "%s%d", prefix, index);
}

void saveCodesToPreferences() {
  // Validação de segurança: garantir que codeCount está dentro dos limites
  if (codeCount < 0 || codeCount > MAX_CODES) {
    Serial.printf("✗ Erro: codeCount inválido: %d\n", codeCount);
    codeCount = (codeCount < 0) ? 0 : MAX_CODES;
  }
  
  prefs.putInt("count", codeCount);
  
  char keyBuffer[16];  // Buffer reutilizável para chaves
  
  for (int i = 0; i < codeCount && i < MAX_CODES; i++) {
    makePrefKey(keyBuffer, sizeof(keyBuffer), "code", i);
    prefs.putULong64(keyBuffer, storedCodes[i].code);
    
    makePrefKey(keyBuffer, sizeof(keyBuffer), "device", i);
    prefs.putString(keyBuffer, storedCodes[i].device);
    
    makePrefKey(keyBuffer, sizeof(keyBuffer), "button", i);
    prefs.putString(keyBuffer, storedCodes[i].button);
    
    makePrefKey(keyBuffer, sizeof(keyBuffer), "bits", i);
    prefs.putUChar(keyBuffer, storedCodes[i].bits);
    
    // ⭐ NOVO: Salvar protocolo e dados relacionados
    makePrefKey(keyBuffer, sizeof(keyBuffer), "protocol", i);
    prefs.putUChar(keyBuffer, (uint8_t)storedCodes[i].protocol);
    
    makePrefKey(keyBuffer, sizeof(keyBuffer), "address", i);
    prefs.putUShort(keyBuffer, storedCodes[i].address);
    
    makePrefKey(keyBuffer, sizeof(keyBuffer), "command", i);
    prefs.putUShort(keyBuffer, storedCodes[i].command);
    
    makePrefKey(keyBuffer, sizeof(keyBuffer), "repeats", i);
    prefs.putUChar(keyBuffer, storedCodes[i].repeats);
  }
  
  Serial.printf("✓ %d códigos salvos no Preferences\n", codeCount);
}

void loadCodesFromPreferences() {
  prefs.begin("ir-codes", false);  // Namespace "ir-codes", modo leitura/escrita
  
  // ⭐ IMPORTANTE:
  // A limpeza "começar do zero" deve ocorrer APENAS UMA VEZ, senão todo reboot apaga seus códigos.
  // Usamos um schema_version para controlar isso.
  const int CURRENT_SCHEMA_VERSION = 2;
  int schemaVersion = prefs.getInt("schema_version", 0);
  codeCount = prefs.getInt("count", 0);
  
  // Se estamos migrando de um firmware antigo (sem schema_version), limpamos uma única vez.
  if (schemaVersion < CURRENT_SCHEMA_VERSION) {
    Serial.printf("⚠ Migrando storage (schema %d -> %d). Limpando códigos antigos UMA VEZ.\n",
                  schemaVersion, CURRENT_SCHEMA_VERSION);
    prefs.clear();
    prefs.putInt("schema_version", CURRENT_SCHEMA_VERSION);
    prefs.putInt("count", 0);
    codeCount = 0;
    prefs.end();
    return;
  }
  
  // Validação de segurança: garantir limites válidos
  if (codeCount > MAX_CODES || codeCount < 0) {
    Serial.println("⚠ Preferences corrompidos ou vazios, iniciando sem códigos");
    codeCount = 0;
    prefs.end();
    return;
  }
  
  char keyBuffer[16];  // Buffer reutilizável para chaves
  char tempBuffer[64];  // Buffer temporário para strings do Preferences
  
  for (int i = 0; i < codeCount && i < MAX_CODES; i++) {
    makePrefKey(keyBuffer, sizeof(keyBuffer), "code", i);
    storedCodes[i].code = prefs.getULong64(keyBuffer, 0ULL);
    
    makePrefKey(keyBuffer, sizeof(keyBuffer), "device", i);
    size_t len = prefs.getString(keyBuffer, tempBuffer, sizeof(tempBuffer));
    if (len > 0) {
      strncpy(storedCodes[i].device, tempBuffer, MAX_DEVICE_NAME);
      storedCodes[i].device[MAX_DEVICE_NAME] = '\0';
    } else {
      storedCodes[i].device[0] = '\0';
    }
    
    makePrefKey(keyBuffer, sizeof(keyBuffer), "button", i);
    len = prefs.getString(keyBuffer, tempBuffer, sizeof(tempBuffer));
    if (len > 0) {
      strncpy(storedCodes[i].button, tempBuffer, MAX_BUTTON_NAME);
      storedCodes[i].button[MAX_BUTTON_NAME] = '\0';
    } else {
      storedCodes[i].button[0] = '\0';
    }
    
    makePrefKey(keyBuffer, sizeof(keyBuffer), "bits", i);
    storedCodes[i].bits = prefs.getUChar(keyBuffer, 32);
    
    // ⭐ NOVO: Carregar protocolo e dados relacionados
    makePrefKey(keyBuffer, sizeof(keyBuffer), "protocol", i);
    storedCodes[i].protocol = (IRProtocol)prefs.getUChar(keyBuffer, PROTOCOL_UNKNOWN);
    
    makePrefKey(keyBuffer, sizeof(keyBuffer), "address", i);
    storedCodes[i].address = prefs.getUShort(keyBuffer, 0);
    
    makePrefKey(keyBuffer, sizeof(keyBuffer), "command", i);
    storedCodes[i].command = prefs.getUShort(keyBuffer, 0);
    
    makePrefKey(keyBuffer, sizeof(keyBuffer), "repeats", i);
    storedCodes[i].repeats = prefs.getUChar(keyBuffer, 0);
  }
  
  Serial.printf("✓ %d códigos carregados do Preferences\n", codeCount);
}

// ============================================================================
// FUNÇÕES - IR MANAGER
// ============================================================================

// Função auxiliar para obter nome do protocolo (para logs) - declarada antes de usar
const char* getProtocolName(IRProtocol protocol) {
  switch(protocol) {
    case PROTOCOL_NEC: return "NEC";
    case PROTOCOL_SAMSUNG: return "Samsung";
    case PROTOCOL_SONY: return "Sony";
    case PROTOCOL_RC5: return "RC5";
    case PROTOCOL_RC6: return "RC6";
    case PROTOCOL_PANASONIC: return "Panasonic";
    case PROTOCOL_LG: return "LG";
    case PROTOCOL_BOSE: return "Bose";
    case PROTOCOL_RAW: return "RAW";
    default: return "Desconhecido";
  }
}

// Função para converter protocolo da biblioteca para nosso enum
IRProtocol detectProtocol() {
  decode_type_t detected = IrReceiver.decodedIRData.protocol;
  
  switch(detected) {
    case NEC: return PROTOCOL_NEC;
    case SAMSUNG: return PROTOCOL_SAMSUNG;
    case SONY: return PROTOCOL_SONY;
    case RC5: return PROTOCOL_RC5;
    case RC6: return PROTOCOL_RC6;
    case PANASONIC: return PROTOCOL_PANASONIC;
    case LG: return PROTOCOL_LG;
    case BOSEWAVE: return PROTOCOL_BOSE;  // BoseWave protocol
    case UNKNOWN:
    default: return PROTOCOL_UNKNOWN;
  }
}

void handleReceivedIR() {
  // Acessa os dados decodificados da nova API
  lastReceivedCode = IrReceiver.decodedIRData.decodedRawData;
  lastReceivedBits = IrReceiver.decodedIRData.numberOfBits;
  
  // Detectar protocolo automaticamente
  lastReceivedProtocol = detectProtocol();
  
  // Extrair address e command baseado no protocolo
  if (lastReceivedProtocol == PROTOCOL_NEC || lastReceivedProtocol == PROTOCOL_SAMSUNG || 
      lastReceivedProtocol == PROTOCOL_LG || lastReceivedProtocol == PROTOCOL_PANASONIC) {
    // Protocolos que usam address + command
    lastReceivedAddress = IrReceiver.decodedIRData.address;
    lastReceivedCommand = IrReceiver.decodedIRData.command;
  } else if (lastReceivedProtocol == PROTOCOL_SONY) {
    // Sony usa apenas command
    lastReceivedAddress = 0;
    lastReceivedCommand = IrReceiver.decodedIRData.command;
  } else if (lastReceivedProtocol == PROTOCOL_RC5 || lastReceivedProtocol == PROTOCOL_RC6) {
    // RC5/RC6 usam address + command
    lastReceivedAddress = IrReceiver.decodedIRData.address;
    lastReceivedCommand = IrReceiver.decodedIRData.command;
  } else if (lastReceivedProtocol == PROTOCOL_BOSE) {
    // BoseWave usa apenas command (8 bits), sem address
    lastReceivedAddress = 0;
    lastReceivedCommand = IrReceiver.decodedIRData.command;
  } else {
    // Protocolo desconhecido - tentar extrair do código raw
    lastReceivedAddress = (lastReceivedCode >> 16) & 0xFFFF;
    lastReceivedCommand = lastReceivedCode & 0xFFFF;
  }
  
  // ⭐ FILTRO 0x0 - Ignorar ruído IR antes de processar
  if (lastReceivedCode == 0ULL || lastReceivedCode == 0xFFFFFFFFFFFFFFFFULL) {
    Serial.printf("⚠ Código inválido ignorado: 0x%llX\n", lastReceivedCode);
    return;
  }
  
  if (isLearning) {
    codeProcessed = false;  // Marca como não processado para a interface detectar
    const char* protocolName = getProtocolName(lastReceivedProtocol);
    Serial.printf("📥 Código recebido (Modo Aprendizado): Protocolo=%s\n", protocolName);
    Serial.printf("   Raw: 0x%llX, Bits: %d\n", lastReceivedCode, lastReceivedBits);
    Serial.printf("   Address: 0x%04X, Command: 0x%04X\n", lastReceivedAddress, lastReceivedCommand);
    Serial.printf("   decodedIRData.address: 0x%04X, decodedIRData.command: 0x%04X\n",
                  IrReceiver.decodedIRData.address, IrReceiver.decodedIRData.command);
  } else {
    Serial.printf("📥 Código recebido: 0x%llX (%d bits)\n", lastReceivedCode, lastReceivedBits);
  }
}

// Função unificada para enviar código IR baseado no protocolo
bool sendIRCode(const IRCode& code) {
  const char* protocolName = getProtocolName(code.protocol);
  Serial.printf("📤 Enviando código IR: %s - %s (Protocolo: %s)\n", 
                code.device, code.button, protocolName);
  Serial.printf("   Detalhes: address=0x%04X, command=0x%04X, bits=%d, repeats=%d\n",
                code.address, code.command, code.bits, code.repeats);
  
  switch(code.protocol) {
    case PROTOCOL_NEC:
      Serial.printf("   → Chamando sendNEC(0x%04X, 0x%04X, %d)\n", code.address, code.command, code.repeats);
      IrSender.sendNEC(code.address, code.command, code.repeats);
      return true;
      
    case PROTOCOL_SAMSUNG: {
      Serial.printf("   → Chamando sendSamsung(0x%04X, 0x%04X, %d)\n", code.address, code.command, code.repeats);
      // Samsung pode precisar de repetições para funcionar corretamente
      // Tentar com 1 repetição se repeats for 0
      uint8_t samsungRepeats = (code.repeats == 0) ? 1 : code.repeats;
      IrSender.sendSamsung(code.address, code.command, samsungRepeats);
      Serial.printf("   ✓ Código Samsung enviado com %d repetição(ões)\n", samsungRepeats);
      return true;
    }
      
    case PROTOCOL_SONY:
      IrSender.sendSony(code.command, code.bits, code.repeats);
      return true;
      
    case PROTOCOL_RC5:
      IrSender.sendRC5(code.address, code.command, code.repeats);
      return true;
      
    case PROTOCOL_RC6:
      IrSender.sendRC6(code.address, code.command, code.repeats);
      return true;
      
    case PROTOCOL_PANASONIC:
      IrSender.sendPanasonic(code.address, code.command, code.repeats);
      return true;
      
    case PROTOCOL_LG:
      IrSender.sendLG(code.address, code.command, code.repeats);
      return true;
      
    case PROTOCOL_BOSE:
      // BoseWave usa apenas command (8 bits), sem address
      // sendBoseWave(uint8_t aCommand, int_fast8_t aNumberOfRepeats)
      IrSender.sendBoseWave((uint8_t)code.command, code.repeats);
      return true;
      
    case PROTOCOL_UNKNOWN:
    default:
      Serial.printf("⚠ Protocolo não suportado: %d, tentando NEC como fallback\n", code.protocol);
      // Fallback: tentar NEC (compatibilidade)
      if (code.bits == 32 || code.bits == 0) {
        IrSender.sendNEC(code.address, code.command, code.repeats);
        return true;
      }
      return false;
  }
  return false;  // Fallback final
}

uint64_t findCode(const char* device, const char* button) {
  // Validação de segurança: verificar limites
  if (!device || !button || codeCount < 0 || codeCount > MAX_CODES) {
    return 0ULL;
  }
  
  for (int i = 0; i < codeCount && i < MAX_CODES; i++) {
    if (strcmp(storedCodes[i].device, device) == 0 &&
        strcmp(storedCodes[i].button, button) == 0) {
      return storedCodes[i].code;
    }
  }
  return 0ULL;
}

int findCodeIndex(const char* device, const char* button) {
  // Validação de segurança: verificar limites
  if (!device || !button || codeCount < 0 || codeCount > MAX_CODES) {
    return -1;
  }
  
  for (int i = 0; i < codeCount && i < MAX_CODES; i++) {
    if (strcmp(storedCodes[i].device, device) == 0 &&
        strcmp(storedCodes[i].button, button) == 0) {
      return i;
    }
  }
  return -1;
}

void toggleLearningMode() {
  isLearning = !isLearning;
  
  if (isLearning) {
    Serial.println("✓ Modo aprendizado ATIVADO (via botão físico)");
  } else {
    Serial.println("✗ Modo aprendizado DESATIVADO");
  }
}

// ============================================================================
// FUNÇÕES - CONFIG / WIFI
// ============================================================================

// Salvar credenciais WiFi no Preferences
void saveWiFiCredentials(const char* ssid, const char* password) {
  wifiPrefs.begin("wifi-config", false);
  wifiPrefs.putString("ssid", ssid);
  wifiPrefs.putString("password", password);
  wifiPrefs.putBool("configured", true);
  wifiPrefs.end();
  Serial.println("✓ Credenciais WiFi salvas");
}

// Carregar credenciais WiFi do Preferences
bool loadWiFiCredentials(char* ssid, char* password, int maxLen) {
  wifiPrefs.begin("wifi-config", true);
  bool configured = wifiPrefs.getBool("configured", false);
  
  if (configured) {
    String ssidStr = wifiPrefs.getString("ssid", "");
    String passStr = wifiPrefs.getString("password", "");
    
    if (ssidStr.length() > 0 && ssidStr.length() < maxLen) {
      strncpy(ssid, ssidStr.c_str(), maxLen - 1);
      ssid[maxLen - 1] = '\0';
      
      if (passStr.length() < maxLen) {
        strncpy(password, passStr.c_str(), maxLen - 1);
        password[maxLen - 1] = '\0';
      } else {
        password[0] = '\0';
      }
      
      wifiPrefs.end();
      return true;
    }
  }
  
  wifiPrefs.end();
  return false;
}

// Criar Access Point para configuração inicial
void startConfigAP() {
  // Quando em modo AP, o ESP32 cria sua própria rede isolada
  // O gateway deve ser o próprio IP do ESP32 (não o gateway do roteador)
  IPAddress AP_IP(AP_IP_OCTET_1, AP_IP_OCTET_2, AP_IP_OCTET_3, AP_IP_OCTET_4);
  IPAddress gateway(AP_IP);  // Gateway é o próprio ESP32 quando em modo AP
  IPAddress subnet(255, 255, 255, 0);
  
  Serial.println("\n📡 Modo de Configuração - Access Point");
  Serial.println("  SSID: " + String(AP_SSID));
  Serial.println("  IP Configurado: " + AP_IP.toString());
  Serial.println("  Gateway: " + gateway.toString() + " (próprio ESP32)");
  Serial.println("  Subnet: " + subnet.toString());
  Serial.println("  ⚠ IMPORTANTE: Conecte seu Mac ao WiFi 'ESP32-ControleRemoto' primeiro!");
  Serial.println("  Depois acesse: http://" + AP_IP.toString() + "/config");
  
  WiFi.mode(WIFI_AP);
  delay(200);
  
  bool apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD);
  if (!apStarted) {
    Serial.println("  ✗ ERRO: Falha ao iniciar Access Point!");
    Serial.println("  Status: " + String(WiFi.status()));
    return;
  }
  
  delay(200);
  
  bool configOk = WiFi.softAPConfig(AP_IP, gateway, subnet);
  if (!configOk) {
    Serial.println("  ⚠ Aviso: softAPConfig retornou false, mas continuando...");
  }
  
  delay(200);
  
  IPAddress actualIP = WiFi.softAPIP();
  Serial.println("\n  ✓ AP iniciado com sucesso!");
  Serial.println("  ✓ IP Real do AP: " + actualIP.toString());
  Serial.println("  ✓ MAC do AP: " + WiFi.softAPmacAddress());
  Serial.println("  ✓ Clientes conectados: " + String(WiFi.softAPgetStationNum()));
  
  // Verificar se o IP está correto
  if (actualIP != AP_IP) {
    Serial.println("  ⚠ ATENÇÃO: IP real (" + actualIP.toString() + ") difere do configurado (" + AP_IP.toString() + ")");
    Serial.println("  Use o IP real para acessar: http://" + actualIP.toString() + "/config");
  }
  Serial.println("");
}

// Conectar ao WiFi usando credenciais salvas
bool connectToWiFi(const char* ssid, const char* password, bool showProgress = true) {
  // Desabilitar AP temporariamente para melhor conexão
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  WiFi.begin(ssid, password);
  
  if (showProgress) {
    Serial.println("\n✓ Conectando na rede WiFi...");
    Serial.println("  SSID: " + String(ssid));
    Serial.println("  Aguarde (pode levar até 30 segundos)...");
  }
  
  int attempts = 0;
  const int maxAttempts = 60;  // Aumentado para 30 segundos (60 * 500ms)
  
  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(500);
    if (showProgress && attempts % 4 == 0) {
      Serial.print(".");
    }
    attempts++;
    yield();  // Permitir que outras tarefas rodem
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    if (showProgress) {
      Serial.println("\n✓ Conectado com sucesso!");
      Serial.println("  IP: " + WiFi.localIP().toString());
      Serial.println("  MAC: " + WiFi.macAddress());
      Serial.println("  RSSI: " + String(WiFi.RSSI()) + " dBm");
      Serial.println("  Acesse: http://" + WiFi.localIP().toString() + " no navegador\n");
    }
    wifiConfigured = true;
    return true;
  } else {
    if (showProgress) {
      Serial.println("\n✗ Falha ao conectar WiFi!");
      Serial.println("  Status: " + String(WiFi.status()));
      Serial.println("  Verifique SSID e Senha");
      Serial.println("  Acesse http://192.168.4.1/config para reconfigurar\n");
    }
    wifiConfigured = false;
    return false;
  }
}

// Configurar WiFi (tenta carregar credenciais ou cria AP)
void setupWiFi() {
  char ssid[MAX_SSID_LENGTH + 1];
  char password[MAX_PASSWORD_LENGTH + 1];
  
  // Tentar carregar credenciais salvas
  if (loadWiFiCredentials(ssid, password, MAX_SSID_LENGTH + 1)) {
    Serial.println("📡 Credenciais WiFi encontradas, tentando conectar...");
    // Tentar conectar
    if (connectToWiFi(ssid, password)) {
      // Conectado com sucesso - criar AP também para permitir reconfiguração
      Serial.println("📡 Iniciando modo híbrido (STA + AP) para permitir reconfiguração...");
      WiFi.mode(WIFI_AP_STA);  // Modo híbrido
      delay(100);
      WiFi.softAP(AP_SSID, AP_PASSWORD);
      IPAddress AP_IP(AP_IP_OCTET_1, AP_IP_OCTET_2, AP_IP_OCTET_3, AP_IP_OCTET_4);
      IPAddress gateway(AP_IP_OCTET_1, AP_IP_OCTET_2, AP_IP_OCTET_3, 1);
      IPAddress subnet(255, 255, 255, 0);
      delay(100);
      WiFi.softAPConfig(AP_IP, gateway, subnet);
      delay(100);
      Serial.println("  ✓ Modo híbrido ativo - AP disponível em " + WiFi.softAPIP().toString());
      Serial.println("  ✓ WiFi conectado em: " + WiFi.localIP().toString() + "\n");
      return;
    }
    // Se falhar, continuar para criar AP
    Serial.println("⚠ Falha na conexão, iniciando modo AP para configuração...");
  } else {
    Serial.println("📡 Nenhuma credencial WiFi encontrada, iniciando modo AP...");
  }
  
  // Se não houver credenciais ou conexão falhou, criar AP
  startConfigAP();
  wifiConfigured = false;
}

// Verificar e reconectar WiFi se necessário
void checkWiFiConnection() {
  unsigned long now = millis();
  
  // Verificar apenas a cada intervalo definido
  if (now - lastWiFiCheck < WIFI_CHECK_INTERVAL) {
    return;
  }
  
  lastWiFiCheck = now;
  
  // Se não está conectado e deveria estar
  if (WiFi.status() != WL_CONNECTED && wifiConfigured) {
    Serial.println("⚠ WiFi desconectado, tentando reconectar...");
    
    char ssid[MAX_SSID_LENGTH + 1];
    char password[MAX_PASSWORD_LENGTH + 1];
    
    if (loadWiFiCredentials(ssid, password, MAX_SSID_LENGTH + 1)) {
      // Se estiver em modo AP, manter modo híbrido
      if (WiFi.getMode() == WIFI_AP) {
        WiFi.mode(WIFI_AP_STA);
        delay(100);
        WiFi.softAP(AP_SSID, AP_PASSWORD);
      }
      WiFi.disconnect();
      delay(100);
      connectToWiFi(ssid, password, false);  // Reconexão silenciosa
      
      // Se reconectou, garantir modo híbrido
      if (WiFi.status() == WL_CONNECTED && WiFi.getMode() != WIFI_AP_STA) {
        WiFi.mode(WIFI_AP_STA);
        delay(100);
        WiFi.softAP(AP_SSID, AP_PASSWORD);
      }
    }
  }
}

// ============================================================================
// FUNÇÕES AUXILIARES - TRATAMENTO DE ERROS E RESPOSTAS JSON
// ============================================================================

// Função auxiliar para enviar resposta JSON de erro padronizada
void sendJsonError(int code, const char* message) {
  char buffer[200];
  snprintf(buffer, sizeof(buffer), "{\"status\":\"error\",\"message\":\"%s\"}", message);
  server.send(code, "application/json", buffer);
}

// Função auxiliar para enviar resposta JSON de sucesso padronizada
void sendJsonSuccess(const char* message = "success") {
  char buffer[150];
  snprintf(buffer, sizeof(buffer), "{\"status\":\"success\",\"message\":\"%s\"}", message);
  server.send(200, "application/json", buffer);
}

// ============================================================================
// HANDLERS HTTP
// ============================================================================

void handleRoot() {
  String html = R"(
<!DOCTYPE html>
<html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width,initial-scale=1'>
  <title>Controle Remoto ESP32</title>
  <style>
    * { box-sizing: border-box; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Arial, sans-serif;
      max-width: 600px;
      margin: 20px auto;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      padding: 10px;
    }
    .container {
      background: white;
      border-radius: 15px;
      padding: 25px;
      box-shadow: 0 10px 40px rgba(0,0,0,0.2);
    }
    h1 {
      color: #333;
      text-align: center;
      margin-bottom: 10px;
    }
    .subtitle {
      text-align: center;
      color: #666;
      font-size: 14px;
      margin-bottom: 25px;
    }
    .btn-grid {
      display: grid;
      gap: 12px;
      margin-bottom: 20px;
    }
    .device-btn {
      width: 100%;
      padding: 16px;
      font-size: 16px;
      font-weight: 500;
      border: none;
      border-radius: 8px;
      cursor: pointer;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      color: white;
      transition: all 0.3s;
      box-shadow: 0 4px 15px rgba(102, 126, 234, 0.4);
    }
    .device-btn:hover {
      transform: translateY(-2px);
      box-shadow: 0 6px 20px rgba(102, 126, 234, 0.6);
    }
    .device-btn:active {
      transform: translateY(0);
    }
    .device-btn:disabled {
      opacity: 0.5;
      cursor: not-allowed;
    }
    .controls {
      display: flex;
      gap: 10px;
      margin-bottom: 20px;
    }
    .btn-control {
      flex: 1;
      padding: 12px;
      font-size: 14px;
      border: none;
      border-radius: 8px;
      cursor: pointer;
      transition: 0.3s;
    }
    .btn-learn {
      background: #28a745;
      color: white;
    }
    .btn-learn:hover { background: #218838; }
    .btn-learn.active {
      background: #dc3545;
    }
    .btn-refresh {
      background: #17a2b8;
      color: white;
    }
    .btn-refresh:hover { background: #138496; }
    .api-status {
      text-align: center;
      padding: 15px;
      background: #f8f9fa;
      border-radius: 8px;
      margin-top: 15px;
    }
    .status-text {
      font-weight: 500;
      color: #333;
    }
    .status-text.success { color: #28a745; }
    .status-text.error { color: #dc3545; }
    .loading {
      text-align: center;
      color: #666;
      padding: 20px;
    }
    .empty-state {
      text-align: center;
      padding: 40px 20px;
      color: #999;
    }
    .empty-state h3 {
      color: #666;
      margin-bottom: 10px;
    }
    .device-btn:active {
      transform: scale(0.98);
      box-shadow: 0 2px 10px rgba(102, 126, 234, 0.4);
    }
    .modal {
      display: none;
      position: fixed;
      z-index: 1000;
      left: 0;
      top: 0;
      width: 100%;
      height: 100%;
      background-color: rgba(0,0,0,0.5);
      animation: fadeIn 0.3s;
    }
    .modal-content {
      background-color: white;
      margin: 15% auto;
      padding: 25px;
      border-radius: 15px;
      max-width: 400px;
      box-shadow: 0 10px 40px rgba(0,0,0,0.3);
      animation: slideDown 0.3s;
    }
    @keyframes fadeIn {
      from { opacity: 0; }
      to { opacity: 1; }
    }
    @keyframes slideDown {
      from { transform: translateY(-50px); opacity: 0; }
      to { transform: translateY(0); opacity: 1; }
    }
    .modal-header {
      font-size: 20px;
      font-weight: bold;
      margin-bottom: 15px;
      color: #333;
    }
    .modal-body {
      margin-bottom: 20px;
    }
    .modal-input {
      width: 100%;
      padding: 12px;
      font-size: 16px;
      border: 2px solid #ddd;
      border-radius: 8px;
      margin-top: 10px;
      box-sizing: border-box;
    }
    .modal-input:focus {
      outline: none;
      border-color: #667eea;
    }
    .modal-buttons {
      display: flex;
      gap: 10px;
      justify-content: flex-end;
    }
    .modal-btn {
      padding: 10px 20px;
      border: none;
      border-radius: 8px;
      cursor: pointer;
      font-size: 14px;
      font-weight: 500;
      transition: 0.3s;
    }
    .modal-btn-primary {
      background: #667eea;
      color: white;
    }
    .modal-btn-primary:hover {
      background: #5568d3;
    }
    .modal-btn-secondary {
      background: #6c757d;
      color: white;
    }
    .modal-btn-secondary:hover {
      background: #5a6268;
    }
    .code-display {
      background: #f8f9fa;
      padding: 10px;
      border-radius: 5px;
      font-family: monospace;
      font-size: 14px;
      color: #666;
      margin-top: 10px;
    }
  </style>
</head>
<body>
  <div class='container'>
    <h1>🎮 Controle Remoto</h1>
    <div class='subtitle'>ESP32 IR Controller</div>
    
    <div class='controls'>
      <button class='btn-control btn-learn' id='btn-learn' onclick='toggleLearn()'>
        📥 Modo Aprendizado
      </button>
      <button class='btn-control btn-refresh' onclick='loadCodes()'>
        🔄 Atualizar
      </button>
    </div>
    
    <div id='btn-grid' class='btn-grid'>
      <div class='loading'>Carregando códigos...</div>
    </div>
    
    <div class='api-status'>
      <p class='status-text' id='api-status'>Pronto</p>
    </div>
  </div>
  
  <!-- Modal para nomear código capturado -->
  <div id='codeModal' class='modal'>
    <div class='modal-content'>
      <div class='modal-header'>📥 Código IR Capturado!</div>
      <div class='modal-body'>
        <p>Um código IR foi detectado. Preencha os dados abaixo:</p>
        <div class='code-display' id='capturedCode'>0x00000000</div>
        <div id='capturedProtocol' style='margin-top: 5px; font-size: 12px; color: #666;'></div>
        <label style='display: block; margin-top: 15px; margin-bottom: 5px; font-weight: 500;'>Nome do Equipamento:</label>
        <input type='text' id='deviceName' class='modal-input' placeholder='Ex: TV Samsung, AC Daikin' maxlength='19' autofocus>
        <label style='display: block; margin-top: 15px; margin-bottom: 5px; font-weight: 500;'>Nome do Botão/Função:</label>
        <input type='text' id='buttonName' class='modal-input' placeholder='Ex: Power On, Ligar, Temp+' maxlength='29'>
      </div>
      <div class='modal-buttons'>
        <button class='modal-btn modal-btn-secondary' onclick='cancelSaveCode()'>Cancelar</button>
        <button class='modal-btn modal-btn-primary' onclick='saveCapturedCode()'>Salvar</button>
      </div>
    </div>
  </div>
  
  <!-- Modal para editar código -->
  <div id='editModal' class='modal'>
    <div class='modal-content'>
      <div class='modal-header'>✏️ Editar Código</div>
      <div class='modal-body'>
        <label style='display: block; margin-bottom: 5px; font-weight: 500;'>Nome do Equipamento:</label>
        <input type='text' id='editDeviceName' class='modal-input' maxlength='19'>
        <label style='display: block; margin-top: 15px; margin-bottom: 5px; font-weight: 500;'>Nome do Botão/Função:</label>
        <input type='text' id='editButtonName' class='modal-input' maxlength='29'>
        <input type='hidden' id='editCodeId'>
      </div>
      <div class='modal-buttons'>
        <button class='modal-btn modal-btn-secondary' onclick='closeEditModal()'>Cancelar</button>
        <button class='modal-btn modal-btn-primary' onclick='saveEditedCode()'>Salvar</button>
      </div>
    </div>
  </div>
  
  <script>
    let learnMode = false;
    let currentCodesCount = 0;
    let codesMap = new Map(); // Mapa para rastrear códigos existentes
    
    function updateStatus(text, isSuccess = true) {
      const statusEl = document.getElementById('api-status');
      statusEl.textContent = text;
      statusEl.className = 'status-text ' + (isSuccess ? 'success' : 'error');
      setTimeout(() => {
        statusEl.textContent = 'Pronto';
        statusEl.className = 'status-text';
      }, 3000);
    }
    
    function sendCode(id) {
      // Encontrar o botão que foi clicado para feedback visual
      const btn = document.getElementById('code-btn-' + id);
      const originalText = btn ? btn.textContent : '';
      
      updateStatus('📤 Enviando código IR...', true);
      
      fetch('/api/code/send', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ id: id })
      })
      .then(r => r.json())
      .then(data => {
        if (data.status === 'success') {
          updateStatus('✓ Código IR enviado com sucesso! Aponte o controle para o dispositivo.', true);
          // Feedback visual no botão
          if (btn) {
            const tempText = btn.textContent;
            btn.textContent = '✓ Enviado!';
            btn.style.background = 'linear-gradient(135deg, #28a745 0%, #20c997 100%)';
            setTimeout(() => {
              btn.textContent = originalText;
              btn.style.background = 'linear-gradient(135deg, #667eea 0%, #764ba2 100%)';
            }, 2000);
          }
        } else {
          updateStatus('✗ Erro ao enviar código: ' + (data.message || 'erro desconhecido'), false);
        }
      })
      .catch(e => {
        updateStatus('✗ Erro de conexão ao enviar código', false);
      });
    }
    
    let capturedCodeData = null;
    let learnPollInterval = null;
    
    function toggleLearn() {
      const endpoint = learnMode ? '/api/learn/stop' : '/api/learn/start';
      const btn = document.getElementById('btn-learn');
      
      fetch(endpoint, { method: 'POST' })
        .then(r => r.json())
        .then(data => {
          learnMode = !learnMode;
          btn.textContent = learnMode ? '⏹ Parar Aprendizado' : '📥 Modo Aprendizado';
          btn.classList.toggle('active', learnMode);
          updateStatus(learnMode ? 'Modo aprendizado ATIVADO - Aponte o controle e pressione um botão' : 'Modo aprendizado DESATIVADO', true);
          
          // Iniciar/parar polling de códigos capturados
          if (learnMode) {
            startLearnPolling();
          } else {
            stopLearnPolling();
            closeModal();
          }
        })
        .catch(e => {
          updateStatus('✗ Erro ao alterar modo', false);
        });
    }
    
    function startLearnPolling() {
      // Verifica códigos capturados a cada 500ms quando em modo aprendizado
      learnPollInterval = setInterval(() => {
        if (!learnMode) {
          stopLearnPolling();
          return;
        }
        
        fetch('/api/learn/captured')
          .then(r => r.json())
          .then(data => {
            const modal = document.getElementById('codeModal');
            if (data.captured && (modal.style.display === 'none' || !modal.style.display)) {
              // Novo código capturado - mostrar modal
              capturedCodeData = data;
              showCodeModal(data.code_hex);
            }
          })
          .catch(() => {}); // Ignorar erros silenciosamente
      }, 500);
    }
    
    function stopLearnPolling() {
      if (learnPollInterval) {
        clearInterval(learnPollInterval);
        learnPollInterval = null;
      }
    }
    
    function showCodeModal(codeHex) {
      const modal = document.getElementById('codeModal');
      const codeDisplay = document.getElementById('capturedCode');
      const protocolDisplay = document.getElementById('capturedProtocol');
      const deviceInput = document.getElementById('deviceName');
      const buttonInput = document.getElementById('buttonName');
      
      codeDisplay.textContent = codeHex;
      if (capturedCodeData && capturedCodeData.protocol) {
        protocolDisplay.textContent = 'Protocolo detectado: ' + capturedCodeData.protocol;
      } else {
        protocolDisplay.textContent = '';
      }
      deviceInput.value = '';
      buttonInput.value = '';
      modal.style.display = 'block';
      deviceInput.focus();
    }
    
    function closeModal() {
      const modal = document.getElementById('codeModal');
      modal.style.display = 'none';
      capturedCodeData = null;
    }
    
    function saveCapturedCode() {
      const deviceInput = document.getElementById('deviceName');
      const buttonInput = document.getElementById('buttonName');
      const device = deviceInput.value.trim();
      const button = buttonInput.value.trim();
      
      if (!device) {
        alert('Por favor, digite o nome do equipamento');
        deviceInput.focus();
        return;
      }
      
      if (!button) {
        alert('Por favor, digite o nome do botão/função');
        buttonInput.focus();
        return;
      }
      
      if (!capturedCodeData) {
        alert('Erro: código não encontrado');
        closeModal();
        return;
      }
      
      fetch('/api/learn/save', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ device: device, button: button })
      })
      .then(r => {
        if (!r.ok) {
          return r.json().then(err => {
            throw new Error(err.message || 'Erro HTTP ' + r.status);
          });
        }
        return r.json();
      })
      .then(data => {
        if (data.status === 'success') {
          updateStatus('✓ Código salvo: ' + device + ' - ' + button, true);
          closeModal();
          loadCodes();
          if (learnMode) {
            updateStatus('Modo aprendizado ATIVO - Aponte o controle e pressione outro botão', true);
          }
        } else {
          alert('Erro ao salvar código: ' + (data.message || 'Erro desconhecido'));
        }
      })
      .catch(e => {
        alert('Erro ao salvar: ' + e.message);
      });
    }
    
    function cancelSaveCode() {
      closeModal();
      // Continuar modo aprendizado mesmo após cancelar
      if (learnMode) {
        updateStatus('Modo aprendizado ATIVO - Aponte o controle e pressione um botão', true);
      }
    }
    
    // Fechar modal ao clicar fora
    window.onclick = function(event) {
      const modal = document.getElementById('codeModal');
      if (event.target === modal) {
        cancelSaveCode();
      }
    }
    
    // Salvar ao pressionar Enter nos inputs do modal de captura
    document.addEventListener('keypress', function(e) {
      if ((e.target.id === 'deviceName' || e.target.id === 'buttonName') && e.key === 'Enter') {
        saveCapturedCode();
      }
    });
    
    // Função para criar botão de código com ações
    function createCodeButton(code) {
      const container = document.createElement('div');
      container.style.display = 'flex';
      container.style.gap = '8px';
      container.style.alignItems = 'center';
      
      const btn = document.createElement('button');
      btn.className = 'device-btn';
      btn.id = 'code-btn-' + code.id;
      btn.style.flex = '1';
      btn.textContent = '📤 ' + (code.device || 'Equipamento') + ' - ' + (code.button || 'Botão');
      btn.title = 'Clique para ENVIAR este código IR';
      btn.onclick = () => {
        btn.style.opacity = '0.6';
        btn.disabled = true;
        sendCode(code.id);
        setTimeout(() => {
          btn.style.opacity = '1';
          btn.disabled = false;
        }, 500);
      };
      
      const editBtn = document.createElement('button');
      editBtn.textContent = '✏️';
      editBtn.title = 'Editar';
      editBtn.style.cssText = 'padding: 8px 12px; border: none; border-radius: 6px; background: #17a2b8; color: white; cursor: pointer; font-size: 14px;';
      editBtn.onclick = (e) => { e.stopPropagation(); editCode(code.id); };
      
      const deleteBtn = document.createElement('button');
      deleteBtn.textContent = '🗑️';
      deleteBtn.title = 'Deletar';
      deleteBtn.style.cssText = 'padding: 8px 12px; border: none; border-radius: 6px; background: #dc3545; color: white; cursor: pointer; font-size: 14px;';
      deleteBtn.onclick = (e) => { e.stopPropagation(); deleteCode(code.id); };
      
      container.appendChild(btn);
      container.appendChild(editBtn);
      container.appendChild(deleteBtn);
      
      return container;
    }
    
    // Atualização inteligente - só modifica o que mudou
    function updateCodesList(codes) {
      const grid = document.getElementById('btn-grid');
      
      // Se não há códigos, mostrar estado vazio
      if (codes.length === 0) {
        if (currentCodesCount > 0) {
          grid.innerHTML = '<div class="empty-state"><h3>Nenhum código salvo</h3><p>Ative o modo aprendizado e capture códigos IR</p></div>';
          currentCodesCount = 0;
          codesMap.clear();
        }
        return;
      }
      
      // Se é a primeira carga ou número de códigos mudou, recriar tudo
      if (currentCodesCount === 0 || currentCodesCount !== codes.length) {
        grid.innerHTML = '';
        codesMap.clear();
        codes.forEach(code => {
          const btn = createCodeButton(code);
          grid.appendChild(btn);
          codesMap.set(code.id, code);
        });
        currentCodesCount = codes.length;
        updateStatus('✓ ' + codes.length + ' códigos carregados', true);
        return;
      }
      
      // Se o número é o mesmo, verificar se há novos códigos
      let hasNewCodes = false;
      codes.forEach(code => {
        if (!codesMap.has(code.id)) {
          // Novo código encontrado - adicionar no final com animação suave
          const btn = createCodeButton(code);
          btn.style.opacity = '0';
          btn.style.transform = 'translateY(-10px)';
          grid.appendChild(btn);
          codesMap.set(code.id, code);
          hasNewCodes = true;
          
          // Animação de entrada
          setTimeout(() => {
            btn.style.transition = 'all 0.3s ease';
            btn.style.opacity = '1';
            btn.style.transform = 'translateY(0)';
          }, 10);
        }
      });
      
      if (hasNewCodes) {
        currentCodesCount = codes.length;
        updateStatus('✓ Novo código adicionado!', true);
      }
    }
    
    function loadCodes(showLoading = false) {
      const grid = document.getElementById('btn-grid');
      
      if (showLoading && currentCodesCount === 0) {
        grid.innerHTML = '<div class="loading">Carregando códigos...</div>';
      }
      
      fetch('/api/codes')
        .then(r => r.json())
        .then(codes => {
          updateCodesList(codes);
        })
        .catch(e => {
          if (currentCodesCount === 0) {
            grid.innerHTML = '<div class="empty-state"><h3>Erro ao carregar códigos</h3></div>';
          }
          updateStatus('✗ Erro ao carregar', false);
        });
    }
    
    // Carregar códigos ao iniciar
    loadCodes(true);
    
    // Verificar status do modo aprendizado
    fetch('/api/status')
      .then(r => r.json())
      .then(data => {
        learnMode = data.learning_mode || false;
        const btn = document.getElementById('btn-learn');
        btn.textContent = learnMode ? '⏹ Parar Aprendizado' : '📥 Modo Aprendizado';
        btn.classList.toggle('active', learnMode);
        
        // Se há códigos salvos, carregar novamente para garantir sincronização
        if (data.codes_stored > 0 && currentCodesCount === 0) {
          loadCodes();
        }
        
        // Iniciar polling se já estiver em modo aprendizado
        if (learnMode) {
          startLearnPolling();
        }
      });
    
    // Polling leve: verifica apenas o count sem recarregar tudo
    // Só atualiza se o número de códigos mudou (mas não quando em modo aprendizado para evitar conflito)
    setInterval(() => {
      if (learnMode) return; // Não verificar count quando em modo aprendizado (já tem polling específico)
      
      fetch('/api/status')
        .then(r => r.json())
        .then(data => {
          // Se o número de códigos mudou, fazer refresh completo
          if (data.codes_stored !== currentCodesCount) {
            loadCodes();
          }
        })
        .catch(() => {}); // Ignorar erros silenciosamente
    }, 5000); // Verificar a cada 5 segundos (mais leve que antes)
  </script>
</body>
</html>
)";
  
  server.send(200, "text/html", html);
}

void handleStatus() {
  DynamicJsonDocument doc(500);
  doc["status"] = "ok";
  doc["learning_mode"] = isLearning;
  doc["codes_stored"] = codeCount;
  doc["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
  doc["wifi_configured"] = wifiConfigured;
  doc["wifi_mac"] = WiFi.macAddress();
  
  if (WiFi.status() == WL_CONNECTED) {
    doc["wifi_ip"] = WiFi.localIP().toString();
    doc["wifi_ssid"] = WiFi.SSID();
    doc["wifi_rssi"] = WiFi.RSSI();
    doc["wifi_gateway"] = WiFi.gatewayIP().toString();
    doc["wifi_subnet"] = WiFi.subnetMask().toString();
  } else {
    doc["wifi_ip"] = "";
    doc["wifi_ssid"] = "";
    doc["wifi_rssi"] = 0;
    doc["wifi_gateway"] = "";
    doc["wifi_subnet"] = "";
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleLearnStart() {
  isLearning = true;
  lastReceivedCode = 0;
  codeProcessed = true;  // Reset flag ao iniciar modo aprendizado
  Serial.println("✓ Modo aprendizado ATIVADO");
  server.send(200, "application/json", "{\"status\":\"learning_started\"}");
}

void handleLearnStop() {
  isLearning = false;
  Serial.println("✗ Modo aprendizado DESATIVADO");
  server.send(200, "application/json", "{\"status\":\"learning_stopped\"}");
}

void handleLearnCaptured() {
  // Retorna o último código capturado se houver e não foi processado
  if (!isLearning) {
    server.send(200, "application/json", "{\"captured\":false}");
    return;
  }
  
  if (lastReceivedCode != 0ULL && !codeProcessed) {
    DynamicJsonDocument doc(300);
    doc["captured"] = true;
    doc["code"] = String((uint32_t)lastReceivedCode, HEX);
    char codeStr[20];
    sprintf(codeStr, "0x%llX", lastReceivedCode);
    doc["code_hex"] = codeStr;
    doc["protocol"] = getProtocolName(lastReceivedProtocol);
    doc["protocol_id"] = (int)lastReceivedProtocol;
    doc["bits"] = lastReceivedBits;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  } else {
    server.send(200, "application/json", "{\"captured\":false}");
  }
}

void handleLearnSave() {
  Serial.println("📝 handleLearnSave chamado");
  
  if (!server.hasArg("plain")) {
    Serial.println("✗ Erro: sem dados no body");
    sendJsonError(400, "no_data");
    return;
  }

  String body = server.arg("plain");
  Serial.printf("📥 Body recebido: %s\n", body.c_str());

  StaticJsonDocument<300> doc;
  DeserializationError error = deserializeJson(doc, body);
  
  if (error) {
    Serial.printf("✗ Erro ao parsear JSON: %s\n", error.c_str());
    sendJsonError(400, "json_parse_error");
    return;
  }

  const char* devicePtr = doc["device"] | "Controle";
  const char* buttonPtr = doc["button"] | "";
  
  char device[20];
  char button[30];
  
  // Copiar e validar device
  if (devicePtr && strlen(devicePtr) > 0) {
    strncpy(device, devicePtr, MAX_DEVICE_NAME);
    device[MAX_DEVICE_NAME] = '\0';
  } else {
    strncpy(device, "Controle", MAX_DEVICE_NAME);
    device[MAX_DEVICE_NAME] = '\0';
    Serial.println("⚠ Device vazio, usando padrão: 'Controle'");
  }
  
  // Copiar e validar button
  if (buttonPtr && strlen(buttonPtr) > 0) {
    strncpy(button, buttonPtr, MAX_BUTTON_NAME);
    button[MAX_BUTTON_NAME] = '\0';
  } else if (doc.containsKey("name")) {
    const char* namePtr = doc["name"];
    if (namePtr && strlen(namePtr) > 0) {
      strncpy(button, namePtr, MAX_BUTTON_NAME);
      button[MAX_BUTTON_NAME] = '\0';
      Serial.printf("⚠ Button vazio, usando 'name': '%s'\n", button);
    } else {
      button[0] = '\0';
    }
  } else {
    button[0] = '\0';
  }
  
  Serial.printf("📋 Device: '%s', Button: '%s'\n", device, button);
  
  // Validar se button ainda está vazio
  if (strlen(button) == 0) {
    Serial.println("✗ Erro: button está vazio após processamento");
    sendJsonError(400, "button_required");
    return;
  }
  
  // Validação de segurança: verificar limites antes de adicionar
  if (codeCount >= MAX_CODES) {
    Serial.printf("✗ Erro: limite de códigos atingido (%d)\n", MAX_CODES);
    server.send(400, "application/json", "{\"status\":\"limit\",\"message\":\"max_codes_reached\"}");
    return;
  }
  
  // Validação adicional: garantir que codeCount está válido
  if (codeCount < 0) {
    codeCount = 0;  // Reset se corrompido
  }

  Serial.printf("🔍 Verificando lastReceivedCode: 0x%llX\n", lastReceivedCode);
  if (lastReceivedCode == 0ULL) {
    Serial.println("✗ Erro: nenhum código capturado (lastReceivedCode = 0)");
    sendJsonError(400, "no_code_captured");
    return;
  }
  
  // Salvar código antes de resetar
  uint64_t savedCode = lastReceivedCode;
  
  // Validação de segurança: garantir que não excede limites
  if (codeCount >= 0 && codeCount < MAX_CODES) {
    storedCodes[codeCount].code = savedCode;
    storedCodes[codeCount].bits = lastReceivedBits;
    
    // ⭐ NOVO: Salvar protocolo e dados relacionados
    storedCodes[codeCount].protocol = lastReceivedProtocol;
    storedCodes[codeCount].address = lastReceivedAddress;
    storedCodes[codeCount].command = lastReceivedCommand;
    storedCodes[codeCount].repeats = 0;  // Padrão: sem repetições
    
    // Validação de tamanho de strings antes de copiar
    strncpy(storedCodes[codeCount].device, device, MAX_DEVICE_NAME);
    storedCodes[codeCount].device[MAX_DEVICE_NAME] = '\0';
    
    strncpy(storedCodes[codeCount].button, button, MAX_BUTTON_NAME);
    storedCodes[codeCount].button[MAX_BUTTON_NAME] = '\0';
  } else {
    Serial.println("✗ Erro crítico: codeCount inválido ao salvar!");
    sendJsonError(500, "internal_error");
    return;
  }
  
  const char* protocolName = getProtocolName(lastReceivedProtocol);
  Serial.printf("💾 Salvando código no índice %d (Protocolo: %s)\n", codeCount, protocolName);
  Serial.printf("   Dados salvos: address=0x%04X, command=0x%04X, bits=%d\n",
                 storedCodes[codeCount].address, storedCodes[codeCount].command, storedCodes[codeCount].bits);
  codeCount++;
  
  // Salvar no Preferences
  saveCodesToPreferences();
  Serial.println("✓ Preferences atualizado");
  
  // Marca como processado após salvar e reseta para próximo código
  codeProcessed = true;
  lastReceivedCode = 0;  // Reset para próximo código
  lastReceivedProtocol = PROTOCOL_UNKNOWN;
  lastReceivedAddress = 0;
  lastReceivedCommand = 0;

  Serial.printf("✓ Código salvo: %s - %s (Protocolo: %s, 0x%llX)\n", 
                device, button, protocolName, savedCode);
  
  // Retornar informações para atualização automática da interface
  DynamicJsonDocument response(200);
  response["status"] = "success";
  response["code_count"] = codeCount;
  String responseStr;
  serializeJson(response, responseStr);
  Serial.println("📤 Enviando resposta: " + responseStr);
  server.send(200, "application/json", responseStr);
}

void handleListCodes() {
  DynamicJsonDocument doc(8192);  // Aumentado para suportar mais códigos
  JsonArray array = doc.to<JsonArray>();

  // Validação de segurança: garantir limites válidos
  int safeCount = (codeCount > MAX_CODES) ? MAX_CODES : codeCount;
  safeCount = (safeCount < 0) ? 0 : safeCount;

  for (int i = 0; i < safeCount; i++) {
    if (storedCodes[i].code != 0ULL) {  // Filtro para códigos válidos
      JsonObject obj = array.createNestedObject();
      obj["id"] = i;
      obj["name"] = String(storedCodes[i].device) + " - " + String(storedCodes[i].button);
      obj["device"] = storedCodes[i].device;
      obj["button"] = storedCodes[i].button;
      obj["protocol"] = getProtocolName(storedCodes[i].protocol);
      obj["protocol_id"] = (int)storedCodes[i].protocol;
      // Retornar code como string hex para evitar problemas com uint64_t no JSON
      char codeStr[20];
      sprintf(codeStr, "0x%llX", storedCodes[i].code);
      obj["code"] = codeStr;
    }
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// Handler para editar código
void handleCodeEdit() {
  if (!server.hasArg("plain")) {
    sendJsonError(400, "no_data");
    return;
  }

  StaticJsonDocument<300> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  
  if (error) {
    sendJsonError(400, "json_parse_error");
    return;
  }

  int id = doc["id"] | -1;
  const char* devicePtr = doc["device"] | "";
  const char* buttonPtr = doc["button"] | "";
  
  // Validação
  if (id < 0 || id >= codeCount || id >= MAX_CODES) {
    sendJsonError(404, "invalid_id");
    return;
  }
  
  if (strlen(devicePtr) == 0 || strlen(buttonPtr) == 0) {
    sendJsonError(400, "device_and_button_required");
    return;
  }
  
  // Atualizar código
  strncpy(storedCodes[id].device, devicePtr, MAX_DEVICE_NAME);
  storedCodes[id].device[MAX_DEVICE_NAME] = '\0';
  
  strncpy(storedCodes[id].button, buttonPtr, MAX_BUTTON_NAME);
  storedCodes[id].button[MAX_BUTTON_NAME] = '\0';
  
  // Salvar no Preferences
  saveCodesToPreferences();
  
  Serial.printf("✓ Código editado: ID %d -> %s - %s\n", id, devicePtr, buttonPtr);
  sendJsonSuccess("code_updated");
}

// Handler para deletar código (atualizado)
void handleCodeDelete() {
  if (!server.hasArg("plain")) {
    sendJsonError(400, "no_data");
    return;
  }

  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  
  if (error) {
    sendJsonError(400, "json_parse_error");
    return;
  }

  int id = doc["id"] | -1;

  // Validação de segurança: verificar limites antes de deletar
  if (id >= 0 && id < codeCount && codeCount > 0 && codeCount <= MAX_CODES) {
    // Mover códigos para preencher o espaço
    for (int i = id; i < codeCount - 1 && i < MAX_CODES - 1; i++) {
      storedCodes[i] = storedCodes[i + 1];
    }
    codeCount--;
    if (codeCount < 0) codeCount = 0;  // Proteção contra underflow
    
    // Salvar no Preferences
    saveCodesToPreferences();
    
    Serial.printf("✓ Código removido (ID: %d)\n", id);
    sendJsonSuccess("code_deleted");
  } else {
    sendJsonError(400, "invalid_id");
  }
}


void handleCodeSend() {
  if (!server.hasArg("plain")) {
    sendJsonError(400, "no_data");
    return;
  }

  StaticJsonDocument<300> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  
  if (error) {
    sendJsonError(400, "json_parse_error");
    return;
  }

  uint64_t codeToSend = 0ULL;
  
  // Aceita tanto "id" quanto "code" diretamente
  if (doc.containsKey("id")) {
    int id = doc["id"].as<int>();
    // Validação de segurança: verificar limites
    if (id >= 0 && id < codeCount && id < MAX_CODES && codeCount <= MAX_CODES) {
      codeToSend = storedCodes[id].code;
      Serial.printf("Enviando código por ID %d: 0x%llX\n", id, codeToSend);
    } else {
      sendJsonError(404, "invalid_id");
      return;
    }
  } else if (doc.containsKey("code")) {
    // Aceita código como string hex (ex: "0x12345678") ou número
    const char* codeStr = doc["code"];
    if (codeStr && (strncmp(codeStr, "0x", 2) == 0 || strncmp(codeStr, "0X", 2) == 0)) {
      codeToSend = strtoull(codeStr, NULL, 16);
    } else {
      codeToSend = doc["code"].as<uint64_t>();
    }
    Serial.printf("Enviando código direto: 0x%llX\n", codeToSend);
  } else {
    sendJsonError(400, "id_or_code_required");
    return;
  }

  if (codeToSend == 0ULL) {
    sendJsonError(400, "invalid_code");
    return;
  }

  // Buscar código completo do storage para ter protocolo
  IRCode codeToSendObj;
  bool found = false;
  
  if (doc.containsKey("id")) {
    int id = doc["id"].as<int>();
    if (id >= 0 && id < codeCount && id < MAX_CODES) {
      codeToSendObj = storedCodes[id];
      found = true;
    }
  }
  
  // Se não encontrou por ID, criar objeto temporário (fallback para NEC)
  if (!found) {
    codeToSendObj.code = codeToSend;
    codeToSendObj.protocol = PROTOCOL_NEC;  // Fallback
    codeToSendObj.address = (codeToSend >> 16) & 0xFFFF;
    codeToSendObj.command = codeToSend & 0xFFFF;
    codeToSendObj.bits = 32;
    codeToSendObj.repeats = 0;
  }
  
  if (sendIRCode(codeToSendObj)) {
    sendJsonSuccess("code_sent");
  } else {
    sendJsonError(500, "failed_to_send");
  }
}

// Handler para página de configuração WiFi
void handleWiFiConfig() {
  String html = R"(
<!DOCTYPE html>
<html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width,initial-scale=1'>
  <title>Configuração WiFi - ESP32</title>
  <style>
    * { box-sizing: border-box; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Arial, sans-serif;
      max-width: 400px;
      margin: 50px auto;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      padding: 20px;
    }
    .container {
      background: white;
      border-radius: 15px;
      padding: 30px;
      box-shadow: 0 10px 40px rgba(0,0,0,0.2);
    }
    h1 {
      color: #333;
      text-align: center;
      margin-bottom: 10px;
    }
    .subtitle {
      text-align: center;
      color: #666;
      font-size: 14px;
      margin-bottom: 25px;
    }
    .form-group {
      margin-bottom: 20px;
    }
    label {
      display: block;
      margin-bottom: 8px;
      color: #333;
      font-weight: 500;
    }
    input {
      width: 100%;
      padding: 12px;
      font-size: 16px;
      border: 2px solid #ddd;
      border-radius: 8px;
      box-sizing: border-box;
    }
    input:focus {
      outline: none;
      border-color: #667eea;
    }
    button {
      width: 100%;
      padding: 14px;
      font-size: 16px;
      font-weight: 500;
      border: none;
      border-radius: 8px;
      cursor: pointer;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      color: white;
      transition: all 0.3s;
      box-shadow: 0 4px 15px rgba(102, 126, 234, 0.4);
    }
    button:hover {
      transform: translateY(-2px);
      box-shadow: 0 6px 20px rgba(102, 126, 234, 0.6);
    }
    .status {
      margin-top: 20px;
      padding: 12px;
      border-radius: 8px;
      text-align: center;
      font-weight: 500;
    }
    .status.success {
      background: #d4edda;
      color: #155724;
    }
    .status.error {
      background: #f8d7da;
      color: #721c24;
    }
    .status.warning {
      background: #fff3cd;
      color: #856404;
    }
    .info-box {
      background: #e7f3ff;
      border-left: 4px solid #667eea;
      padding: 12px;
      margin-bottom: 20px;
      border-radius: 4px;
      font-size: 13px;
    }
    .info-box strong {
      display: block;
      margin-bottom: 5px;
      color: #333;
    }
    .btn-secondary {
      background: #6c757d;
      margin-top: 10px;
    }
    .btn-secondary:hover {
      background: #5a6268;
    }
  </style>
</head>
<body>
  <div class='container'>
    <h1>📡 Configuração WiFi</h1>
    <div class='subtitle'>Configure a conexão WiFi do ESP32</div>
    
    <div id='infoBox' class='info-box' style='display:none;'>
      <strong>Status Atual:</strong>
      <div id='currentStatus'>Carregando...</div>
    </div>
    
    <form id='wifiForm'>
      <div class='form-group'>
        <label for='ssid'>Nome da Rede (SSID):</label>
        <input type='text' id='ssid' name='ssid' required maxlength='32' autofocus>
      </div>
      
      <div class='form-group'>
        <label for='password'>Senha:</label>
        <input type='password' id='password' name='password' maxlength='64'>
      </div>
      
      <button type='submit'>Conectar</button>
      <button type='button' class='btn-secondary' onclick='reconnectWiFi()'>🔄 Reconectar WiFi</button>
    </form>
    
    <div id='status' class='status' style='display:none;'></div>
  </div>
  
  <script>
    // Carregar status atual ao abrir a página
    fetch('/api/status')
      .then(r => r.json())
      .then(data => {
        const infoBox = document.getElementById('infoBox');
        const currentStatus = document.getElementById('currentStatus');
        
        if (data.wifi_connected) {
          currentStatus.innerHTML = 
            '✓ <strong>Conectado</strong><br>' +
            'IP: ' + data.wifi_ip + '<br>' +
            'SSID: ' + data.wifi_ssid + '<br>' +
            'RSSI: ' + data.wifi_rssi + ' dBm<br>' +
            'MAC: ' + (data.wifi_mac || 'N/A');
          infoBox.style.display = 'block';
        } else {
          // Detectar IP do AP dinamicamente
          const apIP = window.location.hostname || '192.168.68.1';
          currentStatus.innerHTML = 
            '✗ <strong>Desconectado</strong><br>' +
            'Modo: Access Point (' + apIP + ')<br>' +
            '⚠ Conecte-se ao WiFi "ESP32-ControleRemoto" primeiro!<br>' +
            'Configure o WiFi abaixo para conectar à sua rede';
          infoBox.style.display = 'block';
          infoBox.className = 'info-box';
          infoBox.style.background = '#fff3cd';
          infoBox.style.borderLeftColor = '#ffc107';
        }
      })
      .catch(() => {
        document.getElementById('infoBox').style.display = 'none';
      });
    
    function reconnectWiFi() {
      const statusDiv = document.getElementById('status');
      statusDiv.textContent = '🔄 Reconectando...';
      statusDiv.className = 'status';
      statusDiv.style.display = 'block';
      
      fetch('/api/wifi/reconnect', { method: 'POST' })
        .then(r => r.json())
        .then(data => {
          if (data.status === 'success') {
            statusDiv.textContent = '✓ Reconectado! IP: ' + data.ip;
            statusDiv.className = 'status success';
            setTimeout(() => location.reload(), 2000);
          } else {
            statusDiv.textContent = '✗ ' + (data.message || 'Erro ao reconectar');
            statusDiv.className = 'status error';
          }
        })
        .catch(e => {
          statusDiv.textContent = '✗ Erro de conexão';
          statusDiv.className = 'status error';
        });
    }
    
    document.getElementById('wifiForm').addEventListener('submit', function(e) {
      e.preventDefault();
      
      const ssid = document.getElementById('ssid').value.trim();
      const password = document.getElementById('password').value;
      const statusDiv = document.getElementById('status');
      
      if (!ssid) {
        statusDiv.textContent = 'Por favor, informe o SSID';
        statusDiv.className = 'status error';
        statusDiv.style.display = 'block';
        return;
      }
      
      statusDiv.textContent = '⏳ Conectando (pode levar até 30 segundos)...';
      statusDiv.className = 'status';
      statusDiv.style.display = 'block';
      
      fetch('/api/wifi/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ssid: ssid, password: password })
      })
      .then(r => r.json())
      .then(data => {
        if (data.status === 'success') {
          statusDiv.textContent = '✓ ' + data.message;
          statusDiv.className = 'status success';
          setTimeout(() => {
            statusDiv.textContent = 'Aguarde alguns segundos e acesse: http://' + data.ip;
            setTimeout(() => location.reload(), 3000);
          }, 2000);
        } else {
          statusDiv.textContent = '⚠ ' + (data.message || 'Erro desconhecido');
          statusDiv.className = 'status warning';
        }
      })
      .catch(e => {
        statusDiv.textContent = '✗ Erro de conexão';
        statusDiv.className = 'status error';
      });
    });
  </script>
</body>
</html>
)";
  server.send(200, "text/html", html);
}

// Handler para salvar configuração WiFi
void handleWiFiConfigSave() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"no_data\"}");
    return;
  }

  String body = server.arg("plain");
  StaticJsonDocument<300> doc;
  DeserializationError error = deserializeJson(doc, body);
  
  if (error) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"json_parse_error\"}");
    return;
  }

  String ssid = doc["ssid"] | "";
  String password = doc["password"] | "";
  
  // Validação de segurança: verificar tamanho
  if (ssid.length() == 0 || ssid.length() > MAX_SSID_LENGTH) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"invalid_ssid\"}");
    return;
  }
  
  if (password.length() > MAX_PASSWORD_LENGTH) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"password_too_long\"}");
    return;
  }
  
  // Salvar credenciais
  saveWiFiCredentials(ssid.c_str(), password.c_str());
  Serial.println("💾 Credenciais salvas, tentando conectar...");
  
  // Tentar conectar (força reconexão completa)
  WiFi.disconnect();
  delay(500);
  
  bool connected = connectToWiFi(ssid.c_str(), password.c_str());
  
  DynamicJsonDocument response(300);
  if (connected) {
    // Ativar modo híbrido para permitir reconfiguração
    WiFi.mode(WIFI_AP_STA);
    delay(100);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    IPAddress AP_IP(AP_IP_OCTET_1, AP_IP_OCTET_2, AP_IP_OCTET_3, AP_IP_OCTET_4);
    IPAddress gateway(AP_IP_OCTET_1, AP_IP_OCTET_2, AP_IP_OCTET_3, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(AP_IP, gateway, subnet);
    
    response["status"] = "success";
    response["ip"] = WiFi.localIP().toString();
    response["mac"] = WiFi.macAddress();
    response["rssi"] = WiFi.RSSI();
    response["message"] = "WiFi configurado com sucesso! IP: " + WiFi.localIP().toString();
  } else {
    // Se falhar, manter AP ativo
    if (WiFi.getMode() != WIFI_AP && WiFi.getMode() != WIFI_AP_STA) {
      startConfigAP();
    }
    response["status"] = "warning";
    response["message"] = "Credenciais salvas, mas falha ao conectar. Verifique SSID e senha. O AP continua ativo para nova tentativa.";
  }
  
  String responseStr;
  serializeJson(response, responseStr);
  server.send(200, "application/json", responseStr);
}

// Handler para forçar reconexão WiFi
void handleWiFiReconnect() {
  Serial.println("🔄 Reconexão WiFi solicitada via API...");
  
  char ssid[MAX_SSID_LENGTH + 1];
  char password[MAX_PASSWORD_LENGTH + 1];
  
  DynamicJsonDocument response(300);
  
  if (loadWiFiCredentials(ssid, password, MAX_SSID_LENGTH + 1)) {
    WiFi.disconnect();
    delay(500);
    
    bool connected = connectToWiFi(ssid, password);
    
    if (connected) {
      // Ativar modo híbrido
      WiFi.mode(WIFI_AP_STA);
      delay(100);
      WiFi.softAP(AP_SSID, AP_PASSWORD);
      IPAddress AP_IP(AP_IP_OCTET_1, AP_IP_OCTET_2, AP_IP_OCTET_3, AP_IP_OCTET_4);
      IPAddress gateway(AP_IP_OCTET_1, AP_IP_OCTET_2, AP_IP_OCTET_3, 1);
      IPAddress subnet(255, 255, 255, 0);
      WiFi.softAPConfig(AP_IP, gateway, subnet);
      
      response["status"] = "success";
      response["ip"] = WiFi.localIP().toString();
      response["message"] = "Reconectado com sucesso";
    } else {
      response["status"] = "error";
      response["message"] = "Falha ao reconectar";
    }
  } else {
    response["status"] = "error";
    response["message"] = "Nenhuma credencial WiFi configurada";
  }
  
  String responseStr;
  serializeJson(response, responseStr);
  server.send(200, "application/json", responseStr);
}

void setupRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/config", HTTP_GET, handleWiFiConfig);
  server.on("/api/wifi/config", HTTP_POST, handleWiFiConfigSave);
  server.on("/api/wifi/reconnect", HTTP_POST, handleWiFiReconnect);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/learn/start", HTTP_POST, handleLearnStart);
  server.on("/api/learn/stop", HTTP_POST, handleLearnStop);
  server.on("/api/learn/save", HTTP_POST, handleLearnSave);
  server.on("/api/learn/captured", HTTP_GET, handleLearnCaptured);
  server.on("/api/codes", HTTP_GET, handleListCodes);
  server.on("/api/code/send", HTTP_POST, handleCodeSend);
  server.on("/api/code/edit", HTTP_POST, handleCodeEdit);
  server.on("/api/code/delete", HTTP_POST, handleCodeDelete);
}

// ============================================================================
// SETUP E LOOP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n");
  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║   CONTROLE REMOTO UNIVERSAL - ESP32    ║");
  Serial.println("║         Iniciando Sistema...           ║");
  Serial.println("╚════════════════════════════════════════╝\n");

  pinMode(BUTTON_LEARNING, INPUT_PULLUP);

  // Emissor: OUTPUT e LOW no boot. IrSender.begin() não toca no pino; LEDC é anexado no 1º send().
  pinMode(IR_EMITTER_PIN, OUTPUT);
  digitalWrite(IR_EMITTER_PIN, LOW);

  // IrSender: com IR_SEND_PIN definido, begin() sem args. O envio usa IR_SEND_PIN; LEDC no 1º send().
  IrSender.begin();

  // Inicializa receptor IR com a nova API (sem LED feedback)
  IrReceiver.begin(IR_RECEIVER_PIN, false);
  
  // Preferences é inicializado dentro de loadCodesFromPreferences()
  loadCodesFromPreferences();

  setupWiFi();
  
  // Aguardar um pouco para garantir que o AP esteja totalmente iniciado
  delay(500);
  
  setupRoutes();

  server.begin();
  Serial.println("✓ Servidor Web iniciado na porta 80");
  
  // Mostrar informações finais de rede
  Serial.println("\n════════════════════════════════════════");
  Serial.println("  INFORMAÇÕES DE REDE:");
  Serial.println("════════════════════════════════════════");
  
  if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
    IPAddress apIP = WiFi.softAPIP();
    Serial.println("  📡 Access Point:");
    Serial.println("    SSID: " + String(AP_SSID));
    Serial.println("    IP: " + apIP.toString());
    Serial.println("    Gateway: " + apIP.toString() + " (próprio ESP32)");
    Serial.println("    Subnet: 255.255.255.0");
    Serial.println("    MAC: " + WiFi.softAPmacAddress());
    Serial.println("    Clientes: " + String(WiFi.softAPgetStationNum()));
    Serial.println("    ➜ Acesse: http://" + apIP.toString() + "/config");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("  📶 WiFi Conectado:");
    Serial.println("    IP: " + WiFi.localIP().toString());
    Serial.println("    Gateway: " + WiFi.gatewayIP().toString());
    Serial.println("    Subnet: " + WiFi.subnetMask().toString());
    Serial.println("    DNS: " + WiFi.dnsIP().toString());
    Serial.println("    SSID: " + WiFi.SSID());
    Serial.println("    RSSI: " + String(WiFi.RSSI()) + " dBm");
    Serial.println("    MAC: " + WiFi.macAddress());
    Serial.println("    ➜ Acesse: http://" + WiFi.localIP().toString());
  }
  
  Serial.println("════════════════════════════════════════\n");

  Serial.println("✓ Receptor IR ativo no GPIO 14");
  Serial.println("✓ Emissor IR ativo no GPIO " + String(IR_EMITTER_PIN) + " (IrSender.begin)");
  Serial.println();
}

void loop() {
  server.handleClient();

  // Verificar e reconectar WiFi se necessário (Fase 1 - Correção Crítica)
  checkWiFiConnection();

  // Nova API: IrReceiver.decode() retorna true se houver dados
  if (IrReceiver.decode()) {
    handleReceivedIR();
    IrReceiver.resume(); // Habilita recepção do próximo sinal
  }

  static unsigned long lastButtonCheck = 0;
  const unsigned long debounceDelay = 50;
  
  if (digitalRead(BUTTON_LEARNING) == LOW) {
    unsigned long now = millis();
    if (now - lastButtonCheck > debounceDelay) {
      lastButtonCheck = now;
      if (digitalRead(BUTTON_LEARNING) == LOW) {
        toggleLearningMode();
        while (digitalRead(BUTTON_LEARNING) == LOW) {
          yield();
          delay(10);
        }
        delay(500);
      }
    }
  }
  
  yield();
}
