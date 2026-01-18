# 🔍 Guia de Debug - Problema com Envio Samsung

## Problema Relatado

- **Protocolo detectado:** Samsung ✅ (correto)
- **Problema:** Código não funciona ao enviar ❌
- **Dispositivo:** TV Samsung controlada por Google TV remote

## Melhorias Implementadas

### 1. Logs Detalhados Adicionados

Agora o código imprime informações detalhadas no Serial Monitor:

**Na Captura:**
```
📥 Código recebido (Modo Aprendizado): Protocolo=Samsung
   Raw: 0xE0E040BF, Bits: 32
   Address: 0xE0E0, Command: 0x40BF
   decodedIRData.address: 0xE0E0, decodedIRData.command: 0x40BF
```

**No Envio:**
```
📤 Enviando código IR: TV Samsung - Power On (Protocolo: Samsung)
   Detalhes: address=0xE0E0, command=0x40BF, bits=32, repeats=0
   → Chamando sendSamsung(0xE0E0, 0x40BF, 1)
   ✓ Código Samsung enviado com 1 repetição(ões)
```

### 2. Repetições Automáticas para Samsung

Samsung geralmente precisa de repetições para funcionar. O código agora:
- Se `repeats = 0`, automaticamente usa `1 repetição`
- Isso aumenta a chance do dispositivo receber o comando

### 3. Verificação de Dados

Os logs mostram:
- Address e Command extraídos
- Comparação com dados diretos da biblioteca
- Confirmação do que está sendo enviado

## Como Debugar

### Passo 1: Verificar Serial Monitor

1. Abra o Serial Monitor (115200 baud)
2. Capture um código novamente
3. Observe os logs de captura
4. Clique para enviar
5. Observe os logs de envio

**O que verificar:**
- ✅ Address e Command não são 0x0000
- ✅ Os valores fazem sentido (não são 0xFFFF)
- ✅ O protocolo está sendo detectado como Samsung

### Passo 2: Verificar Circuito

**LED IR (GPIO 4):**
- ✅ LED IR conectado corretamente?
- ✅ Resistor limitador de corrente (220Ω-330Ω)?
- ✅ LED apontando na direção correta?
- ✅ LED não está queimado?

**Teste rápido:**
- Use um celular com câmera
- Aponte a câmera para o LED IR
- Ao enviar código, você deve ver o LED piscar (visível na câmera)

### Passo 3: Verificar Distância e Ângulo

- ✅ LED IR a menos de 2 metros da TV?
- ✅ LED apontando diretamente para o receptor IR da TV?
- ✅ Sem obstáculos entre LED e TV?

### Passo 4: Testar com Repetições

Se ainda não funcionar, podemos aumentar repetições:

**Opção A:** Modificar código para 2-3 repetições
**Opção B:** Testar com controle original para comparar

## Possíveis Problemas

### 1. Address/Command Incorretos

**Sintoma:** Logs mostram valores estranhos (0x0000, 0xFFFF)

**Solução:** 
- Verificar se `IrReceiver.decodedIRData.address` e `.command` estão corretos
- Pode ser necessário usar `decodedRawData` e extrair manualmente

### 2. Formato Samsung Extendido

**Sintoma:** Samsung pode usar formato extendido (48 bits)

**Solução:**
- Verificar `numberOfBits` - se for 48, pode precisar tratamento especial
- Samsung extendido pode precisar de `sendSamsungExt()` se disponível

### 3. Timing/Frequência

**Sintoma:** LED pisca mas TV não responde

**Solução:**
- Verificar se frequência do LED está correta (38kHz)
- ESP32 usa timer hardware, deve estar OK
- Mas pode precisar ajustar se LED não for de 38kHz

### 4. Inversão de Address/Command

**Sintoma:** Código parece correto mas não funciona

**Solução:**
- Samsung pode ter address e command invertidos
- Testar: `sendSamsung(command, address, repeats)`

## Próximos Passos de Debug

1. **Capturar código novamente** e verificar logs
2. **Enviar código** e verificar logs de envio
3. **Comparar valores** com controle original (se possível)
4. **Testar LED IR** com câmera do celular
5. **Verificar distância/ângulo** do LED para TV

## Se Nada Funcionar

Podemos tentar:
1. Usar `sendRaw()` com timings capturados
2. Inverter address/command
3. Aumentar repetições para 2-3
4. Verificar se precisa de Samsung Extendido (48 bits)

---

**Após fazer upload, capture um código novamente e envie. Os logs detalhados vão mostrar exatamente o que está acontecendo!**
