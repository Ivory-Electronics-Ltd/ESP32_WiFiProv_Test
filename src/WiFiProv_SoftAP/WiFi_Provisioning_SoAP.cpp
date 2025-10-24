/**
 * @file WiFi_Provisioning_SoAP.cpp
 * @brief Implements ESP32 Wi-Fi provisioning using the SoftAP scheme.
 *
 * This example starts the ESP32 in provisioning mode using the SoftAP transport. The device
 * creates a temporary access point so a user can connect and supply network credentials.
 * After credentials are received and validated, they are stored and can be retrieved using
 * ESP-IDF Wi-Fi APIs. The example demonstrates both event-driven logging and two methods of
 * reading stored credentials (direct NVS key probing and esp_wifi_get_config()).
 *
 * Key capabilities:
 * - SoftAP provisioning start via WiFiProv.beginProvision()
 * - QR code printing for mobile provisioning apps
 * - Event handling for all provisioning phases
 * - Credential extraction post-success
 * - Heap size reporting for diagnostics
 *
 * Security considerations:
 * - Retrieved credentials are printed to Serial for demonstration. Remove such prints in
 *   production firmware to avoid leaking sensitive data.
 * - POP (Proof of Possession) adds an extra layer preventing rogue provisioning attempts.
 *
 * Usage steps (serial log guidance):
 * 1. Device enters provisioning mode and announces SoftAP SSID + password.
 * 2. User connects to the AP and supplies target Wi-Fi credentials.
 * 3. On success, credentials are stored; example prints them via esp_wifi_get_config().
 *
 * @note The Arduino WiFi library does not expose password directly; ESP-IDF API esp_wifi_get_config() is used.
 * @warning Printing the password is for debugging only.
 */

#include <WiFi.h>
#include <WiFiProv.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include <cstring>

#include "wifi_provisioning/wifi_config.h"

// -----------------------------------------------------------------------------
// Mesh / delayed DHCP tuning macros (override via build_flags if needed)
// -----------------------------------------------------------------------------
#ifndef WIFI_CONNECT_DELAY_MS
#define WIFI_CONNECT_DELAY_MS 4000 // Delay after credentials before first connect (mesh steering settle)
#endif
#ifndef WIFI_ASSOC_TIMEOUT_MS
#define WIFI_ASSOC_TIMEOUT_MS 15000 // Time allowed to reach WL_CONNECTED (association/auth)
#endif
#ifndef WIFI_DHCP_TIMEOUT_MS_EXT
#define WIFI_DHCP_TIMEOUT_MS_EXT 45000 // Time allowed to obtain IP after WL_CONNECTED
#endif
#ifndef WIFI_RETRY_BACKOFF_MS
#define WIFI_RETRY_BACKOFF_MS 5000 // Base backoff between association retries
#endif
#ifndef WIFI_MAX_ASSOC_RETRIES
#define WIFI_MAX_ASSOC_RETRIES 5 // Maximum association attempts before giving up
#endif
#ifndef WIFI_SCAN_ON_FAIL
#define WIFI_SCAN_ON_FAIL 1 // Perform scan to pick best BSSID if repeated failures
#endif
#ifndef WIFI_LOCK_BEST_BSSID
#define WIFI_LOCK_BEST_BSSID 1 // Lock to strongest BSSID if scan enabled
#endif

/** @brief Captured Wi-Fi SSID after provisioning (populated via ESP-IDF config read). */
String captured_ssid = "";
/** @brief Captured Wi-Fi password after provisioning (populated via ESP-IDF config read). */
String captured_password = "";

/** @brief Proof of Possession token required by the provisioning app. */
const char *pop = "abcd1234";
/** @brief SoftAP service name (SSID) broadcasted during provisioning. */
const char *service_name = "SENSORE_rDAQ";
/** @brief SoftAP WPA2 password. Must be >= 8 chars. */
const char *service_key = "12345678";

/**
 * @brief If true, previously stored provisioning data will be cleared when beginProvision() is called.
 * @details Useful when forcing re-provisioning during development. Set false in production to retain credentials.
 */
bool reset_provisioned = true;

