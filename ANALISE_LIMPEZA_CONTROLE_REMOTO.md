# Análise de limpeza – Controle Remoto Universal ESP32

**Data:** 2025  
**Objetivo:** Identificar código de teste, diagnóstico e itens dispensáveis para a aplicação final do controle remoto.  
**Escopo:** `src/main.cpp` e interface web embutida.

---

## 1. Resumo executivo

| Categoria | Itens | Ação recomendada |
|-----------|--------|-------------------|
| **Teste/diagnóstico** | Teste LED (boot + 2 botões na UI) | Remover ou tornar opcional por `#define` |
| **API legada** | `handleCommand` + `/api/command` | Remover (não usado pela UI) |
| **Não utilizado** | `initFS` / LittleFS | Remover (nada lê/escreve no FS) |
| **Interface web** | Botões e JS de teste LED, `console.log`, keypress quebrado | Remover/corrigir |
| **Logs Serial** | ~125 `Serial.print` | Manter; opcional: enxugar ou condicional (DEBUG) em fases futuras |

A aplicação central (aprender, enviar, editar, apagar códigos IR; WiFi; `/config`) deve ser **mantida**.

---

## 2. Código de teste e diagnóstico

Foram introduzidos para depuração do circuito (LED vermelho e IR). Com o LED IR funcionando, podem ser removidos ou guardados atrás de flag.

### 2.1. Teste do LED ao boot

| Onde | O quê |
|------|-------|
| **Função** | `testLED_Emissor()` (≈ linhas 1428–1455) |
| **Chamada** | `setup()`, final (≈ linha 2274) |
| **Efeito** | ~7 s de sequência (2 s HIGH, 2 s LOW, 5 piscadas) no pino do emissor |

**Recomendação:**  
- **Remover** a chamada `testLED_Emissor() do `setup()` e a própria função `testLED_Emissor()`.  
- Ou, se quiser manter para manutenção futura:  
  - `#define TEST_LED_AT_BOOT 0`  
  - Em `setup()`: `#if TEST_LED_AT_BOOT` → `testLED_Emissor();` → `#endif`

---

### 2.2. Endpoints e handlers de teste de LED (GPIO)

| Onde | O quê |
|------|-------|
| **Rota** | `POST /api/test-led` |
| **Handler** | `handleTestLed()` (≈ 1404–1414) |
| **Rota** | `POST /api/test-led-inv` |
| **Handler** | `handleTestLedInverted()` (≈ 1416–1426) |
| **Registro** | `setupRoutes()` (≈ 2195–2196) |

**Recomendação:**  
Remover `handleTestLed`, `handleTestLedInverted` e as duas linhas de `server.on(..., handleTestLed)` / `handleTestLedInverted` em `setupRoutes()`.

---

### 2.3. Interface web – botões e JS de teste LED

| Onde | O quê |
|------|-------|
| **HTML** | Dois botões: "Teste GPIO 2 (HIGH)" e "Teste GPIO 2 (LOW)" (≈ 877–882) |
| **JS** | `testLed()` e `testLedInv()` (≈ 947–971) |

**Recomendação:**  
Remover os dois botões e as duas funções `testLed` e `testLedInv`.

---

## 3. Código legado ou não utilizado

### 3.1. API `/api/command` e `handleCommand`

| Onde | O quê |
|------|-------|
| **Rota** | `POST /api/command` |
| **Handler** | `handleCommand()` (≈ 1339–1375) |
| **Uso** | Interface usa `POST /api/code/send` com `{ "id": id }`. Nada chama `/api/command`. |

`handleCommand` envia por `device` + `button` (busca com `findCodeIndex`). A UI atual envia por **id** via `/api/code/send`.

**Recomendação:**  
Remover `handleCommand` e a linha `server.on("/api/command", HTTP_POST, handleCommand)` em `setupRoutes()`.  
Se no futuro quiser uma API por device/button, recriar de forma explícita.

