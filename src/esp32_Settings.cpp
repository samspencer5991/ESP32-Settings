#include "esp32_Settings.h"
#include <LittleFS.h>

#define FORMAT_LITTLEFS_IF_FAILED true
#define DEVICE_CONFIGURED_VALUE 114 // Arbitrary value to indicate the device has been configured

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
void esp32Settings_BootCheck(	void* globalSettings, uint16_t gSize, void* presets,
										uint16_t pSize, size_t num, uint8_t* bootFlag)
{
	ESP_LOGI(SETTINGS_TAG, "Preset size %d", pSize);
	// Perform the boot check
	ESP_LOGI(SETTINGS_TAG, "Boot check initiated.");
	if (globalSettings == nullptr || presets == nullptr
		|| num == 0 || bootFlag == nullptr)
	{
		ESP_LOGE(SETTINGS_TAG, "Invalid settings or presets pointers.");
		return;
	}

	// Assign the global settings and presets pointers
	globalSettingsPtr = globalSettings;
	presetsPtr = presets;
	bootFlagPtr = bootFlag;
	numPresets = num;
	globalSettingsSize = gSize;
	presetSize = pSize;
	
	// Check if an appropriate file system is available
	ESP_LOGI(SETTINGS_TAG, "Checking boot state...");
	if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED))
	{
		ESP_LOGI(SETTINGS_TAG, "LittleFS Mount Failed. Formatting...");
		esp32Settings_NewDeviceConfig();
	}

	// Check for the correct file structures
	ESP_LOGI(SETTINGS_TAG, "Checking file system...");
	uint8_t structureOk = 1;
	if (!LittleFS.exists("/global.txt"))
		structureOk = 0;

	if (!LittleFS.exists("/presets.txt"))
		structureOk = 0;

	if (!structureOk)
	{
		ESP_LOGI(SETTINGS_TAG, "File system structure incorrect.");
		esp32Settings_NewDeviceConfig();
	}

	// Check the global settings size from the file system
	ESP_LOGI(SETTINGS_TAG, "Validating global config size...");
	File globalConfigFile = LittleFS.open("/global.txt", "r");
	size_t globalConfigFileSize = globalConfigFile.size();
	ESP_LOGI(SETTINGS_TAG, "Global config file size: %d", globalConfigFileSize);
	if (globalConfigFileSize != globalSettingsSize)
	{
		ESP_LOGI(SETTINGS_TAG, "Global settings file size does not match.");
		esp32Settings_NewDeviceConfig();
	}
	globalConfigFile.close();
	
	// Read the global settings
	esp32Settings_ReadGlobalSettings();

	// Check the preset file size from the file system
	File presetsFile = LittleFS.open("/presets.txt", "r");
	size_t presetsFileSize = presetsFile.size();
	presetsFile.close();
	ESP_LOGI(SETTINGS_TAG, "Presets size: %d (expected %d).", presetsFileSize, presetSize*numPresets);
	if (presetsFileSize != presetSize*numPresets)
	{
		ESP_LOGI(SETTINGS_TAG, "Presets file size does not match.");
		esp32Settings_NewDeviceConfig();
	}

	// Read the preset data
	esp32Settings_ReadPresets();

	// Uncomment to force a new device configuration
	// globalSettings.bootState = 0;
	if (*bootFlagPtr != DEVICE_CONFIGURED_VALUE)
	{
		ESP_LOGI(SETTINGS_TAG, "Configuring new device...");
		esp32Settings_NewDeviceConfig();
	}
	else
	{
		ESP_LOGI(SETTINGS_TAG, "Performing standard boot...");
		esp32Settings_StandardBoot();
	}
}

// Configures the device to a factory state
void esp32Settings_NewDeviceConfig()
{
	// Configure default values for global settings
	if( assignDefaultGlobalSettings != nullptr)
		assignDefaultGlobalSettings();
	else
		ESP_LOGE(SETTINGS_TAG, "No default global settings function assigned. Pointer is null.");

	// Set the boot flag to indicate the device has been configured
	*bootFlagPtr = DEVICE_CONFIGURED_VALUE;

	// Format the file system to revert to a default state
	ESP_LOGI(SETTINGS_TAG, "Formatting file system...");
	LittleFS.format();

	// Create the storage file for the global config
	ESP_LOGI(SETTINGS_TAG, "Creating global settings file...");
	esp32Settings_SaveGlobalSettings();

	ESP_LOGI(SETTINGS_TAG, "Boot flag = %d", *bootFlagPtr);
	
	// Configure default preset values
	if( assignDefaultPresetSettings != nullptr)
		assignDefaultPresetSettings();
	else
		ESP_LOGE(SETTINGS_TAG, "No default preset settings function assigned. Pointer is null.");


	// Create the presets storage file
	ESP_LOGI(SETTINGS_TAG, "Creating presets file...");
	esp32Settings_SavePresets();

	ESP_LOGI(SETTINGS_TAG, "Device configured. Rebooting.");

	delay(10);
	esp32Settings_SoftwareReset();
}

