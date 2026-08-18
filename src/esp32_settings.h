#ifndef ESP32_SETTINGS_H
#define ESP32_SETTINGS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_FIRST_BOOT_VALUE 100
#define DEVICE_CONFIGURED_VALUE 114 // Arbitrary value to indicate the device has been configured

// `banks` and/or `presets` may be NULL to indicate that storage element is unused:
// it is then ignored entirely (no size validation, existence check, load or save).
// `globalSettings` and `bootFlag` are always required.
uint8_t esp32Settings_BootCheck(	void* globalSettings, uint16_t gSize,
										void* banks, uint16_t bSize, size_t numBanks,
										void* presets, uint16_t pSize, size_t numPresets,
										uint8_t* bootFlag);
void esp32Settings_NewDeviceConfig();
void esp32Settings_StandardBoot();
void esp32Settings_SoftwareReset();

// Settings callbacks
void esp32Settings_AssignDefaultGlobalSettings(void (*fptr)());
void esp32Settings_AssignDefaultBankSettings(void (*fptr)());
void esp32Settings_AssignDefaultPresetSettings(void (*fptr)());
void esp32Settings_ResetAllSettings();
void esp32Settings_ReadGlobalSettings();
void esp32Settings_SaveGlobalSettings();
void esp32Settings_ReadBanks();
void esp32Settings_SaveBanks();
void esp32Settings_ReadPresets();
void esp32Settings_SavePresets();

#ifdef __cplusplus
}
#endif

#endif // ESP32_SETTINGS_H
