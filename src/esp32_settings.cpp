#include "esp32_settings.h"
#include "esp32_settings_log.h"
#include <LittleFS.h>
#include "Arduino.h"

#define FORMAT_LITTLEFS_IF_FAILED true
#define DEVICE_CONFIGURED_VALUE 114 // Arbitrary value to indicate the device has been configured

// Both ESP32 and RP2040 use string modes for file operations
#define SETTINGS_FILE_WRITE_MODE "w"
#define SETTINGS_FILE_READ_MODE "r"

static const char *SETTINGS_TAG = "ESP32_SETTINGS";

void* globalSettingsPtr = NULL;
void* presetsPtr = NULL;
uint8_t* bootFlagPtr = NULL;
size_t numPresets = 0;

uint16_t globalSettingsSize = 0;
uint16_t presetSize = 0;

void (*assignDefaultGlobalSettings)() = nullptr;
void (*assignDefaultPresetSettings)() = nullptr;

void esp32Settings_ListDir(fs::FS &fs, const char *dirname, uint8_t levels);

// Checks for the standard combination of a global settings file and a presets file
// If the files are not present or the sizes do not match, it will format the file
// This function also initialises the global and preset settings pointers as well as the number of presets
// The bootflag is a pointer to the global settings boot state flag
uint8_t esp32Settings_BootCheck(	void* globalSettings, uint16_t gSize, void* presets,
										uint16_t pSize, size_t num, uint8_t* bootFlag)
{
	uint8_t bootFlagValue = 0;
	SETTINGS_LOGI(SETTINGS_TAG, "Preset size %d", pSize);
	// Perform the boot check
	SETTINGS_LOGI(SETTINGS_TAG, "Boot check initiated.");
	if (globalSettings == nullptr || presets == nullptr
		|| num == 0 || bootFlag == nullptr)
	{
		SETTINGS_LOGE(SETTINGS_TAG, "Invalid settings or presets pointers.");
		return 0;
	}

	// Assign the global settings and presets pointers
	globalSettingsPtr = globalSettings;
	presetsPtr = presets;
	bootFlagPtr = bootFlag;
	numPresets = num;
	globalSettingsSize = gSize;
	presetSize = pSize;
	
	// Check if an appropriate file system is available
	SETTINGS_LOGI(SETTINGS_TAG, "Checking boot state...");
	if (!LittleFS.begin())
	{
		SETTINGS_LOGI(SETTINGS_TAG, "LittleFS Mount Failed. Formatting...");
		esp32Settings_NewDeviceConfig();
	}

	// Check for the correct file structures
	SETTINGS_LOGI(SETTINGS_TAG, "Checking file system...");
	uint8_t structureOk = 1;
	if (!LittleFS.exists("/global.txt"))
		structureOk = 0;

	if (!LittleFS.exists("/presets.txt"))
		structureOk = 0;

	if (!structureOk)
	{
		SETTINGS_LOGI(SETTINGS_TAG, "File system structure incorrect.");
		esp32Settings_NewDeviceConfig();
	}

	// Check the global settings size from the file system
	SETTINGS_LOGI(SETTINGS_TAG, "Validating global config size...");
	File globalConfigFile = LittleFS.open("/global.txt", SETTINGS_FILE_READ_MODE);
	size_t globalConfigFileSize = globalConfigFile.size();
	SETTINGS_LOGI(SETTINGS_TAG, "Global config file size: %d", globalConfigFileSize);
	if (globalConfigFileSize != globalSettingsSize)
	{
		SETTINGS_LOGI(SETTINGS_TAG, "Global settings file size does not match.");
		esp32Settings_NewDeviceConfig();
	}
	globalConfigFile.close();

	// Read the global settings
	esp32Settings_ReadGlobalSettings();

	// Check the preset file size from the file system
	File presetsFile = LittleFS.open("/presets.txt", SETTINGS_FILE_READ_MODE);
	size_t presetsFileSize = presetsFile.size();
	presetsFile.close();
	SETTINGS_LOGI(SETTINGS_TAG, "Presets size: %d (expected %d).", presetsFileSize, presetSize*numPresets);
	if (presetsFileSize != presetSize*numPresets)
	{
		SETTINGS_LOGI(SETTINGS_TAG, "Presets file size does not match.");
		esp32Settings_NewDeviceConfig();
	}

	// Read the preset data
	esp32Settings_ReadPresets();

	// Uncomment to force a new device configuration
	// globalSettings.bootState = 0;
    bootFlagValue = *bootFlagPtr;
	if (*bootFlagPtr != DEVICE_CONFIGURED_VALUE)
	{
		SETTINGS_LOGI(SETTINGS_TAG, "Configuring new device...");
		esp32Settings_NewDeviceConfig();
	}
	else
	{
		SETTINGS_LOGI(SETTINGS_TAG, "Performing standard boot...");
		esp32Settings_StandardBoot();
	}
	return bootFlagValue;
}

// Configures the device to a factory state
void esp32Settings_NewDeviceConfig()
{
	// Configure default values for global settings
	if( assignDefaultGlobalSettings != nullptr)
		assignDefaultGlobalSettings();
	else
		SETTINGS_LOGE(SETTINGS_TAG, "No default global settings function assigned. Pointer is null.");

	// Set the boot flag to indicate the device has been configured
	*bootFlagPtr = DEVICE_CONFIGURED_VALUE;

	// Format the file system to revert to a default state
	SETTINGS_LOGI(SETTINGS_TAG, "Formatting file system...");
	LittleFS.format();

	// Create the storage file for the global config
	SETTINGS_LOGI(SETTINGS_TAG, "Creating global settings file...");
	esp32Settings_SaveGlobalSettings();

	SETTINGS_LOGI(SETTINGS_TAG, "Boot flag = %d", *bootFlagPtr);
	
	// Configure default preset values
	if( assignDefaultPresetSettings != nullptr)
		assignDefaultPresetSettings();
	else
		SETTINGS_LOGE(SETTINGS_TAG, "No default preset settings function assigned. Pointer is null.");


	// Create the presets storage file
	SETTINGS_LOGI(SETTINGS_TAG, "Creating presets file...");
	esp32Settings_SavePresets();

	SETTINGS_LOGI(SETTINGS_TAG, "Device configured. Rebooting.");

	delay(10);
	esp32Settings_SoftwareReset();
}