void esp32Settings_StandardBoot()
{
	// Read the preset data into the struct array
	esp32Settings_ReadGlobalSettings();

	// Read the preset data into the struct array
	esp32Settings_ReadPresets();

	ESP_LOGI(SETTINGS_TAG, "Standard boot complete!");
	esp32Settings_ListDir(LittleFS, "/", 1);
}

void esp32Settings_SoftwareReset()
{
	ESP.restart();
}

void esp32Settings_AssignDefaultGlobalSettings(void (fptr)())
{
	if (fptr != nullptr)
	{
		assignDefaultGlobalSettings = fptr;
	}
	else
	{
		ESP_LOGE(SETTINGS_TAG, "No default global settings function assigned. Pointer is null.");
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
		ESP_LOGE(SETTINGS_TAG, "No default preset settings function assigned. Pointer is null.");
	}
}

void esp32Settings_ResetAllSettings()
{
	uint8_t resetBootStateValue = 0;
	ESP_LOGI(SETTINGS_TAG, "Writing reset bootstate.");
	File globalConfigFile = LittleFS.open("/global.txt", "w");
	size_t len = globalConfigFile.write((uint8_t *)&resetBootStateValue, 1);
	globalConfigFile.close();
	ESP_LOGI(SETTINGS_TAG, "Wrote %d bytes to global file (expected %d).", len, 1);
	delay(1);
	esp32Settings_SoftwareReset();
}

void esp32Settings_ReadGlobalSettings()
{
	ESP_LOGI(SETTINGS_TAG, "Reading global settings...");
	File globalConfigFile = LittleFS.open("/global.txt", "r");
	globalConfigFile.read((uint8_t *)globalSettingsPtr, globalSettingsSize);
	globalConfigFile.close();
}

void esp32Settings_SaveGlobalSettings()
{
	ESP_LOGI(SETTINGS_TAG, "Saving global settings to file.");
	File globalConfigFile = LittleFS.open("/global.txt", "w");
	size_t len = globalConfigFile.write((uint8_t *)globalSettingsPtr, globalSettingsSize);
	globalConfigFile.close();
	ESP_LOGI(SETTINGS_TAG, "Wrote %d bytes to global file (expected %d).", len, globalSettingsSize);
}

void esp32Settings_ReadPresets()
{
	ESP_LOGI(SETTINGS_TAG, "Reading presets...");
	File presetsFile = LittleFS.open("/presets.txt", "r");
	presetsFile.read((uint8_t *)presetsPtr, presetSize*numPresets);
	presetsFile.close();
}

void esp32Settings_SavePresets()
{
	ESP_LOGI(SETTINGS_TAG, "Saving presets to file.");
	File presetsFile = LittleFS.open("/presets.txt", "w");
	size_t len = presetsFile.write((uint8_t *)presetsPtr, presetSize*numPresets);
	presetsFile.close();
	ESP_LOGI(SETTINGS_TAG, "Wrote %d bytes to presets file (expected %d).", len, presetSize*numPresets);
}


void esp32Settings_ListDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
	ESP_LOGV(SETTINGS_TAG, "Listing directory: %s\r\n", dirname);

	File root = fs.open(dirname);
	if (!root)
	{
		ESP_LOGV(SETTINGS_TAG, "- failed to open directory");
		return;
	}
	if (!root.isDirectory())
	{
		ESP_LOGV(SETTINGS_TAG, " - not a directory");
		return;
	}

	File file = root.openNextFile();
	while (file)
	{
		if (file.isDirectory())
		{
			ESP_LOGV(SETTINGS_TAG, "  DIR : %s", file.name());
			if (levels)
			{
				esp32Settings_ListDir(fs, file.path(), levels - 1);
			}
		}
		else
		{
			ESP_LOGV(SETTINGS_TAG, "  FILE: %s (%d)", file.name(), file.size());
		}
		file = root.openNextFile();
	}
	ESP_LOGV(SETTINGS_TAG,"directory listing complete.\n");
	root.close();
}
