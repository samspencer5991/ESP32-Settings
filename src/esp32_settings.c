#include "esp32_settings.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

#include "esp_log.h"
#include "esp_system.h"     // esp_restart
#include "esp_littlefs.h"   // joltwallet/littlefs VFS
#include "esp_rom_crc.h"    // esp_rom_crc32_le
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"  // vTaskDelay

// ---------------------------------------------------------------------------
// Storage configuration
//
// The settings live as two binary blobs on a LittleFS mount of the `storage`
// partition. This library OWNS that mount — nothing else should mount `storage`
// (on the N16R8/WiFi build the manager's WebUI also wants a `storage` LittleFS
// image; resolve that ownership then — see docs/STATUS.md).
// ---------------------------------------------------------------------------
#define SETTINGS_PARTITION_LABEL   "storage"
#define SETTINGS_BASE_PATH         "/lfs"
#define GLOBAL_PATH                SETTINGS_BASE_PATH "/global.txt"
#define GLOBAL_TMP_PATH            SETTINGS_BASE_PATH "/global.tmp"
#define PRESETS_PATH               SETTINGS_BASE_PATH "/presets.txt"
#define PRESETS_TMP_PATH           SETTINGS_BASE_PATH "/presets.tmp"

#define DEVICE_CONFIGURED_VALUE    114 // Arbitrary value to indicate the device has been configured

// On-disk integrity header prepended to each blob. Validation is magic + version
// + exact length + CRC32 of the payload — a corrupt or truncated file (e.g. a
// power loss mid-write) is detected and the caller falls back to defaults, rather
// than the old size-only check which a half-written file of the right length passed.
#define SETTINGS_MAGIC     0x53455454u // 'SETT'
#define SETTINGS_VERSION   1

typedef struct __attribute__((packed)) {
	uint32_t magic;
	uint16_t version;
	uint16_t reserved;  // keeps the payload 4-byte aligned in the file
	uint32_t length;    // payload length in bytes
	uint32_t crc32;     // esp_rom_crc32_le(0, payload, length)
} settings_header_t;

static const char *TAG = "ESP32_SETTINGS";

void* globalSettingsPtr = NULL;
void* presetsPtr = NULL;
uint8_t* bootFlagPtr = NULL;
size_t numPresets = 0;

uint16_t globalSettingsSize = 0;
uint16_t presetSize = 0;

void (*assignDefaultGlobalSettings)() = NULL;
void (*assignDefaultPresetSettings)() = NULL;

static void esp32Settings_ListDir(const char *dirname, uint8_t levels);

// Mount the storage partition. Returns true if mounted (or already mounted).
// format_if_mount_failed lets a blank/corrupt filesystem come up formatted.
static bool esp32Settings_Mount(void)
{
	esp_vfs_littlefs_conf_t conf = {
		.base_path = SETTINGS_BASE_PATH,
		.partition_label = SETTINGS_PARTITION_LABEL,
		.format_if_mount_failed = true,
		.dont_mount = false,
	};
	esp_err_t err = esp_vfs_littlefs_register(&conf);
	if (err == ESP_ERR_INVALID_STATE) {
		// Already registered (e.g. a re-entry) — treat as mounted.
		return true;
	}
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "LittleFS mount of '%s' failed: %s",
				 SETTINGS_PARTITION_LABEL, esp_err_to_name(err));
		return false;
	}
	return true;
}

// Write header + payload to a temp file, then atomically rename over the target.
// LittleFS commits the data on fclose() and rename() is atomic, so an interrupted
// save can never leave a half-written destination file.
static bool esp32Settings_WriteBlob(const char *path, const char *tmp_path,
									 const void *data, size_t len)
{
	FILE *f = fopen(tmp_path, "wb");
	if (f == NULL) {
		ESP_LOGE(TAG, "Failed to open %s for write", tmp_path);
		return false;
	}

	settings_header_t hdr = {
		.magic = SETTINGS_MAGIC,
		.version = SETTINGS_VERSION,
		.reserved = 0,
		.length = (uint32_t)len,
		.crc32 = esp_rom_crc32_le(0, (const uint8_t *)data, (uint32_t)len),
	};

	bool ok = (fwrite(&hdr, 1, sizeof hdr, f) == sizeof hdr) &&
			  (fwrite(data, 1, len, f) == len);
	fclose(f); // commits the temp file to flash

	if (!ok) {
		// A short write here is almost always heap exhaustion in the caller, not a full
		// filesystem: the littlefs write needs a ~4 KB block, so if free internal heap is
		// starved (e.g. a large ArduinoJson doc still alive) the first fwrite returns 0.
		// Callers must release transient buffers before saving. See docs/SOLUTIONS.md.
		ESP_LOGE(TAG, "Short write to %s (expected %u payload bytes) — low heap?", tmp_path, (unsigned)len);
		remove(tmp_path);
		return false;
	}
	if (rename(tmp_path, path) != 0) {
		ESP_LOGE(TAG, "rename %s -> %s failed", tmp_path, path);
		remove(tmp_path);
		return false;
	}
	ESP_LOGI(TAG, "Wrote %u payload bytes to %s", (unsigned)len, path);
	return true;
}