// -----------------------------------------------------------------------------
// Forward Declarations
// -----------------------------------------------------------------------------
/** @brief Provisioning + Wi-Fi system event handler callback. */
void SysProvEvent(arduino_event_t *sys_event);
/** @brief Reads stored credentials by probing potential NVS key names (may fail on newer cores). */
void readStoredCredentials();
/** @brief Reads stored credentials using official ESP-IDF Wi-Fi API (preferred). */
void readStoredCredentials_ESP_IDF();
/** @brief Periodic connection diagnostics (RSSI, latency probe, IP, uptime). */
void periodicConnectionDiagnostics();
/** @brief Initiates STA connection after provisioning and manages DHCP retries. */
void ensureStationConnected();

// -----------------------------------------------------------------------------
// Internal state for diagnostics
// -----------------------------------------------------------------------------
/** @brief Last millisecond timestamp when diagnostics were printed. */
static uint32_t last_diag_ms = 0;
/** @brief Interval between diagnostics prints (ms). */
static const uint32_t DIAG_INTERVAL_MS = 5000;
/** @brief Cached successful DNS resolution flag to avoid repeated lookups noise. */
static bool dns_test_done = false;
/** @brief Timestamp when a station connection attempt started (for DHCP timeout logic). */
static uint32_t connect_start_ms = 0;
/** @brief Flag indicating we invoked WiFi.begin() manually. */
static bool connection_initiated = false;
/** @brief Maximum milliseconds to wait for DHCP before retry. */
static const uint32_t DHCP_TIMEOUT_MS = 15000;

// Extended Wi-Fi connection state tracking for mesh / delayed DHCP environments
static uint32_t cred_ready_ms = 0;    // Timestamp when credentials became available
static uint8_t assoc_retries = 0;     // Association retry counter
static bool ip_obtained_once = false; // Whether we have ever gotten a valid IP

// Connection phase enum
enum WifiConnectPhase
{
    WIFI_PHASE_IDLE = 0,
    WIFI_PHASE_WAIT_DELAY,
    WIFI_PHASE_ASSOC,
    WIFI_PHASE_DHCP,
    WIFI_PHASE_STABLE
};
static WifiConnectPhase wifi_phase = WIFI_PHASE_IDLE;

// ===================
// Setup
// ===================
/**
 * @brief Arduino setup entry point.
 * @details Initializes serial logging, NVS, registers event handler, and starts SoftAP provisioning.
 */
void setup()
{
    Serial.begin(115200);
    delay(3000);

    Serial.printf("Free heap: %d bytes\n", esp_get_free_heap_size());

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Register event handler
    WiFi.onEvent(SysProvEvent);

    // WiFi.mode(WIFI_MODE_STA);
    // WiFi.disconnect(true);

    Serial.println("Begin Provisioning using SoftAP...");
    Serial.printf("Connect to WiFi: '%s'\n", service_name);
    Serial.printf("Password: '%s'\n", service_key);

    // Start SoftAP provisioning
    WiFiProv.beginProvision(
        WIFI_PROV_SCHEME_SOFTAP,
        WIFI_PROV_SCHEME_HANDLER_NONE,
        WIFI_PROV_SECURITY_1,
        pop,
        service_name,
        service_key,
        NULL,
        reset_provisioned);

    // ADD THIS LINE BACK:
    WiFiProv.printQR(service_name, pop, "softap");

    Serial.println("\nProvisioning Instructions:");
    Serial.println("1. Connect to WiFi hotspot: " + String(service_name));
    Serial.println("2. Use password: " + String(service_key));
    Serial.println("3. Open browser to 192.168.4.1");
    Serial.println("4. Enter your home WiFi credentials");
}

/**
 * @brief Arduino main loop.
 * @note Empty because provisioning flow is event-driven.
 */
void loop()
{
    // Provisioning remains event-driven; we add non-intrusive periodic diagnostics once connected.
    periodicConnectionDiagnostics();
}

// ===================
// Periodic connection diagnostics
// ===================
/**
 * @brief Prints Wi-Fi connection health every DIAG_INTERVAL_MS.
 * @details Reports RSSI, local IP, a lightweight TCP latency estimate to google.com (or fallback host), free heap, and uptime.
 *          Uses non-blocking timing via millis(). If not connected, skips latency probe and indicates status.
 * @note Latency probe opens a TCP socket to port 80 and measures connect time; not a true ICMP ping but works without extra libs.
 */
