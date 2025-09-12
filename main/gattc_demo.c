/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */


#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "nvs.h"
#include "nvs_flash.h"

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#define GATTC_TAG "GATTC_DEMO"
#define PROFILE_NUM      1
#define PROFILE_A_APP_ID 0
#define SCAN_DURATION_MS 5000
#define MAX_SCAN_DEVICES 100
#define MIN_RSSI -85

// Structure to store scanned devices
typedef struct {
    uint8_t bda[6];           // MAC address
    int8_t rssi;              // Signal strength
    char name[32];            // Device name (sanitized)
    bool has_name;            // Whether device has a name
} scanned_device_t;

// Array to store devices during scan
static scanned_device_t scan_results[MAX_SCAN_DEVICES];
static int scan_results_count = 0;

// Timer for continuous scanning
static esp_timer_handle_t scan_timer = NULL;

/* Declare static functions */
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);

static void start_ble_scan(uint32_t timeout_ms);

/**
 * @brief Timer callback para iniciar novo scan automaticamente
 */
static void scan_timer_callback(void* arg)
{
    ESP_LOGI(GATTC_TAG, "Starting new BLE scan cycle...");
    start_ble_scan(SCAN_DURATION_MS);
}

/**
 * @brief Sanitiza e copia nome do dispositivo para a estrutura
 */
static void sanitize_device_name(const uint8_t *name, uint8_t name_len, char *output, size_t max_len)
{
    size_t output_idx = 0;
    int i = 0;
    
    while (i < name_len && output_idx < (max_len - 1)) {
        uint8_t c = name[i];
        
        // Handle comma replacement
        if (c == ',') {
            output[output_idx++] = ' ';
            i++;
            continue;
        }
        
        // Handle ASCII characters (0-127)
        if (c < 128) {
            // Add printable ASCII characters
            if (c >= 32 && c <= 126) {
                output[output_idx++] = (char)c;
            }
        } else {
            // Handle UTF-8 sequences (skip them)
            if ((c & 0xF0) == 0xF0) {
                i += 3; // 4-byte UTF-8
            } else if ((c & 0xE0) == 0xE0) {
                i += 2; // 3-byte UTF-8
            } else if ((c & 0xC0) == 0xC0) {
                i += 1; // 2-byte UTF-8
            }
        }
        i++;
    }
    
    output[output_idx] = '\0';
}

/**
 * @brief Verifica se o dispositivo já existe na lista (deduplica por MAC)
 */
