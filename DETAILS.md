# ESP32-C3 BLE Scanner 

Este projeto implementa um scanner BLE para ESP32-c3 que periodicamente exibe uma lista de dispositivos BLE disponíveis. Essa lista exibe MAC adress, nome do dispositivo e RSSI, além de ser ordenada por RSSI e ter filtro RSSI.

## Detalhes Importantes

- O scan inicia no boot do MCU e dura 5 segundos. Então a lista de dispositivos é exibida nos logs e o próximo scan ocorre automaticamente após 0,5 segundo. Não há critério de término desse ciclo;
- RSSI menores que -85 são filtrados;
- Antes de ser exibida, a lista é deduplicada por endereço MAC.
- Vale destacar que a mudança dos valores específicos é facilmente alterável. Valores estão definidos por #define de forma clara.

## Formato de Saída
```
=== BLE Scan Results (RSSI >= -85 dBm, sorted by signal strength) ===
Found 39 devices total, 23 devices above RSSI threshold:

DEV,1D:13:0A:8E:73:23,-62,
DEV,F0:B6:59:40:4F:67,-66,Nome_Dispositivo
DEV,3E:6C:ED:81:D8:8F,-67,
...
END
```

**Formato de cada linha**:
- `DEV,<MAC>,<RSSI>,<NOME>` - Informações do dispositivo