void periodicConnectionDiagnostics()
{
    uint32_t now = millis();
    if (now - last_diag_ms < DIAG_INTERVAL_MS)
        return; // not time yet
    last_diag_ms = now;

    // Attempt to progress connection (will no-op until credentials are captured)
    ensureStationConnected();

    wl_status_t st = WiFi.status();
    if (st != WL_CONNECTED)
    {
        const char *statusStr = "UNKNOWN";
        switch (st)
        {
        case WL_IDLE_STATUS:
            statusStr = "IDLE";
            break;
        case WL_NO_SSID_AVAIL:
            statusStr = "NO_SSID";
            break;
        case WL_SCAN_COMPLETED:
            statusStr = "SCAN_DONE";
            break;
        case WL_CONNECTED:
            statusStr = "CONNECTED";
            break;
        case WL_CONNECT_FAILED:
            statusStr = "CONNECT_FAIL";
            break;
        case WL_CONNECTION_LOST:
            statusStr = "CONN_LOST";
            break;
        case WL_DISCONNECTED:
            statusStr = "DISCONNECTED";
            break;
        }
        const char *phaseStr = (wifi_phase == WIFI_PHASE_IDLE) ? "IDLE" : (wifi_phase == WIFI_PHASE_WAIT_DELAY) ? "WAIT_DELAY"
                                                                      : (wifi_phase == WIFI_PHASE_ASSOC)        ? "ASSOC"
                                                                      : (wifi_phase == WIFI_PHASE_DHCP)         ? "DHCP"
                                                                                                                : "STABLE";
        Serial.printf("[DIAG] Phase=%s status=%s Heap=%u Uptime=%lus\n", phaseStr, statusStr, esp_get_free_heap_size(), now / 1000UL);
        return;
    }

    // RSSI and IP
    int32_t rssi = WiFi.RSSI();
    IPAddress ip = WiFi.localIP();
    uint32_t ip_raw = (uint32_t)ip; // avoid ambiguous operator== overloads causing crash
    // Ensure a non-empty SSID (WiFi.SSID() can temporarily be empty); fallback to captured value or esp_wifi_get_config
    String liveSSID = WiFi.SSID();
    if (!liveSSID.length() && captured_ssid.length())
        liveSSID = captured_ssid;
    if (!liveSSID.length())
    {
        wifi_config_t cfg;
        if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK)
            liveSSID = String((char *)cfg.sta.ssid);
        if (liveSSID.length())
            captured_ssid = liveSSID; // cache
    }

    // Decide whether to attempt latency probe (skip if IP not yet assigned)
    String latencyStr = "SKIP";
    if (ip_raw != 0)
    {
        const char *host = "google.com";
        uint16_t port = 80;
        uint32_t t_start = millis();
        WiFiClient client;
        bool connected = client.connect(host, port);
        if (!connected && !dns_test_done)
        {
            connected = client.connect("142.250.72.206", port);
            if (connected)
                dns_test_done = true;
        }
        uint32_t latency_ms = millis() - t_start;
        if (connected)
        {
            client.stop();
            latencyStr = String(latency_ms) + "ms";
        }
        else
        {
            latencyStr = "FAIL";
        }
    }

    const char *phaseStr = (wifi_phase == WIFI_PHASE_IDLE) ? "IDLE" : (wifi_phase == WIFI_PHASE_WAIT_DELAY) ? "WAIT_DELAY"
                                                                  : (wifi_phase == WIFI_PHASE_ASSOC)        ? "ASSOC"
                                                                  : (wifi_phase == WIFI_PHASE_DHCP)         ? "DHCP"
                                                                                                            : "STABLE";
    Serial.printf("[DIAG] Phase=%s SSID='%s' RSSI=%ddBm IP=%s TCP_Lat=%s Heap=%u Up=%lus\n",
                  phaseStr, liveSSID.c_str(), rssi, ip.toString().c_str(), latencyStr.c_str(), esp_get_free_heap_size(), now / 1000UL);

    // Optional: once we have credentials captured, remind.
    if (captured_ssid.length())
    {
        Serial.printf("[DIAG] Provisioned SSID='%s' (stored password length=%u)\n", captured_ssid.c_str(), (unsigned)captured_password.length());
    }
}

