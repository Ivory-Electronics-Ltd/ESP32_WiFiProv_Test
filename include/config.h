/**
 * @file config.h
 * @brief Configuration settings for ESP32 WiFi Provisioning
 */

#ifndef CONFIG_H
#define CONFIG_H

// Provisioning Configuration
#define PROV_POP "abcd1234"                    // Proof of Possession (PIN)
#define PROV_SERVICE_NAME "PROV_Sensore"         // BLE Service name (must start with PROV_)
#define PROV_SERVICE_KEY NULL                  // Not used for BLE
#define PROV_RESET_PROVISIONED true           // Auto-delete previous credentials

// Debug Configuration
#define ENABLE_DEBUG_OUTPUT true
#define STATUS_UPDATE_INTERVAL 5000            // Status print interval in ms

// WiFi Configuration
#define WIFI_CONNECTION_TIMEOUT 30000          // WiFi connection timeout in ms
#define WIFI_RETRY_DELAY 5000                  // Delay between WiFi reconnection attempts

// BLE UUID Configuration (optional custom UUID)
#define USE_CUSTOM_UUID true
#define CUSTOM_UUID {0xb4, 0xdf, 0x5a, 0x1c, \
                    0x3f, 0x6b, 0xf4, 0xbf, \
                    0xea, 0x4a, 0x82, 0x03, \
                    0x04, 0x90, 0x1a, 0x02}


#endif // CONFIG_H