/*
 * SD Card Storage Module Implementation
 *
 * Provides SD card storage for photos using SPI 1-bit mode.
 * Compatible with ESP32-CAM which shares GPIO 4 with flash LED.
 *
 * ESP32-CAM SD Card Pins (SPI Mode):
 * - GPIO 14: CLK
 * - GPIO 15: CMD (MOSI)
 * - GPIO 2:  DATA0 (MISO)
 * - GPIO 13: CS (Directly on module, directly directly directly - some boards
 * differ)
 *
 * Note: GPIO 4 is NOT used in 1-bit mode, so flash LED is unaffected.
 */

#include "sd_storage.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd_storage";

/* Mount point for the SD card */
#define MOUNT_POINT "/sdcard"
#define PHOTOS_DIR MOUNT_POINT "/photos"

/* ESP32-CAM SD Card SPI Pins */
#define PIN_NUM_MISO 2
#define PIN_NUM_MOSI 15
#define PIN_NUM_CLK 14
#define PIN_NUM_CS 13

/*
 * Module state
 * Note: s_photo_count uses RTC_DATA_ATTR to persist across deep sleep.
 * This ensures unique filenames after each wakeup cycle.
 */
static bool s_initialized = false;
static bool s_available = false;
static RTC_DATA_ATTR uint32_t s_photo_count = 0; /* Survives deep sleep! */
static sdmmc_card_t *s_card = NULL;

/* Forward declarations */
static esp_err_t create_photos_directory(void);

esp_err_t sd_storage_init(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "SD storage already initialized");
    return s_available ? ESP_OK : ESP_ERR_NOT_FOUND;
  }

  ESP_LOGI(TAG, "Initializing SD card in SPI mode...");

  esp_err_t ret;

  /* Configure SPI bus */
  spi_bus_config_t bus_cfg = {
      .mosi_io_num = PIN_NUM_MOSI,
      .miso_io_num = PIN_NUM_MISO,
      .sclk_io_num = PIN_NUM_CLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 4000,
  };

  ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
    s_initialized = true;
    s_available = false;
    return ret;
  }

  /* Configure SD SPI device */
  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config.gpio_cs = PIN_NUM_CS;
  slot_config.host_id = SPI2_HOST;

  /* Mount configuration */
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 5,
      .allocation_unit_size = 16 * 1024};

  /* SDMMC host configuration for SPI mode */
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();

  ESP_LOGI(TAG, "Mounting SD card...");
  ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config,
                                &s_card);

  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGW(
          TAG,
          "Failed to mount filesystem - SD card not formatted or not present");
    } else {
      ESP_LOGW(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
    }
    spi_bus_free(SPI2_HOST);
    s_initialized = true;
    s_available = false;
    return ret;
  }

  /* Print card info */
  ESP_LOGI(TAG, "SD card mounted successfully!");
  sdmmc_card_print_info(stdout, s_card);

  /* Create photos directory if it doesn't exist */
  ret = create_photos_directory();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to create photos directory, continuing anyway");
  }

  s_initialized = true;
  s_available = true;

  ESP_LOGI(TAG, "SD storage ready. Photos will be saved to %s", PHOTOS_DIR);

  return ESP_OK;
}

bool sd_storage_is_available(void) { return s_available; }

esp_err_t sd_storage_save_photo(const uint8_t *data, size_t size,
                                char *out_filename, size_t filename_size) {
  if (!s_available) {
    ESP_LOGW(TAG, "SD card not available");
    return ESP_ERR_INVALID_STATE;
  }

  if (data == NULL || size == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  /* Get MAC address for unique filename */
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  /* Increment counter */
  s_photo_count++;

  /* Generate filename: IMG_MACADDR_COUNTER.jpg */
  char filename[128];
  snprintf(filename, sizeof(filename), "%s/IMG_%02X%02X%02X_%05" PRIu32 ".jpg",
           PHOTOS_DIR, mac[3], mac[4], mac[5], s_photo_count);

  ESP_LOGI(TAG, "Saving photo to: %s (%zu bytes)", filename, size);

  /* Open file for writing */
  FILE *f = fopen(filename, "wb");
  if (f == NULL) {
    ESP_LOGE(TAG, "Failed to open file for writing: %s", filename);
    return ESP_FAIL;
  }

  /* Write data */
  size_t written = fwrite(data, 1, size, f);
  fclose(f);

  if (written != size) {
    ESP_LOGE(TAG, "Failed to write all data: wrote %zu of %zu bytes", written,
             size);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Photo saved successfully: %s", filename);

  /* Copy filename to output if requested */
  if (out_filename != NULL && filename_size > 0) {
    /* Just copy the filename part, not full path */
    const char *basename = strrchr(filename, '/');
    if (basename) {
      basename++; /* Skip the '/' */
    } else {
      basename = filename;
    }
    strncpy(out_filename, basename, filename_size - 1);
    out_filename[filename_size - 1] = '\0';
  }

  return ESP_OK;
}

uint32_t sd_storage_get_photo_count(void) { return s_photo_count; }

void sd_storage_deinit(void) {
  if (!s_initialized) {
    return;
  }

  if (s_available && s_card != NULL) {
    ESP_LOGI(TAG, "Unmounting SD card...");
    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
    spi_bus_free(SPI2_HOST);
    s_card = NULL;
  }

  s_available = false;
  s_initialized = false;
  ESP_LOGI(TAG, "SD storage deinitialized");
}

/* === Private Functions === */

static esp_err_t create_photos_directory(void) {
  struct stat st;

  if (stat(PHOTOS_DIR, &st) == 0) {
    /* Directory exists */
    return ESP_OK;
  }

  /* Create directory */
  ESP_LOGI(TAG, "Creating photos directory: %s", PHOTOS_DIR);
  if (mkdir(PHOTOS_DIR, 0755) != 0) {
    ESP_LOGE(TAG, "Failed to create directory: %s", PHOTOS_DIR);
    return ESP_FAIL;
  }

  return ESP_OK;
}
