#ifndef SETTINGS_LOG_H
#define SETTINGS_LOG_H

#include "esp32_settings.h"

#ifdef SETTINGS_CORE_ESP32
    #include "esp_log.h"
    
    // Use ESP32 logging directly
    #define SETTINGS_LOGI(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
    #define SETTINGS_LOGW(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
    #define SETTINGS_LOGE(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
    #define SETTINGS_LOGD(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)
    #define SETTINGS_LOGV(tag, format, ...) ESP_LOGV(tag, format, ##__VA_ARGS__)
    
#else
    // RP2040 or other platforms - use Serial
    #include "Arduino.h"
    
    #define SETTINGS_LOGI(tag, format, ...) \
        do { \
            Serial.printf("[I][%s] ", tag); \
            Serial.printf(format, ##__VA_ARGS__); \
            Serial.println(); \
        } while(0)
    
    #define SETTINGS_LOGW(tag, format, ...) \
        do { \
            Serial.printf("[W][%s] ", tag); \
            Serial.printf(format, ##__VA_ARGS__); \
            Serial.println(); \
        } while(0)
    
    #define SETTINGS_LOGE(tag, format, ...) \
        do { \
            Serial.printf("[E][%s] ", tag); \
            Serial.printf(format, ##__VA_ARGS__); \
            Serial.println(); \
        } while(0)
    
    #define SETTINGS_LOGD(tag, format, ...) \
        do { \
            Serial.printf("[D][%s] ", tag); \
            Serial.printf(format, ##__VA_ARGS__); \
            Serial.println(); \
        } while(0)
    
    #define SETTINGS_LOGV(tag, format, ...) \
        do { \
            Serial.printf("[V][%s] ", tag); \
            Serial.printf(format, ##__VA_ARGS__); \
            Serial.println(); \
        } while(0)
    
#endif

#endif // SETTINGS_LOG_H