static bool device_exists_in_results(const uint8_t *bda)
{
    for (int i = 0; i < scan_results_count; i++) {
        if (memcmp(scan_results[i].bda, bda, 6) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Adiciona dispositivo aos resultados do scan
 */
static void add_device_to_results(const uint8_t *bda, int8_t rssi, const uint8_t *adv_name, uint8_t adv_name_len)
{
    if (scan_results_count >= MAX_SCAN_DEVICES) {
        ESP_LOGW(GATTC_TAG, "Scan results buffer full, ignoring device");
        return;
    }
    
    // Don't add if device already exists
    if (device_exists_in_results(bda)) {
        return;
    }
    
    scanned_device_t *device = &scan_results[scan_results_count];
    
    // Copy MAC address
    memcpy(device->bda, bda, 6);
    
    // Set RSSI
    device->rssi = rssi;
    
    // Process device name
    if (adv_name != NULL && adv_name_len > 0) {
        sanitize_device_name(adv_name, adv_name_len, device->name, sizeof(device->name));
        device->has_name = true;
    } else {
        device->name[0] = '\0';
        device->has_name = false;
    }
    
    scan_results_count++;
    ESP_LOGI(GATTC_TAG, "Device added to results. Total: %d", scan_results_count);
}

/**
 * @brief Compara dois dispositivos para ordenação por RSSI (decrescente)
 */
static int compare_devices_by_rssi(const void *a, const void *b)
{
    const scanned_device_t *dev_a = (const scanned_device_t *)a;
    const scanned_device_t *dev_b = (const scanned_device_t *)b;
    
    // Sort by RSSI descending (stronger signal first)
    return dev_b->rssi - dev_a->rssi;
}

/**
 * @brief Ordena e exibe dispositivos encontrados com filtro RSSI
 */
static void display_sorted_results(void)
{
    if (scan_results_count == 0) {
        printf("No devices found during scan.\n");
        return;
    }
    
    // Sort devices by RSSI (strongest first)
    qsort(scan_results, scan_results_count, sizeof(scanned_device_t), compare_devices_by_rssi);
    
    // Count devices that pass RSSI filter
    int filtered_count = 0;
    for (int i = 0; i < scan_results_count; i++) {
        if (scan_results[i].rssi >= MIN_RSSI) {
            filtered_count++;
        }
    }
    
    printf("\n=== BLE Scan Results (RSSI >= %d dBm, sorted by signal strength) ===\n", MIN_RSSI);
    printf("Found %d devices total, %d devices above RSSI threshold:\n\n", scan_results_count, filtered_count);
    
    if (filtered_count == 0) {
        printf("No devices found with RSSI >= %d dBm.\n", MIN_RSSI);
        return;
    }
    
    // Display only devices that meet RSSI criteria
    for (int i = 0; i < scan_results_count; i++) {
        scanned_device_t *device = &scan_results[i];
        
        // Apply RSSI filter
        if (device->rssi < MIN_RSSI) {
            continue; // Skip devices with weak signal
        }
        
        // Output in protocol format: DEV,<MAC>,<RSSI>,<NAME>
        printf("DEV,");
        
        // MAC address (uppercase, with colons)
        for (int j = 0; j < 6; j++) {
            printf("%02X", device->bda[j]);
            if (j < 5) printf(":");
        }
        
        // RSSI and name
        printf(",%d,", device->rssi);
        if (device->has_name) {
            printf("%s", device->name);
        }
        printf("\n");
    }
    
    printf("\n");
}

/**
 * @brief Limpa resultados do scan anterior
 */
static void clear_scan_results(void)
{
    scan_results_count = 0;
    memset(scan_results, 0, sizeof(scan_results));
}

static void send_scan_end(void)
{
    // Display organized results
    display_sorted_results();
    
    printf("END\n");
    
    // Clear results for next scan
    clear_scan_results();
    
    // Schedule next scan in 500ms (to allow a small gap between scans)
    if (scan_timer != NULL) {
        esp_timer_start_once(scan_timer, 500000); // 500ms in microseconds
    }
}

/**
 * @brief Inicia varredura BLE - Função essencial para descoberta de sensores
 */
static void start_ble_scan(uint32_t timeout_ms)
{
    // Clear collected scan results
    clear_scan_results(); 
    
    // Convert milliseconds to seconds for ESP-IDF API (round up)
    uint32_t duration_seconds = (timeout_ms + 999) / 1000;  // Round up instead of down
    if (duration_seconds == 0) duration_seconds = 1; // Minimum 1 second
    
    esp_err_t scan_ret = esp_ble_gap_start_scanning(duration_seconds);
    if (scan_ret != ESP_OK) {
        ESP_LOGE(GATTC_TAG, "Failed to start scanning: %s", esp_err_to_name(scan_ret));
        printf("ERROR: Failed to start BLE scan: %s\n", esp_err_to_name(scan_ret));
    }
}

/**
 * @brief Parâmetros de scan BLE
 */
static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x50,
    .scan_window            = 0x30,
    .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE
};

struct gattc_profile_inst {
    esp_gattc_cb_t gattc_cb;
    uint16_t gattc_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_start_handle;
    uint16_t service_end_handle;
    uint16_t char_handle;
    esp_bd_addr_t remote_bda;
};

/* One gatt-based profile one app_id and one gattc_if, this array will store the gattc_if returned by ESP_GATTS_REG_EVT */
static struct gattc_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gattc_cb = gattc_profile_event_handler,
        .gattc_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};

/**
 * @brief Manipulador de eventos para o perfil GATT Client (versão simplificada para scanning)
 */
static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTC_REG_EVT:
        ESP_LOGI(GATTC_TAG, "BLE GATT client registered successfully");
        esp_err_t scan_ret = esp_ble_gap_set_scan_params(&ble_scan_params);
        if (scan_ret){
            ESP_LOGE(GATTC_TAG, "set scan params error, error code = %x", scan_ret);
        } else {
            ESP_LOGI(GATTC_TAG, "BLE scan parameters configured");
        }
        break;
    default:
        // Outros eventos GATT não são necessários para scanning BLE
        // Scanner apenas coleta advertising data, não estabelece conexões GATT
        break;
    }
}

