#ifndef ESP32_SETTINGS_H
#define ESP32_SETTINGS_H
#include <Arduino.h>
#include "stdlib.h"

void esp32Settings_BootCheck(	void* globalSettings, uint16_t gSize, void* presets,
										uint16_t pSize, size_t numPresets, uint8_t* bootFlag);
void esp32Settings_NewDeviceConfig();
void esp32Settings_StandardBoot();
void esp32Settings_SoftwareReset();

// Settings callbacks
void esp32Settings_AssignDefaultGlobalSettings(void (*fptr)());
void esp32Settings_AssignDefaultPresetSettings(void (*fptr)());
void esp32Settings_ResetAllSettings();
void esp32Settings_ReadGlobalSettings();
void esp32Settings_SaveGlobalSettings();
void esp32Settings_ReadPresets();
void esp32Settings_SavePresets();

#endif // ESP32_SETTINGS_H