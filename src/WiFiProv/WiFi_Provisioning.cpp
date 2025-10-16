#include <WiFi.h>
#include <WiFiProv.h>
#include "nvs_flash.h"

// Proof of possession
const char *pop = "abcd1234";
// Device name
const char *service_name = "PROV_123"; //"SENSORE_rDAQ"; // "PROV_123";
// Optional SoftAP password (NULL = no password)
const char *service_key = NULL;

// Reset provisioning flag
bool reset_provisioned = true;

// ===================
// Event handler
// ===================
void SysProvEvent(arduino_event_t *sys_event)
{
    switch (sys_event->event_id)
    {
    case ARDUINO_EVENT_PROV_START:
        Serial.println("Provisioning started. Use smartphone app to configure Wi-Fi.");
        break;

    case ARDUINO_EVENT_PROV_CRED_RECV:

        Serial.printf("Received SSID: %s\n", sys_event->event_info.prov_cred_recv.ssid);
        Serial.printf("Received Password: %s\n", sys_event->event_info.prov_cred_recv.password);
        break;

    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
        Serial.println("Provisioning Successful!");
        break;

    case ARDUINO_EVENT_PROV_CRED_FAIL:
        Serial.println("Provisioning Failed. Check credentials and retry.");
        break;

    case ARDUINO_EVENT_PROV_END:
        Serial.println("Provisioning Ended.");
        break;

    default:
        break;
    }
}

// ===================
// Clear old provisioning
// ===================
void clearProvisioning()
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    // Erase all NVS data (Wi-Fi + BLE)
    ESP_ERROR_CHECK(nvs_flash_erase());

    Serial.println("All previous Wi-Fi/BLE provisioning cleared!");
}

// ===================
// Setup
// ===================
void setup()
{
    Serial.begin(115200);
    delay(3000);

    Serial.println(__FILE__);

    // Check available heap before starting BLE
    Serial.printf("Free heap before BLE init: %d bytes\n", esp_get_free_heap_size());

    // Initialize NVS first (required for BLE)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Register event handler
    WiFi.onEvent(SysProvEvent);

    // Ensure Wi-Fi is idle
    WiFi.mode(WIFI_MODE_STA);
    WiFi.disconnect(true);

    Serial.println("Begin Provisioning using BLE...");

    // Start provisioning (BLE)
    WiFiProv.beginProvision(
        WIFI_PROV_SCHEME_BLE,
        WIFI_PROV_SCHEME_HANDLER_FREE_BLE,
        WIFI_PROV_SECURITY_1,
        pop,
        service_name,
        service_key,
        NULL,
        reset_provisioned);

    // Print QR code to serial
    WiFiProv.printQR(service_name, pop, "ble");
}

// ===================
// Main loop
// ===================
void loop()
{
    // Nothing needed here
}
