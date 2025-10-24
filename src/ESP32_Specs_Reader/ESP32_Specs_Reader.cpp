#include <Arduino.h>
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"

static void printMac(const char *label, esp_mac_type_t type)
{
    uint8_t mac[6];
    if (esp_read_mac(mac, type) == ESP_OK)
    {
        Serial.printf("%s MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", label, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    else
    {
        Serial.printf("%s MAC: <error reading>\n", label);
    }
}

void setup()
{
    Serial.begin(115200);

    delay(1000);

    Serial.println(__FILE__);

    Serial.println();
    Serial.println("===== ESP32 Board Specifications =====");

    // Chip info
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    Serial.printf("Chip Model: %s\n", (chip_info.model == CHIP_ESP32) ? "ESP32" : "Unknown/Other");
    Serial.printf("Chip Features: WiFi%s%s, %d cores\n",
                  (chip_info.features & CHIP_FEATURE_BT) ? ", BT" : "",
                  (chip_info.features & CHIP_FEATURE_BLE) ? ", BLE" : "",
                  chip_info.cores);

    Serial.printf("Silicon Revision: %d\n", chip_info.revision);

    // Flash size probing
    esp_flash_t *flash = esp_flash_default_chip;

    uint32_t flash_size = 0;

    if (esp_flash_get_size(flash, &flash_size) == ESP_OK)
    {
        Serial.printf("Detected Flash Size: %u KB (%u MB)\n", flash_size / 1024, flash_size / (1024 * 1024));
    }
    else
    {
        Serial.println("Detected Flash Size: <error>");
    }

    // Flash configuration (from build-time settings)
    Serial.printf("Flash Configuration: Check platformio.ini for mode/speed settings\n");

    // PSRAM
#if CONFIG_SPIRAM_SUPPORT
    size_t psram_size = esp_spiram_get_size();
    Serial.printf("PSRAM Supported: Yes, Size: %u KB (%u MB)\n", psram_size / 1024, psram_size / (1024 * 1024));
#else
    Serial.println("PSRAM Supported: No");
#endif

    // Heap / memory caps
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free_heap = esp_get_minimum_free_heap_size();

    Serial.printf("Free Heap: %u bytes\n", free_heap);
    Serial.printf("Minimum Free Heap (lifetime low): %u bytes\n", min_free_heap);

#if CONFIG_SPIRAM_SUPPORT
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    Serial.printf("Free PSRAM Heap: %u bytes\n", free_psram);
#endif

    // MAC addresses
    printMac("Factory Base", ESP_MAC_WIFI_STA);
    printMac("WiFi SoftAP", ESP_MAC_WIFI_SOFTAP);
#if CONFIG_BT_ENABLED
    printMac("BT", ESP_MAC_BT);
#endif
#if CONFIG_ETH_USE_ESP32_EMAC
    printMac("Ethernet", ESP_MAC_ETH);
#endif

    // Security features (simplified - some APIs vary by ESP-IDF version)
    Serial.println("Security Features: Check via menuconfig or eFuse dump for details");

    // Partition table info (print first OTA/app partition sizes)
    Serial.println("\nPartition Table:");
    const esp_partition_t *part = NULL;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it)
    {
        part = esp_partition_get(it);
        Serial.printf("APP Partition: label=%s, addr=0x%06X, size=%u KB\n", part->label, part->address, part->size / 1024);
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);

    it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it)
    {
        part = esp_partition_get(it);
        Serial.printf("DATA Partition: label=%s, addr=0x%06X, size=%u KB\n", part->label, part->address, part->size / 1024);
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);

    Serial.println("======================================");
}

void loop()
{
    // Nothing; specs printed once.
}
