# 🔍 Análise Arquitetural - Suporte a Múltiplos Protocolos IR

**Data:** 2024  
**Analista:** Arquitetura de Software  
**Problema Identificado:** Protocolo NEC hardcoded - incompatível com Samsung TV, Daikin AC, Google TV/Chromecast

---

## 🚨 Problema Atual

### Situação Identificada

**Código atual:**
```cpp
// Linha 1099 - handleCommand()
IrSender.sendNEC(address, command, 0); // ❌ NEC hardcoded

// Linha 1403 - handleCodeSend()
IrSender.sendNEC(address, command, 0); // ❌ NEC hardcoded
```

**Estrutura de dados:**
```cpp
struct IRCode {
  char device[20];
  char button[30];
  uint64_t code;  // Apenas o código, sem informação de protocolo
  uint8_t bits;
};
```

### Impacto

🔴 **CRÍTICO** - O sistema não funciona com:
- **Samsung TV**: Usa protocolo Samsung (extensão do NEC) ou protocolo próprio
- **Daikin AC**: Usa protocolo Daikin específico (formato diferente do NEC padrão)
- **Google TV/Chromecast**: Pode usar RC5, RC6, Sony, ou outros protocolos

**Consequências:**
- Códigos capturados não funcionam ao enviar
- Usuário não consegue controlar seus dispositivos
- Sistema limitado a dispositivos NEC apenas

---

## 📊 Análise de Protocolos IR

### Protocolos Comuns por Fabricante

| Fabricante/Dispositivo | Protocolo Provável | Características |
|------------------------|-------------------|-----------------|
| Samsung TV | Samsung, NEC Extendido | 32-48 bits, address + command |
| Daikin AC | Daikin, NEC (formato custom) | 32-56 bits, múltiplos comandos |
| Google TV/Chromecast | RC5, RC6, Sony | 12-20 bits, toggle bit |
| LG TV | LG, NEC | Similar Samsung |
| Sony | Sony (SIRC) | 12-20 bits |
| Panasonic | Panasonic | 48 bits |

### Biblioteca IRremote.hpp - Protocolos Suportados

A biblioteca `IRremote.hpp` suporta:
- ✅ NEC (padrão e extendido)
- ✅ Samsung
- ✅ Sony (SIRC)
- ✅ RC5, RC6
- ✅ Panasonic
- ✅ LG
- ✅ JVC
- ✅ **E muitos outros...**

**API disponível:**
```cpp
IrSender.sendNEC(address, command, repeats);
IrSender.sendSamsung(address, command, repeats);
IrSender.sendSony(command, bits, repeats);
IrSender.sendRC5(address, command, repeats);
IrSender.sendRC6(address, command, repeats);
IrSender.sendPanasonic(address, command, repeats);
IrSender.sendLG(address, command, repeats);
// ... e outros
```

**Detecção automática:**
```cpp
IrReceiver.decodedIRData.protocol; // Retorna enum do protocolo detectado
```

---

## 🏗️ Proposta Arquitetural

### Opção 1: Detecção Automática + Armazenamento de Protocolo (RECOMENDADA)

#### Princípios de Design

1. **Detecção Automática na Captura**
   - A biblioteca IRremote já detecta o protocolo automaticamente
   - Armazenar o protocolo junto com o código

2. **Envio Baseado em Protocolo**
   - Usar o protocolo armazenado para enviar
   - Fallback inteligente se protocolo não suportado

3. **Extensibilidade**
   - Fácil adicionar novos protocolos
   - Interface unificada para envio

#### Arquitetura Proposta

```
┌─────────────────────────────────────────────────────────┐
│                    CAMADA DE APRESENTAÇÃO              │
│  (Interface Web / API REST)                            │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│              CAMADA DE APLICAÇÃO                        │
│  - handleCodeSend()                                     │
│  - handleLearnSave()                                    │
│  - handleCommand()                                      │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│          CAMADA DE SERVIÇO (IR Service)                 │
│  - IRService::sendCode(IRCode)                          │
│  - IRService::detectProtocol()                          │
│  - IRService::convertProtocol()                         │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│        CAMADA DE DOMÍNIO (IR Protocol Handler)         │
│  - ProtocolHandler::send(protocol, data)                 │
│  - Strategy Pattern para cada protocolo                │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│              CAMADA DE INFRAESTRUTURA                   │
│  - IRremote.hpp (biblioteca externa)                    │
│  - Preferences (armazenamento)                          │
└─────────────────────────────────────────────────────────┘
```

#### Mudanças na Estrutura de Dados