// ===================
// Event handler
// ===================
/**
 * @brief Handles provisioning and Wi-Fi related events.
 * @param sys_event Pointer to the Arduino event structure containing event id and associated data.
 * @details Responds to provisioning start, credential reception, success/fail, end, and Wi-Fi link events.
 */
void SysProvEvent(arduino_event_t *sys_event)
{
    switch (sys_event->event_id)
    {
    case ARDUINO_EVENT_PROV_START:
        Serial.println("Provisioning started. Connect to SoftAP to configure WiFi.");
        break;

    case ARDUINO_EVENT_PROV_CRED_RECV:
        Serial.printf("Received SSID: %s\n", sys_event->event_info.prov_cred_recv.ssid);
        // Note: Password is NOT available in sys_event for SoftAP mode
        Serial.println("Password: <not accessible via event in SoftAP mode>");
        break;

    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
        Serial.println("Provisioning Successful!");
        // Try to read stored credentials from NVS after successful provisioning
        // readStoredCredentials();
        readStoredCredentials_ESP_IDF();
        cred_ready_ms = millis();
        wifi_phase = WIFI_PHASE_WAIT_DELAY;
        assoc_retries = 0;
        connection_initiated = false;
        ip_obtained_once = false;
        break;

    case ARDUINO_EVENT_PROV_CRED_FAIL:
        Serial.println("Provisioning Failed. Check credentials and retry.");
        break;

    case ARDUINO_EVENT_PROV_END:
        Serial.println("Provisioning Ended.");
        break;

    // WiFi connection events
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        Serial.println("WiFi Connected Successfully!");
        Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
        ip_obtained_once = true;
        wifi_phase = WIFI_PHASE_STABLE;
        break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        Serial.println("WiFi Disconnected");
        break;

    default:
        break;
    }
}

// ===================
// Read stored credentials from NVS - CORRECTED VERSION
// ===================
/**
 * @brief Attempts to read Wi-Fi credentials directly from NVS by probing multiple key names.
 * @note This method may fail depending on internal key naming changes across framework versions.
 * @warning Password is printed in plain text for debugging. Remove in production.
 */
void readStoredCredentials()
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Try the correct NVS namespace for WiFi credentials
    err = nvs_open("nvs.net80211", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        // Try alternative namespace
        err = nvs_open("wifi.sta", NVS_READONLY, &nvs_handle);
        if (err != ESP_OK)
        {
            Serial.println("Failed to open WiFi NVS namespace");
            return;
        }
    }

    // Read SSID - try multiple possible key names
    size_t ssid_len = 33;
    char ssid[33] = {0};
    err = nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len);
    if (err != ESP_OK)
    {
        err = nvs_get_str(nvs_handle, "sta.ssid", ssid, &ssid_len);
    }

    if (err == ESP_OK)
    {
        Serial.printf("Stored SSID: %s\n", ssid);
    }
    else
    {
        Serial.println("Failed to read SSID from NVS");
    }

    // Read Password - try multiple possible key names
    size_t pass_len = 65;
    char password[65] = {0};
    err = nvs_get_str(nvs_handle, "password", password, &pass_len);
    if (err != ESP_OK)
    {
        err = nvs_get_str(nvs_handle, "sta.pswd", password, &pass_len);
    }
    if (err != ESP_OK)
    {
        err = nvs_get_str(nvs_handle, "passwd", password, &pass_len);
    }

    if (err == ESP_OK)
    {
        Serial.printf("Stored Password: %s\n", password);
        Serial.printf("Password Length: %d characters\n", strlen(password));
    }
    else
    {
        Serial.printf("Failed to read password from NVS (error: 0x%x)\n", err);
    }

    nvs_close(nvs_handle);
}

// ===================
// Read stored credentials using ESP-IDF WiFi API
// ===================
/**
 * @brief Reads Wi-Fi credentials using ESP-IDF's esp_wifi_get_config().
 * @details Preferred method—returns current station config regardless of underlying NVS key layout.
 * @warning Printing credentials is for debugging only.
 */
