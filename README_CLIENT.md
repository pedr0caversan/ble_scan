# BLE Scanner Project

Sistema completo para scan de dispositivos BLE usando ESP32-C3 e cliente C para Linux/macOS.

## Componentes

### 1. ESP32-C3 Firmware (`main/gattc_demo.c`)
- Aceita comandos seriais no formato `SCAN <timeout_ms>`
- Retorna dispositivos encontrados no formato: `DEV,<MAC>,<RSSI>,<NOME>`
- Envia marcador `END` ao final do scan

### 2. Cliente C (`ble_client.c`)
- Conecta à porta serial do ESP32
- Envia comando de scan
- Coleta e ordena resultados por RSSI
- Exibe lista formatada dos dispositivos

## Protocolo de Comunicação

### Comando
```
SCAN <timeout_ms>
```
- `timeout_ms`: Tempo de scan em millisegundos (1000-300000)

### Resposta
```
DEV,AA:BB:CC:DD:EE:FF,-45,SmartWatch
DEV,11:22:33:44:55:66,-67,iPhone
DEV,00:11:22:33:44:55,-89,
END
```

Formato da linha de dispositivo: `DEV,<MAC>,<RSSI>,<NOME>`
- MAC: Endereço no formato XX:XX:XX:XX:XX:XX
- RSSI: Intensidade do sinal em dBm
- NOME: Nome do dispositivo (pode estar vazio)

## Compilação e Instalação

### ESP32-C3 Firmware

```bash
# Configure para ESP32-C3
idf.py set-target esp32c3

# Compile
idf.py build

# Flash no dispositivo
idf.py -p /dev/ttyUSB0 flash

# Monitor serial (opcional)
idf.py -p /dev/ttyUSB0 monitor
```

### Cliente C (Linux/macOS)

```bash
# Compilar
make

# Ou manualmente:
gcc -Wall -Wextra -std=c99 -O2 -o ble_client ble_client.c

# Instalar globalmente (opcional)
make install
```

## Uso

### 1. Conectar ESP32-C3
- Conecte o ESP32-C3 via USB
- Identifique a porta serial (ex: `/dev/ttyUSB0`, `/dev/tty.usbserial-XXXX`)

### 2. Executar Cliente

```bash
# Formato básico
./ble_client <porta_serial> <timeout_ms>

# Exemplos
./ble_client /dev/ttyUSB0 10000      # Linux - scan por 10 segundos
./ble_client /dev/tty.usbserial-14320 15000  # macOS - scan por 15 segundos
```

### 3. Exemplo de Saída

```
BLE Scanner Client
Port: /dev/ttyUSB0, Timeout: 10000ms
Sending scan command...
Scanning for BLE devices...

Scan completed. Found 3 device(s):
MAC Address       RSSI   Device Name
-----------       ----   -----------
AA:BB:CC:DD:EE:FF  -32   SmartWatch
11:22:33:44:55:66  -45   iPhone 12
00:11:22:33:44:55  -67   <No Name>
```

## Características

### Funcionalidades
- ✅ Scan sob demanda via comando serial
- ✅ Timeout configurável (1s a 5min)
- ✅ Protocolo texto simples e robusto (CRLF compatível)
- ✅ Ordenação automática por intensidade de sinal
- ✅ Deduplicação automática por MAC address
- ✅ Suporte a nomes de dispositivos (opcionais)
- ✅ Detecção de erros e timeouts
- ✅ Compatibilidade Linux e macOS
- ✅ Prevenção de múltiplos marcadores END

### Tratamento de Erros
- Porta serial inexistente ou sem permissão
- Timeout sem receber marcador `END`
- Parâmetros inválidos
- Falhas de comunicação
- Quebras de linha inconsistentes (CR/LF/CRLF)

### Melhorias Implementadas
- **Deduplicação inteligente**: Dispositivos duplicados são filtrados automaticamente
- **RSSI otimizado**: Mantém o melhor RSSI quando mesmo dispositivo é detectado múltiplas vezes
- **Protocolo robusto**: Suporte a diferentes terminações de linha (CR/LF/CRLF)
- **END único**: Prevenção de múltiplos marcadores de fim

### Limitações
- Máximo 100 dispositivos por scan
- Nomes de dispositivo limitados a 63 caracteres
- Requer cabo USB para comunicação

## Desenvolvimento

### Estrutura do Projeto
```
ble_scan/
├── main/
│   ├── gattc_demo.c        # Firmware ESP32
│   └── CMakeLists.txt      # Build ESP32
├── ble_client.c            # Cliente C
├── Makefile               # Build cliente
├── CMakeLists.txt         # Build ESP32 (raiz)
└── README_CLIENT.md       # Esta documentação
```

### Extensões Futuras
- [ ] Suporte a filtros por nome/MAC
- [ ] Export para JSON/CSV
- [ ] Interface gráfica
- [ ] Múltiplos scans sequenciais
- [ ] Conexão WiFi para controle remoto

## Troubleshooting

### Problemas Comuns

**Erro "Permission denied" na porta serial:**
```bash
# Linux - adicionar usuário ao grupo dialout
sudo usermod -a -G dialout $USER
# Fazer logout/login ou reiniciar

# Ou dar permissão temporária
sudo chmod 666 /dev/ttyUSB0
```

**Erro "Device or resource busy":**
```bash
# Fechar outros programas usando a porta serial
# Ex: Arduino IDE, monitor do idf.py, etc.
```

**Timeout sem END:**
- Verificar se o ESP32 está rodando o firmware correto
- Verificar baud rate (deve ser 115200)
- Testar comando manual no terminal serial

**Nenhum dispositivo encontrado:**
- Verificar se há dispositivos BLE próximos
- Aumentar tempo de scan
- Verificar se Bluetooth está habilitado nos dispositivos próximos

### Debug
```bash
# Teste manual de comunicação
screen /dev/ttyUSB0 115200
# Digite: SCAN 5000
# Deve retornar linhas DEV,... e terminar com END

# Verificar portas disponíveis
ls /dev/tty*        # Linux
ls /dev/tty.*       # macOS
```