```cpp
// Enum de protocolos suportados
enum IRProtocol {
  PROTOCOL_UNKNOWN = 0,
  PROTOCOL_NEC = 1,
  PROTOCOL_SAMSUNG = 2,
  PROTOCOL_SONY = 3,
  PROTOCOL_RC5 = 4,
  PROTOCOL_RC6 = 5,
  PROTOCOL_PANASONIC = 6,
  PROTOCOL_LG = 7,
  PROTOCOL_DAIKIN = 8,  // Pode precisar tratamento especial
  PROTOCOL_RAW = 99     // Para protocolos não suportados (armazenar raw)
};

struct IRCode {
  char device[20];
  char button[30];
  uint64_t code;        // Código IR
  uint8_t bits;         // Número de bits
  IRProtocol protocol;  // ⭐ NOVO: Protocolo detectado
  uint16_t address;     // ⭐ NOVO: Address (para protocolos que usam)
  uint16_t command;     // ⭐ NOVO: Command (para protocolos que usam)
  uint8_t repeats;      // ⭐ NOVO: Número de repetições (padrão: 0)
};
```

#### Fluxo de Captura (Modo Aprendizado)

```
1. Usuário ativa modo aprendizado
2. IrReceiver.decode() detecta sinal IR
3. IrReceiver.decodedIRData.protocol → identifica protocolo automaticamente
4. Extrair dados do protocolo:
   - NEC/Samsung: address + command
   - Sony: apenas command
   - RC5/RC6: address + command + toggle
5. Armazenar IRCode completo (protocolo + dados)
```

#### Fluxo de Envio

```
1. Usuário clica em botão na interface
2. Buscar IRCode do storage
3. IRService::sendCode(IRCode)
4. ProtocolHandler::send(protocol, IRCode)
5. Switch/case ou Strategy Pattern:
   - case PROTOCOL_NEC: IrSender.sendNEC(...)
   - case PROTOCOL_SAMSUNG: IrSender.sendSamsung(...)
   - case PROTOCOL_SONY: IrSender.sendSony(...)
   - case PROTOCOL_RC5: IrSender.sendRC5(...)
   - etc.
```

#### Implementação Sugerida

**1. Função de Detecção e Conversão:**
```cpp
IRProtocol detectAndStoreProtocol() {
  // IrReceiver.decodedIRData já contém o protocolo detectado
  decode_type_t detected = IrReceiver.decodedIRData.protocol;
  
  // Mapear enum da biblioteca para nosso enum
  switch(detected) {
    case NEC: return PROTOCOL_NEC;
    case SAMSUNG: return PROTOCOL_SAMSUNG;
    case SONY: return PROTOCOL_SONY;
    case RC5: return PROTOCOL_RC5;
    case RC6: return PROTOCOL_RC6;
    case PANASONIC: return PROTOCOL_PANASONIC;
    case LG: return PROTOCOL_LG;
    default: return PROTOCOL_UNKNOWN;
  }
}
```

**2. Função de Envio Unificada:**
```cpp
bool sendIRCode(const IRCode& code) {
  switch(code.protocol) {
    case PROTOCOL_NEC:
      IrSender.sendNEC(code.address, code.command, code.repeats);
      return true;
      
    case PROTOCOL_SAMSUNG:
      IrSender.sendSamsung(code.address, code.command, code.repeats);
      return true;
      
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
      
    case PROTOCOL_UNKNOWN:
    default:
      Serial.printf("⚠ Protocolo não suportado: %d\n", code.protocol);
      // Fallback: tentar NEC (compatibilidade retroativa)
      if (code.bits == 32) {
        IrSender.sendNEC(code.address, code.command, code.repeats);
        return true;
      }
      return false;
  }
}
```

**3. Migração de Dados Existentes:**
```cpp
void migrateLegacyCodes() {
  // Códigos antigos não têm protocolo definido
  // Assumir NEC como padrão (compatibilidade retroativa)
  for (int i = 0; i < codeCount; i++) {
    if (storedCodes[i].protocol == PROTOCOL_UNKNOWN) {
      storedCodes[i].protocol = PROTOCOL_NEC;
      // Tentar extrair address/command do código uint64_t
      storedCodes[i].address = (storedCodes[i].code >> 16) & 0xFFFF;
      storedCodes[i].command = storedCodes[i].code & 0xFFFF;
    }
  }
}
```

---

### Opção 2: Strategy Pattern (Mais Complexa, Mais Extensível)

#### Vantagens
- ✅ Máxima extensibilidade
- ✅ Fácil adicionar novos protocolos sem modificar código existente
- ✅ Testável e manutenível

#### Desvantagens
- ❌ Mais complexa
- ❌ Overhead de memória (ponteiros para funções)
- ❌ Pode ser over-engineering para este caso

