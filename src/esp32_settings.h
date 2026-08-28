#ifndef ESP32_SETTINGS_H
#define ESP32_SETTINGS_H

#include <stdint.h>
#include <stddef.h>

#include "esp32_settings_tlv.h"   // TLV codec + settings_serialize_fn / _deserialize_fn

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_FIRST_BOOT_VALUE 100
#define DEVICE_CONFIGURED_VALUE 114 // Arbitrary value to indicate the device has been configured

// `banks` and/or `presets` may be NULL to indicate that storage element is unused:
// it is then ignored entirely (no size validation, existence check, load or save).
// `globalStore` and `bootFlag` are always required.
// NOTE: the first param is named `globalStore`, not `globalSettings`, on purpose —
// some consumers `#define globalSettings (*ptr)` (PSRAM offload), which would expand
// inside this declaration and corrupt the signature.
uint8_t esp32Settings_BootCheck(	void* globalStore, uint16_t gSize,
										void* banks, uint16_t bSize, size_t numBanks,
										void* presets, uint16_t pSize, size_t numPresets,
										uint8_t* bootFlag);

// Forward-compatible (TLV) boot check. Decodes tagged streams via the caller's
// (de)serialise callbacks instead of validating a raw struct blob by length, so
// evolving GlobalSettings/Preset never wipes user settings. Register the
// default-assignment callbacks first (esp32Settings_AssignDefault*); banks are not
// supported in tagged mode. `presets` and its callbacks may be NULL if unused.
// See esp32_settings_tlv.h and docs/settings-migration-plan.md.
uint8_t esp32Settings_BootCheckTagged(
		void* globalStore, settings_serialize_fn gSerialize, settings_deserialize_fn gDeserialize,
		void* presets, settings_serialize_fn pSerialize, settings_deserialize_fn pDeserialize,
		uint8_t* bootFlag, void* ctx);
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