---

### 3.2. LittleFS e `initFS`

| Onde | O quê |
|------|-------|
| **Include** | `#include <LittleFS.h>` |
| **Função** | `initFS()` (≈ 411–419) |
| **Chamada** | `setup()` (≈ 2227) |
| **Uso** | Nenhum `LittleFS.open` / `File.read` / `File.write` no projeto. HTML é inline. |

**Recomendação:**  
Remover:

- `#include <LittleFS.h>`
- `initFS()` (corpo e declaração)
- a chamada `initFS();` em `setup()`

Se mais tarde for servir HTML ou assets do LittleFS, recolocar.

---

## 4. Interface web – outros ajustes

### 4.1. Bug no keypress (Enter para salvar)

| Onde | O quê |
|------|-------|
| **Código** | `document.addEventListener('keypress', ...)` (≈ 1169–1174) |
| **Problema** | Verifica `e.target.id === 'codeName'`. Não existe elemento com `id="codeName"`. Os inputs do modal são `deviceName` e `buttonName`. |

**Recomendação:**  
Alterar a condição para que Enter em qualquer um dos campos do modal de captura dispare `saveCapturedCode()`, por exemplo:

```js
if ((e.target.id === 'deviceName' || e.target.id === 'buttonName') && e.key === 'Enter') {
  saveCapturedCode();
}
```

---

### 4.2. `console.log` e `console.error`

| Onde | Exemplo |
|------|---------|
| **JS** | `console.log('Enviando para salvar:', ...)`, `console.log('Status HTTP:', ...)`, `console.log('Resposta recebida:', ...)`, `console.error('Erro ao salvar:', ...)`, `console.error('Erro na requisição:', e)`, etc. |

**Recomendação:**  
Para versão “limpa” do controle remoto: remover ou comentar esses `console.log`/`console.error`.  
Opcional: manter só em desenvolvimento (ex. variável `const DEBUG_UI = false` e `if (DEBUG_UI) console.log(...)`).

---

## 5. Logs Serial (C++)

Há dezenas de `Serial.println`/`Serial.printf` (debug, status, erros).

**Recomendação:**  
- **Agora:** manter. Ajudam em suporte e manutenção no dispositivo físico.  
- **Futuro (opcional):**  
  - `#define DEBUG_SERIAL 0` e, ao redor dos logs puramente de debug, `#if DEBUG_SERIAL`.  
  - Deixar sempre ativos: erros, “WiFi conectado”, “código salvo”, “servidor iniciado”, etc.

Não tratar como “limpeza obrigatória” nesta fase.

---

## 6. O que manter (core do controle remoto)

- **Config e hardware:** constantes (`IR_EMITTER_PIN`, `IR_RECEIVER_PIN`, `BUTTON_LEARNING`), `pinMode`/`digitalWrite` do emissor em `setup()` (sem `testLED_Emissor`), `IrSender.begin`, `IrReceiver.begin`.
- **Storage:** Preferences para códigos IR e WiFi (`saveCodesToPreferences`, `loadCodesFromPreferences`, `saveWiFiCredentials`, `loadWiFiCredentials`).
- **IR:** `sendIRCode`, `findCodeIndex`, `detectProtocol`, `getProtocolName`, estrutura `IRCode` e enum `IRProtocol`, `handleReceivedIR` e integração com `IrReceiver.decode` no `loop`.
- **API e rotas:**
  - `GET /` → `handleRoot`
  - `GET /config` → `handleWiFiConfig`
  - `POST /api/wifi/config` → `handleWiFiConfigSave`
  - `POST /api/wifi/reconnect` → `handleWiFiReconnect`
  - `GET /api/status` → `handleStatus`
  - `POST /api/learn/start` → `handleLearnStart`
  - `POST /api/learn/stop` → `handleLearnStop`
  - `POST /api/learn/save` → `handleLearnSave`
  - `GET /api/learn/captured` → `handleLearnCaptured`
  - `GET /api/codes` → `handleListCodes`
  - `POST /api/code/send` → `handleCodeSend`
  - `POST /api/code/edit` → `handleCodeEdit`
  - `POST /api/code/delete` → `handleCodeDelete`