// Read + verify a blob into `data`. Returns false (leaving as much of `data`
// possibly-partially-filled — caller reseeds on failure) if the file is missing,
// has the wrong magic/version, a mismatched length, or a bad CRC.
static bool esp32Settings_ReadBlob(const char *path, void *data, size_t len)
{
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		ESP_LOGW(TAG, "%s not present", path);
		return false;
	}

	settings_header_t hdr;
	bool ok = (fread(&hdr, 1, sizeof hdr, f) == sizeof hdr) &&
			  (hdr.magic == SETTINGS_MAGIC) &&
			  (hdr.version == SETTINGS_VERSION) &&
			  (hdr.length == len) &&
			  (fread(data, 1, len, f) == len) &&
			  (esp_rom_crc32_le(0, (const uint8_t *)data, (uint32_t)len) == hdr.crc32);
	fclose(f);

	if (!ok) {
		ESP_LOGW(TAG, "%s invalid (magic/version/size/CRC mismatch)", path);
	}
	return ok;
}

// Checks for the standard combination of a global settings file and a presets file.
// If either is missing or fails validation, the device is reconfigured to defaults.
// Also initialises the global/preset pointers and the number of presets. The bootflag
// is a pointer into the global settings struct (its stored boot-state byte).
uint8_t esp32Settings_BootCheck(	void* globalSettings, uint16_t gSize, void* presets,
										uint16_t pSize, size_t num, uint8_t* bootFlag)
{
	uint8_t bootFlagValue = 0;
	ESP_LOGI(TAG, "Preset size %d", pSize);
	ESP_LOGI(TAG, "Boot check initiated.");
	if (globalSettings == NULL || presets == NULL || num == 0 || bootFlag == NULL) {
		ESP_LOGE(TAG, "Invalid settings or presets pointers.");
		return 0;
	}

	// Assign the global settings and presets pointers
	globalSettingsPtr = globalSettings;
	presetsPtr = presets;
	bootFlagPtr = bootFlag;
	numPresets = num;
	globalSettingsSize = gSize;
	presetSize = pSize;

	// Mount the filesystem (formats a blank/corrupt partition automatically).
	ESP_LOGI(TAG, "Mounting storage...");
	if (!esp32Settings_Mount()) {
		// No usable filesystem at all — nothing we can persist to. Reconfigure
		// tries a format + rewrite; if the partition itself is absent this will
		// keep failing, which is the correct loud failure mode.
		esp32Settings_NewDeviceConfig();
	}

	// Validate + load both files. A missing/corrupt file triggers reconfiguration
	// (which formats, writes defaults, and reboots — so it does not return).
	ESP_LOGI(TAG, "Validating stored settings...");
	bool globalOk  = esp32Settings_ReadBlob(GLOBAL_PATH, globalSettingsPtr, globalSettingsSize);
	bool presetsOk = globalOk && esp32Settings_ReadBlob(PRESETS_PATH, presetsPtr,
													    (size_t)presetSize * numPresets);
	if (!globalOk || !presetsOk) {
		ESP_LOGI(TAG, "Stored settings missing/invalid — reconfiguring.");
		esp32Settings_NewDeviceConfig();
	}

	// Both files loaded into the struct pointers; the boot flag now reflects storage.
	bootFlagValue = *bootFlagPtr;
	if (*bootFlagPtr != DEVICE_CONFIGURED_VALUE) {
		ESP_LOGI(TAG, "Configuring new device...");
		esp32Settings_NewDeviceConfig();
	} else {
		ESP_LOGI(TAG, "Performing standard boot...");
		esp32Settings_StandardBoot();
	}
	return bootFlagValue;
}

