# esp32-settings — ESP-IDF component wrapper.
# LittleFS-backed persistence for a global-settings blob and a presets blob on the
# `storage` partition. ESP-IDF v6+ only (no Arduino support).
#
# Only `littlefs` is listed: esp_restart (esp_system), esp_rom_crc32_le (esp_rom),
# vTaskDelay (freertos) and ESP_LOG (log) come from the always-available common components.
idf_component_register(SRCS "src/esp32_settings.c"
                            "src/esp32_settings_tlv.c"
                       INCLUDE_DIRS "src"
                       PRIV_REQUIRES littlefs)