void esp32Settings_StandardBoot()
{
	// Read the preset data into the struct array
	esp32Settings_ReadGlobalSettings();

	// Read the preset data into the struct array
	esp32Settings_ReadPresets();

	SETTINGS_LOGI(SETTINGS_TAG, "Standard boot complete!");
	esp32Settings_ListDir(LittleFS, "/", 1);
}

void esp32Settings_SoftwareReset()
{
	SETTINGS_LOGI(SETTINGS_TAG, "Performing software reset...");
#if defined(SETTINGS_CORE_ESP32)
	ESP.restart();
#elif defined(SETTINGS_CORE_RP2040)
	watchdog_reboot(0, 0, 0);
	while(1);
#endif
	
}

void esp32Settings_AssignDefaultGlobalSettings(void (fptr)())
{
	if (fptr != nullptr)
	{
		assignDefaultGlobalSettings = fptr;
	}
	else
	{
		SETTINGS_LOGE(SETTINGS_TAG, "No default global settings function assigned. Pointer is null.");
	}
}

void esp32Settings_AssignDefaultPresetSettings(void (fptr)())
{
	if (fptr != nullptr)
	{
		assignDefaultPresetSettings = fptr;
	}
	else
	{
		SETTINGS_LOGE(SETTINGS_TAG, "No default preset settings function assigned. Pointer is null.");
	}
}

void esp32Settings_ResetAllSettings()
{
	uint8_t resetBootStateValue = 0;
	SETTINGS_LOGI(SETTINGS_TAG, "Writing reset bootstate.");
	File globalConfigFile = LittleFS.open("/global.txt", SETTINGS_FILE_WRITE_MODE);
	size_t len = globalConfigFile.write((uint8_t *)&resetBootStateValue, 1);
	globalConfigFile.close();
	SETTINGS_LOGI(SETTINGS_TAG, "Wrote %d bytes to global file (expected %d).", len, 1);
	delay(1);
	esp32Settings_SoftwareReset();
}

void esp32Settings_ReadGlobalSettings()
{
	SETTINGS_LOGI(SETTINGS_TAG, "Reading global settings...");
	File globalConfigFile = LittleFS.open("/global.txt", SETTINGS_FILE_READ_MODE);
	globalConfigFile.read((uint8_t *)globalSettingsPtr, globalSettingsSize);
	globalConfigFile.close();
}

void esp32Settings_SaveGlobalSettings()
{
	SETTINGS_LOGI(SETTINGS_TAG, "Saving global settings to file.");
	File globalConfigFile = LittleFS.open("/global.txt", SETTINGS_FILE_WRITE_MODE);
	size_t len = globalConfigFile.write((uint8_t *)globalSettingsPtr, globalSettingsSize);
	globalConfigFile.close();
	SETTINGS_LOGI(SETTINGS_TAG, "Wrote %d bytes to global file (expected %d).", len, globalSettingsSize);
}

void esp32Settings_ReadPresets()
{
	SETTINGS_LOGI(SETTINGS_TAG, "Reading presets...");
	File presetsFile = LittleFS.open("/presets.txt", SETTINGS_FILE_READ_MODE);
	presetsFile.read((uint8_t *)presetsPtr, presetSize*numPresets);
	presetsFile.close();
}

void esp32Settings_SavePresets()
{
	SETTINGS_LOGI(SETTINGS_TAG, "Saving presets to file.");
	File presetsFile = LittleFS.open("/presets.txt", SETTINGS_FILE_WRITE_MODE);
	size_t len = presetsFile.write((uint8_t *)presetsPtr, presetSize*numPresets);
	presetsFile.close();
	SETTINGS_LOGI(SETTINGS_TAG, "Wrote %d bytes to presets file (expected %d).", len, presetSize*numPresets);
}


void esp32Settings_ListDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
	SETTINGS_LOGV(SETTINGS_TAG, "Listing directory: %s\r\n", dirname);

	File root = fs.open(dirname, SETTINGS_FILE_READ_MODE);
	if (!root)
	{
		SETTINGS_LOGV(SETTINGS_TAG, "- failed to open directory");
		return;
	}
	if (!root.isDirectory())
	{
		SETTINGS_LOGV(SETTINGS_TAG, " - not a directory");
		return;
	}

	File file = root.openNextFile();
	while (file)
	{
		if (file.isDirectory())
		{
			SETTINGS_LOGV(SETTINGS_TAG, "  DIR : %s", file.name());
			if (levels)
			{
#ifdef SETTINGS_CORE_ESP32
				esp32Settings_ListDir(fs, file.path(), levels - 1);
#else
				// RP2040: Build full path manually since file.path() doesn't exist
				char fullPath[256];
				snprintf(fullPath, sizeof(fullPath), "%s/%s", dirname, file.name());
				esp32Settings_ListDir(fs, fullPath, levels - 1);
#endif
			}
		}
		else
		{
			SETTINGS_LOGV(SETTINGS_TAG, "  FILE: %s (%d)", file.name(), file.size());
		}
		file = root.openNextFile();
	}
	SETTINGS_LOGV(SETTINGS_TAG,"directory listing complete.\n");
	root.close();
}
