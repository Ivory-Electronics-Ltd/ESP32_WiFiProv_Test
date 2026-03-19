/**
 * @file WiFi_Provisioning.cpp
 * @brief BLE-based Wi-Fi provisioning example for ESP32 (Arduino framework).
 *
 * This module demonstrates how to use the Arduino WiFi Provisioning (WiFiProv) API to
 * provision Wi-Fi credentials over BLE using a Proof of Possession (POP) and a device
 * service name. A mobile provisioning application (e.g. ESP SoftAP & BLE Provisioning
 * app) can scan the QR code emitted over serial to automatically populate necessary
 * provisioning parameters.
 *
 * Features:
 * - Starts BLE provisioning service (scheme BLE + FREE_BLE handler).
 * - Prints a scannable QR code with POP + service name.
 * - Logs provisioning lifecycle events (start, credentials received, success, fail, end).
 * - Optionally clears prior provisioning data (when `reset_provisioned` is true).
 *
 * Usage:
 * 1. Flash this firmware to an ESP32.
 * 2. Open the serial monitor at 115200 baud.
 * 3. Scan the printed QR code with the official provisioning app (or manually enter fields).
 * 4. Supply target Wi-Fi SSID and password in the app.
 * 5. Observe event callbacks confirming success or failure.
 *
 * Security Notes:
 * - The POP should not be a trivial string in production; choose a sufficiently random token.
 * - Avoid printing received credentials to logs in production builds; kept here for demonstration.
 *
 * Build Dependencies:
 * - Arduino core for ESP32.
 * - WiFiProv library (bundled with Arduino ESP32 core >= 2.x).
 *
 * Limitations:
 * - Example is minimal; no automatic reconnection or post-provision Wi-Fi usage logic is added.
 * - Loop is intentionally empty—provisioning is event-driven.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiProv.h>
#include "nvs_flash.h"

/** @brief Proof of Possession (POP) string required by the provisioning app for authentication.
 *  Change this to a secure random value for production.
 */
const char *pop = "abcd1234";
/** @brief Service / device name advertised during provisioning (also used in QR code). */
const char *service_name = "PROV_Sensore"; //"SENSORE_rDAQ"; // "PROV_123";
/** @brief Optional SoftAP password; unused for BLE scheme. Set NULL to omit. */
const char *service_key = NULL;

/** @brief If true, stored provisioning data (NVS) will be erased when starting provisioning.
 *  This forces re-provisioning. Set to false to retain existing credentials.
 */
bool reset_provisioned = true;


//* Function declarations */
void clearProvisioning();
void SysProvEvent(arduino_event_t *sys_event);

// ===================
// Setup
// ===================
/**
 * @brief Arduino setup entry point initializing BLE provisioning.
 *
 * Responsibilities:
 * - Initialize Serial console and delay for monitor attachment.
 * - Initialize NVS storage (performing erase/re-init if needed).
 * - Register the provisioning event handler.
 * - Prepare Wi-Fi subsystem (idle station mode, erase past connection state).
 * - Start BLE provisioning with security mode 1 (Proof of Possession required).
 * - Emit a QR code to serial for easy mobile app provisioning.
 */
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
/**
 * @brief Main application loop. Intentionally empty because provisioning is fully event-driven.
 *
 * You may extend this loop to react after provisioning success (e.g., start MQTT, HTTP server,
 * or other application logic once Wi-Fi credentials are in place and a connection is established).
 */
void loop()
{
    // Nothing needed here
}


// ===================
// Event handler
// ===================
/**
 * @brief Global provisioning + Wi-Fi system event callback registered with WiFi.onEvent().
 *
 * Handles high-level provisioning lifecycle notifications and logs relevant information:
 * - Start of provisioning
 * - Credentials reception (SSID + password)
 * - Success / failure notifications
 * - End of provisioning session
 *
 * @param sys_event Pointer to the Arduino event structure describing the current event.
 */
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
/**
 * @brief Erase all stored provisioning data (Wi-Fi + BLE) from NVS flash.
 *
 * This is a destructive operation clearing the entire NVS partition. It is useful for
 * development cycles where repeated provisioning is necessary or to ensure no stale
 * credentials remain. Requires NVS to be initialized first.
 */
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