void readStoredCredentials_ESP_IDF()
{
    wifi_config_t wifi_cfg;
    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);

    if (err == ESP_OK)
    {
        Serial.printf("Stored SSID: %s\n", (char *)wifi_cfg.sta.ssid);
        Serial.printf("Stored Password: %s\n", (char *)wifi_cfg.sta.password);
        Serial.printf("Password Length: %d characters\n", strlen((char *)wifi_cfg.sta.password));
        captured_ssid = String((char *)wifi_cfg.sta.ssid);
        captured_password = String((char *)wifi_cfg.sta.password);
        if (wifi_phase == WIFI_PHASE_IDLE)
        {
            cred_ready_ms = millis();
            wifi_phase = WIFI_PHASE_WAIT_DELAY; // ensure state machine picks it up
        }
    }
    else
    {
        Serial.printf("Failed to read WiFi config (error: 0x%x)\n", err);
    }
}

// ===================
// Ensure station connection & DHCP completion
// ===================
/**
 * @brief Starts or maintains the station connection after credentials are known.
 * @details If credentials captured and not yet connected, calls WiFi.begin(). If IP not acquired within
 *          DHCP_TIMEOUT_MS while status is WL_CONNECTED but IP is 0.0.0.0, triggers a reconnect sequence.
 */