#### Estrutura
```cpp
class IRProtocolStrategy {
public:
  virtual bool send(const IRCode& code) = 0;
  virtual bool canHandle(IRProtocol protocol) = 0;
};

class NECProtocolStrategy : public IRProtocolStrategy {
  bool send(const IRCode& code) override {
    IrSender.sendNEC(code.address, code.command, code.repeats);
    return true;
  }
  bool canHandle(IRProtocol protocol) override {
    return protocol == PROTOCOL_NEC;
  }
};

// Factory para criar strategies
IRProtocolStrategy* getStrategy(IRProtocol protocol);
```

**Recomendação:** Opção 1 é suficiente para este caso. Opção 2 apenas se houver necessidade de protocolos muito customizados.

---

### Opção 3: Protocolo RAW (Para Casos Especiais)

Para protocolos não suportados pela biblioteca (ex: Daikin customizado):

```cpp
struct IRCode {
  // ... campos anteriores ...
  bool isRaw;           // Se true, usar dados raw
  uint16_t rawLength;   // Tamanho do array raw
  uint16_t* rawData;    // Dados raw (timings)
};

// Envio RAW
if (code.isRaw) {
  IrSender.sendRaw(code.rawData, code.rawLength, 38); // 38kHz
}
```

**Quando usar:**
- Protocolo não suportado pela biblioteca
- Protocolo customizado do fabricante
- Último recurso

---

## 📋 Plano de Implementação

### Fase 1: Preparação (Sem Breaking Changes)

1. **Adicionar campos à estrutura IRCode**
   - Adicionar `protocol`, `address`, `command`, `repeats`
   - Manter compatibilidade: valores padrão para códigos antigos

2. **Função de migração**
   - Detectar códigos antigos (protocol == UNKNOWN)
   - Assumir NEC como padrão
   - Extrair address/command do código uint64_t

3. **Função de detecção**
   - Capturar protocolo durante aprendizado
   - Armazenar junto com código

### Fase 2: Implementação do Envio Multi-Protocolo

1. **Criar função `sendIRCode()` unificada**
   - Switch/case para cada protocolo
   - Fallback para NEC se protocolo desconhecido

2. **Substituir chamadas hardcoded**
   - `handleCommand()` → usar `sendIRCode()`
   - `handleCodeSend()` → usar `sendIRCode()`

3. **Atualizar captura**
   - `handleReceivedIR()` → detectar e armazenar protocolo
   - `handleLearnSave()` → salvar protocolo detectado

### Fase 3: Melhorias e Testes

1. **Interface Web**
   - Mostrar protocolo detectado ao salvar código
   - Permitir seleção manual de protocolo (fallback)

2. **Logging**
   - Log do protocolo usado ao enviar
   - Avisos se protocolo desconhecido

3. **Testes**
   - Testar com Samsung TV
   - Testar com Daikin AC
   - Testar com Google TV/Chromecast

---

## 🎯 Recomendação Final

### Implementar: **Opção 1 (Detecção Automática)**

**Justificativa:**
- ✅ Solução mais simples e direta
- ✅ Aproveita detecção automática da biblioteca
- ✅ Compatível com códigos existentes (migração automática)
- ✅ Extensível para novos protocolos
- ✅ Baixo overhead de memória
- ✅ Fácil manutenção

**Ordem de Implementação:**
1. Adicionar campos à estrutura (compatibilidade retroativa)
2. Implementar detecção na captura
3. Implementar função de envio unificada
4. Substituir chamadas hardcoded
5. Testar com dispositivos reais
6. Adicionar UI para mostrar protocolo

**Estimativa de Esforço:**
- Preparação: 1-2 horas
- Implementação: 3-4 horas
- Testes: 2-3 horas
- **Total: 6-9 horas**

---

## ⚠️ Considerações Especiais

### Daikin AC

Daikin pode usar protocolo customizado. Se a biblioteca não detectar automaticamente:
- Opção A: Usar modo RAW (capturar timings)
- Opção B: Verificar se há biblioteca específica Daikin
- Opção C: Usar biblioteca alternativa (ex: IRremoteESP8266 tem melhor suporte)

### Google TV/Chromecast

Pode variar por modelo. Testar captura e verificar protocolo detectado.

### Compatibilidade Retroativa

**CRÍTICO:** Códigos já salvos devem continuar funcionando:
- Assumir NEC como padrão
- Migração automática na primeira carga
- Não perder dados existentes

---

## 📚 Referências

- [IRremote.hpp Documentation](https://github.com/Arduino-IRremote/Arduino-IRremote)
- [Protocolos IR Comuns](https://www.sbprojects.net/knowledge/ir/)
- [ESP32 IR Remote Guide](https://randomnerdtutorials.com/esp32-ir-remote-control/)

---

*Documento gerado para análise arquitetural de suporte multi-protocolo IR*