/**
 * @brief Callback para eventos GAP (Generic Access Profile) - Core do BLE
 * 
 */
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    uint8_t adv_name_len = 0;
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: {
        ESP_LOGI(GATTC_TAG, "BLE scan parameters set complete");
        break;
    }
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTC_TAG, "scan start failed, error status = %x", param->scan_start_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "BLE scan started successfully");
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
        switch (scan_result->scan_rst.search_evt) {
        case ESP_GAP_SEARCH_INQ_RES_EVT: {
            uint8_t *bda = scan_result->scan_rst.bda;
            int8_t rssi = scan_result->scan_rst.rssi;
            
            // Try to get device name from multiple sources
            uint8_t *adv_name = NULL;
            
            // 1. Try complete local name first
            adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv,
                                                ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);
            
            // 2. If no complete name, try shortened local name
            if (adv_name == NULL || adv_name_len == 0) {
                adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv,
                                                    ESP_BLE_AD_TYPE_NAME_SHORT, &adv_name_len);
            }
            
            // 3. Try manufacturer specific data (some devices put name there)
            if (adv_name == NULL || adv_name_len == 0) {
                uint8_t manu_len = 0;
                uint8_t *manu_data = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv,
                                                              ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE, &manu_len);
                // Some manufacturers embed readable names in their data
                if (manu_data != NULL && manu_len > 2) {
                    // Skip the first 2 bytes (company ID) and check if remaining data looks like text
                    adv_name = manu_data + 2;
                    adv_name_len = manu_len - 2;
                    
                    // Validate if it looks like readable text (basic check)
                    bool is_readable = true;
                    for (int i = 0; i < adv_name_len && i < 10; i++) {
                        if (adv_name[i] < 32 || adv_name[i] > 126) {
                            is_readable = false;
                            break;
                        }
                    }
                    
                    if (!is_readable) {
                        adv_name = NULL;
                        adv_name_len = 0;
                    }
                }
            }
            
            // Debug logging to understand what's happening
            if (adv_name != NULL && adv_name_len > 0) {
                ESP_LOGI(GATTC_TAG, "Device with name found, length: %d", adv_name_len);
            } else {
                ESP_LOGI(GATTC_TAG, "Device without name found");
            }
            
            // Add device to results collection 
            add_device_to_results(bda, rssi, adv_name, adv_name_len);
            
            break;
        }
        case ESP_GAP_SEARCH_INQ_CMPL_EVT:
            // Scan completed - send END marker
            send_scan_end();
            break;
        default:
            break;
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (param->scan_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(GATTC_TAG, "scan stop failed, error status = %x", param->scan_stop_cmpl.status);
        }
        // Send END marker when scan stops (only if not already sent)
        send_scan_end();
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(GATTC_TAG, "adv stop failed, error status = %x", param->adv_stop_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "BLE advertising stopped");
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(GATTC_TAG, "BLE connection parameters updated");
        break;
    default:
        break;
    }
}

/**
 * @brief Callback principal do GATT Client - Despachador de eventos
 */
static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    /* If event is register event, store the gattc_if for each profile */
    if (event == ESP_GATTC_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[param->reg.app_id].gattc_if = gattc_if;
        } else {
            ESP_LOGI(GATTC_TAG, "reg app failed, app_id %04x, status %d",
                    param->reg.app_id,
                    param->reg.status);
            return;
        }
    }

    /* If the gattc_if equal to profile A, call profile A cb handler,
     * so here call each profile's callback */
    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            if (gattc_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                    gattc_if == gl_profile_tab[idx].gattc_if) {
                if (gl_profile_tab[idx].gattc_cb) {
                    gl_profile_tab[idx].gattc_cb(event, gattc_if, param);
                }
            }
        }
    } while (0);
}

/**
 * @brief Função principal da aplicação - Inicialização completa do sistema BLE
 */
void app_main(void)
{
    // Initialize NVS.
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    // Skip UART driver configuration for USB-Serial compatibility
    // configure_uart_driver();

    // Initialize UART for direct communication (no console)
    // Skip VFS console setup for raw UART communication
    
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s initialize controller failed: %s", __func__, esp_err_to_name(ret));
        printf("ERROR: BLE controller initialization failed: %s\n", esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        printf("ERROR: BLE controller enable failed: %s\n", esp_err_to_name(ret));
        return;
    }

    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));
        printf("ERROR: Bluetooth stack initialization failed: %s\n", esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));
        printf("ERROR: Bluetooth stack enable failed: %s\n", esp_err_to_name(ret));
        return;
    }

    //register the  callback function to the gap module
    ret = esp_ble_gap_register_callback(esp_gap_cb);
    if (ret){
        ESP_LOGE(GATTC_TAG, "%s gap register failed, error code = %x", __func__, ret);
        printf("ERROR: BLE GAP callback registration failed: %s\n", esp_err_to_name(ret));
        return;
    }

    //register the callback function to the gattc module
    ret = esp_ble_gattc_register_callback(esp_gattc_cb);
    if(ret){
        ESP_LOGE(GATTC_TAG, "%s gattc register failed, error code = %x", __func__, ret);
        return;
    }

    ret = esp_ble_gattc_app_register(PROFILE_A_APP_ID);
    if (ret){
        ESP_LOGE(GATTC_TAG, "%s gattc app register failed, error code = %x", __func__, ret);
    }
    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(500);
    if (local_mtu_ret){
        ESP_LOGE(GATTC_TAG, "set local  MTU failed, error code = %x", local_mtu_ret);
    }

    // Create UART communication task instead of console
    // xTaskCreate(uart_command_task, "uart_cmd_task", 4096, NULL, 10, NULL);
    
    // Create timer for continuous scanning
    esp_timer_create_args_t timer_args = {
        .callback = scan_timer_callback,
        .name = "scan_timer"
    };
    esp_timer_create(&timer_args, &scan_timer);
    
    ESP_LOGI(GATTC_TAG, "BLE GATT Client initialized. Starting continuous 5s scans...");
    printf("BLE Scanner initialized. Starting continuous 5-second scans...\n");
    
    // Start automatic 5-second scan after initialization
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait 1 second for full initialization
    start_ble_scan(SCAN_DURATION_MS);
}
