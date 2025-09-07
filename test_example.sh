#!/bin/bash
# Exemplo de teste do sistema BLE Scanner
# Execute este script após flashear o ESP32-C3

echo "=== BLE Scanner Test Example ==="
echo ""
echo "1. Certifique-se de que o ESP32-C3 foi flasheado com:"
echo "   idf.py -p /dev/ttyACM0 flash"
echo ""
echo "2. Identifique a porta serial do ESP32:"
echo "   Linux: geralmente /dev/ttyACM0 ou /dev/ttyACM0"
echo "   macOS: /dev/tty.usbserial-XXXX ou /dev/tty.usbmodem-XXXX"
echo ""

# Verificar se o cliente foi compilado
if [ ! -f "./ble_client" ]; then
    echo "Compilando cliente BLE..."
    make
    if [ $? -ne 0 ]; then
        echo "Erro na compilação!"
        exit 1
    fi
fi

echo "3. Testando comunicação com ESP32..."
echo ""

echo ""
echo "4. Para testar, execute:"
echo "   ./ble_client <porta_serial> <timeout_ms>" // porta serial ACM0
echo ""
echo "Exemplos:"
echo "   ./ble_client /dev/ttyACM0 10000     # Scan por 10 segundos"
echo "   ./ble_client /dev/ttyACM0 30000     # Scan por 30 segundos"
echo ""
echo "5. Saída esperada:"
echo "   BLE Scanner Client"
echo "   Port: /dev/ttyACM0, Timeout: 10000ms"
echo "   Sending scan command..."
echo "   Scanning for BLE devices..."
echo ""
echo "   Scan completed. Found X device(s):"
echo "   MAC Address       RSSI   Device Name"
echo "   -----------       ----   -----------"
echo "   AA:BB:CC:DD:EE:FF  -45   SmartWatch"
echo "   11:22:33:44:55:66  -67   <No Name>"
echo ""
echo "6. Se não encontrar dispositivos:"
echo "   - Verifique se há dispositivos BLE próximos (celular, smartwatch, etc.)"
echo "   - Certifique-se de que o Bluetooth está ativo nos dispositivos"
echo "   - Aumente o tempo de scan (ex: 30000ms)"
echo ""
echo "7. Problemas comuns:"
echo "   - 'Permission denied': sudo chmod 666 /dev/ttyACM0"
echo "   - 'Device busy': feche outros programas usando a porta serial"
echo "   - 'Timeout': verifique se o ESP32 está rodando o firmware correto"