void ensureStationConnected()
{
    if (!captured_ssid.length())
        return; // No credentials yet

    wl_status_t st = WiFi.status();
    IPAddress ip = WiFi.localIP();
    uint32_t ip_raw = (uint32_t)ip; // raw numeric form for comparisons
    uint32_t now = millis();

    switch (wifi_phase)
    {
    case WIFI_PHASE_IDLE:
        // Should be set by provisioning success; safeguard for manual credential read
        cred_ready_ms = now;
        wifi_phase = WIFI_PHASE_WAIT_DELAY;
        break;

    case WIFI_PHASE_WAIT_DELAY:
        if (now - cred_ready_ms >= WIFI_CONNECT_DELAY_MS)
        {
            Serial.printf("[CONNECT] Starting association to '%s' after %ums delay\n", captured_ssid.c_str(), WIFI_CONNECT_DELAY_MS);
            WiFi.mode(WIFI_MODE_STA);
#if WIFI_SCAN_ON_FAIL
            // Optional pre-association scan to lock strongest BSSID
            if (captured_ssid.length())
            {
                int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
                int bestIdx = -1;
                int bestRSSI = -999;
                for (int i = 0; i < n; i++)
                {
                    String s = WiFi.SSID(i);
                    if (s == captured_ssid)
                    {
                        int r = WiFi.RSSI(i);
                        if (r > bestRSSI)
                        {
                            bestRSSI = r;
                            bestIdx = i;
                        }
                    }
                }
                if (bestIdx >= 0)
                {
                    uint8_t bssid[6];
                    const uint8_t *bssidPtr = WiFi.BSSID(bestIdx);
                    if (bssidPtr)
                        memcpy(bssid, bssidPtr, 6);
                    else
                        memset(bssid, 0, 6);
                    Serial.printf("[SCAN] Best BSSID selected RSSI=%d -> %02X:%02X:%02X:%02X:%02X:%02X\n", bestRSSI,
                                  bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
#if WIFI_LOCK_BEST_BSSID
                    wifi_config_t current_cfg;
                    if (esp_wifi_get_config(WIFI_IF_STA, &current_cfg) == ESP_OK)
                    {
                        memcpy(current_cfg.sta.bssid, bssid, 6);
                        current_cfg.sta.bssid_set = 1;
                        esp_wifi_set_config(WIFI_IF_STA, &current_cfg);
                        Serial.println("[SCAN] Locked STA config to best BSSID");
                    }
#endif
                }
                else
                {
                    Serial.println("[SCAN] No matching BSSID found during pre-association scan");
                }
            }
#endif
            WiFi.begin(captured_ssid.c_str(), captured_password.c_str());
            connection_initiated = true;
            connect_start_ms = now;
            assoc_retries = 0;
            wifi_phase = WIFI_PHASE_ASSOC;
        }
        break;

    case WIFI_PHASE_ASSOC:
        if (st == WL_CONNECTED)
        {
            Serial.println("[CONNECT] Association/auth complete. Waiting for DHCP...");
            wifi_phase = WIFI_PHASE_DHCP;
            connect_start_ms = now;
        }
        else if (now - connect_start_ms > WIFI_ASSOC_TIMEOUT_MS)
        {
            if (assoc_retries < WIFI_MAX_ASSOC_RETRIES)
            {
                assoc_retries++;
                uint32_t backoff = WIFI_RETRY_BACKOFF_MS * assoc_retries;
                Serial.printf("[CONNECT] Assoc timeout (%ums). Retry %u/%u in %ums\n", WIFI_ASSOC_TIMEOUT_MS, assoc_retries, WIFI_MAX_ASSOC_RETRIES, backoff);
                delay(50);
                WiFi.disconnect();
#if WIFI_SCAN_ON_FAIL
                // Re-scan before retry to handle mesh channel steering
                int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
                int bestIdx = -1;
                int bestRSSI = -999;
                for (int i = 0; i < n; i++)
                {
                    if (WiFi.SSID(i) == captured_ssid)
                    {
                        int r = WiFi.RSSI(i);
                        if (r > bestRSSI)
                        {
                            bestRSSI = r;
                            bestIdx = i;
                        }
                    }
                }
                if (bestIdx >= 0)
                {
                    uint8_t bssid[6];
                    const uint8_t *bssidPtr = WiFi.BSSID(bestIdx);
                    if (bssidPtr)
                        memcpy(bssid, bssidPtr, 6);
                    else
                        memset(bssid, 0, 6);
                    Serial.printf("[SCAN] Retry best RSSI=%d BSSID=%02X:%02X:%02X:%02X:%02X:%02X\n", bestRSSI,
                                  bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
#if WIFI_LOCK_BEST_BSSID
                    wifi_config_t current_cfg;
                    if (esp_wifi_get_config(WIFI_IF_STA, &current_cfg) == ESP_OK)
                    {
                        memcpy(current_cfg.sta.bssid, bssid, 6);
                        current_cfg.sta.bssid_set = 1;
                        esp_wifi_set_config(WIFI_IF_STA, &current_cfg);
                        Serial.println("[SCAN] Locked STA config to best BSSID (retry)");
                    }
#endif
                }
                else
                {
                    Serial.println("[SCAN] Retry scan found no matching BSSID");
                }
#endif
                delay(backoff);
                WiFi.begin(captured_ssid.c_str(), captured_password.c_str());
                connect_start_ms = millis();
            }
            else
            {
                Serial.println("[CONNECT] Association failed after max retries; staying idle.");
                wifi_phase = WIFI_PHASE_IDLE;
            }
        }
        break;

    case WIFI_PHASE_DHCP:
        if (ip_raw != 0)
        {
            Serial.printf("[CONNECT] DHCP lease acquired: %s\n", ip.toString().c_str());
            wifi_phase = WIFI_PHASE_STABLE;
            ip_obtained_once = true;
            connection_initiated = false;
        }
        else if (now - connect_start_ms > WIFI_DHCP_TIMEOUT_MS_EXT)
        {
            Serial.printf("[CONNECT] DHCP timeout (%ums). Cycling interface...\n", WIFI_DHCP_TIMEOUT_MS_EXT);
            WiFi.disconnect();
            delay(100);
            WiFi.begin(captured_ssid.c_str(), captured_password.c_str());
            connect_start_ms = millis();
            wifi_phase = WIFI_PHASE_ASSOC; // start from assoc again
        }
        break;

    case WIFI_PHASE_STABLE:
        if (st != WL_CONNECTED)
        {
            Serial.println("[CONNECT] Connection lost. Returning to ASSOC phase.");
            wifi_phase = WIFI_PHASE_ASSOC;
            connect_start_ms = now;
        }
        else if (ip_raw == 0)
        {
            Serial.println("[CONNECT] IP lost. Re-entering DHCP phase.");
            wifi_phase = WIFI_PHASE_DHCP;
            connect_start_ms = now;
        }
        break;
    }
}