// Configures the device to a factory state
void esp32Settings_NewDeviceConfig()
{
	// Configure default values for global settings
	if (assignDefaultGlobalSettings != NULL)
		assignDefaultGlobalSettings();
	else
		ESP_LOGE(TAG, "No default global settings function assigned. Pointer is null.");

	// Set the boot flag to indicate the device has been configured
	*bootFlagPtr = DEVICE_CONFIGURED_VALUE;

	// Format the file system to revert to a clean state
	ESP_LOGI(TAG, "Formatting file system...");
	esp_err_t err = esp_littlefs_format(SETTINGS_PARTITION_LABEL);
	if (err != ESP_OK)
		ESP_LOGE(TAG, "Format failed: %s", esp_err_to_name(err));

	// Create the storage file for the global config
	ESP_LOGI(TAG, "Creating global settings file...");
	esp32Settings_SaveGlobalSettings();

	ESP_LOGI(TAG, "Boot flag = %d", *bootFlagPtr);

	// Configure default preset values
	if (assignDefaultPresetSettings != NULL)
		assignDefaultPresetSettings();
	else
		ESP_LOGE(TAG, "No default preset settings function assigned. Pointer is null.");

	// Create the presets storage file
	ESP_LOGI(TAG, "Creating presets file...");
	esp32Settings_SavePresets();

	ESP_LOGI(TAG, "Device configured. Rebooting.");

	vTaskDelay(pdMS_TO_TICKS(10));
	esp32Settings_SoftwareReset();
}

void esp32Settings_StandardBoot()
{
	// Re-read the persisted data into the struct pointers
	esp32Settings_ReadGlobalSettings();
	esp32Settings_ReadPresets();

	ESP_LOGI(TAG, "Standard boot complete!");
	esp32Settings_ListDir(SETTINGS_BASE_PATH, 1);
}

void esp32Settings_SoftwareReset()
{
	esp_restart();
}

void esp32Settings_AssignDefaultGlobalSettings(void (*fptr)())
{
	if (fptr != NULL)
		assignDefaultGlobalSettings = fptr;
	else
		ESP_LOGE(TAG, "No default global settings function assigned. Pointer is null.");
}

void esp32Settings_AssignDefaultPresetSettings(void (*fptr)())
{
	if (fptr != NULL)
		assignDefaultPresetSettings = fptr;
	else
		ESP_LOGE(TAG, "No default preset settings function assigned. Pointer is null.");
}

// Invalidate the stored config so the next boot reconfigures to defaults. We write
// the in-RAM global blob with a cleared boot flag: on reboot the file validates but
// its boot flag != CONFIGURED, so BootCheck reconfigures.
void esp32Settings_ResetAllSettings()
{
	ESP_LOGI(TAG, "Writing reset bootstate.");
	if (bootFlagPtr != NULL)
		*bootFlagPtr = 0;
	esp32Settings_SaveGlobalSettings();
	vTaskDelay(pdMS_TO_TICKS(1));
	esp32Settings_SoftwareReset();
}

void esp32Settings_ReadGlobalSettings()
{
	ESP_LOGI(TAG, "Reading global settings...");
	if (!esp32Settings_ReadBlob(GLOBAL_PATH, globalSettingsPtr, globalSettingsSize))
		ESP_LOGE(TAG, "Global settings read failed.");
}

void esp32Settings_SaveGlobalSettings()
{
	ESP_LOGI(TAG, "Saving global settings to file.");
	esp32Settings_WriteBlob(GLOBAL_PATH, GLOBAL_TMP_PATH, globalSettingsPtr, globalSettingsSize);
}

void esp32Settings_ReadPresets()
{
	ESP_LOGI(TAG, "Reading presets...");
	if (!esp32Settings_ReadBlob(PRESETS_PATH, presetsPtr, (size_t)presetSize * numPresets))
		ESP_LOGE(TAG, "Presets read failed.");
}

void esp32Settings_SavePresets()
{
	ESP_LOGI(TAG, "Saving presets to file.");
	esp32Settings_WriteBlob(PRESETS_PATH, PRESETS_TMP_PATH, presetsPtr,
							(size_t)presetSize * numPresets);
}

// Diagnostic directory listing (VERBOSE only) via POSIX dirent over the VFS mount.
static void esp32Settings_ListDir(const char *dirname, uint8_t levels)
{
	ESP_LOGV(TAG, "Listing directory: %s", dirname);

	DIR *dir = opendir(dirname);
	if (dir == NULL) {
		ESP_LOGV(TAG, "- failed to open directory");
		return;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		char path[300];
		sprintf(path, "%s/%s", dirname, entry->d_name);

		struct stat st;
		bool is_dir = (entry->d_type == DT_DIR) ||
					  (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
		if (is_dir) {
			ESP_LOGV(TAG, "  DIR : %s", entry->d_name);
			if (levels)
				esp32Settings_ListDir(path, levels - 1);
		} else {
			long size = (stat(path, &st) == 0) ? (long)st.st_size : -1;
			ESP_LOGV(TAG, "  FILE: %s (%ld)", entry->d_name, size);
		}
	}
	closedir(dir);
	ESP_LOGV(TAG, "directory listing complete.");
}