- **WiFi e reconexão:** `setupWiFi`, `checkWiFiConnection`, `connectToWiFi`, lógica de AP + STA.
- **Interface:** HTML/CSS/JS de `handleRoot` (sem botões e JS de teste LED): modais de captura e edição, `loadCodes`, `sendCode`, `editCode`, `deleteCode`, `toggleLearn`, `saveCapturedCode`, `updateStatus`, polling de `/api/status` e `/api/learn/captured`.
- **Auxiliares:** `sendJsonError`, `sendJsonSuccess`.

---

## 7. Checklist de alterações sugeridas

### 7.1. Remoção (recomendado)

| # | Alteração | Arquivo | Linhas (aproximadas) |
|---|-----------|---------|----------------------|
| 1 | Remover `testLED_Emissor()` e sua chamada em `setup()` | `main.cpp` | 1428–1455, 2274 |
| 2 | Remover `handleTestLed` e `handleTestLedInverted` | `main.cpp` | 1404–1426 |
| 3 | Remover `server.on("/api/test-led", ...)` e `server.on("/api/test-led-inv", ...)` | `main.cpp` | 2195–2196 |
| 4 | Remover os 2 botões de teste LED no HTML | `main.cpp` (handleRoot) | 877–882 |
| 5 | Remover funções `testLed()` e `testLedInv()` no JS | `main.cpp` (handleRoot) | 947–971 |
| 6 | Remover `handleCommand` | `main.cpp` | 1339–1375 |
| 7 | Remover `server.on("/api/command", ...)` | `main.cpp` | 2185 |
| 8 | Remover `#include <LittleFS.h>`, `initFS()` e chamada `initFS()` em `setup()` | `main.cpp` | 5, 411–419, 2227 |

### 7.2. Correção

| # | Alteração | Arquivo | Onde |
|---|-----------|---------|------|
| 9 | Ajustar keypress: `'codeName'` → `'deviceName' \|\| 'buttonName'` para Enter salvar no modal de captura | `main.cpp` (handleRoot) | 1171 |

### 7.3. Opcional

| # | Alteração | Onde |
|---|-----------|------|
| 10 | Remover ou condicionar `console.log` / `console.error` no JS | handleRoot, bloco `<script>` |
| 11 | Ajuste de comentário em `setup()`: remover referência a `testLED_Emissor` no comentário do `pinMode`/`digitalWrite` do emissor | `main.cpp` |

---

## 8. Efeito aproximado da limpeza

- **Linhas removidas:** ~120–150 (funções, rotas, HTML, JS, `initFS`).  
- **Rotas removidas:** 3 (`/api/test-led`, `/api/test-led-inv`, `/api/command`).  
- **Dependência removida:** `LittleFS` (incluindo possível lib, se estiver em `platformio.ini`; no `platformio.ini` atual não consta; o core ESP32 já inclui).  
- **Comportamento:**  
  - Boot ~7 s mais rápido (sem `testLED_Emissor`).  
  - Interface mais enxuta (sem botões de teste).  
  - Enter no modal de captura passa a salvar.  
  - Sem risco para as funções centrais do controle remoto (aprender, enviar, editar, apagar, WiFi, /config).

---

## 9. Ordem sugerida para implementar

1. Remoções de teste (itens 1–5 do checklist).  
2. Remoções de legado/não usados (itens 6–8).  
3. Correção do keypress (item 9).  
4. Ajuste de comentário em `setup()` (item 11).  
5. Opcional: `console.log` (item 10).

---

*Documento gerado a partir da análise do `main.cpp` e da interface web embutida – foco em controle remoto universal em produção.*
