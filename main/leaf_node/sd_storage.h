/*
 * SD Card Storage Module Header
 *
 * Provides SD card storage for photos on relay and leaf nodes.
 * Uses SPI 1-bit mode for ESP32-CAM compatibility.
 *
 * If SD card is not available, the node continues to function
 * normally (photos are only uploaded via HTTP).
 */

#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize SD card storage
 *
 * Attempts to mount SD card using SPI 1-bit mode.
 * If SD card is not present or mount fails, returns error
 * but the system should continue without SD storage.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sd_storage_init(void);

/**
 * @brief Check if SD card is available and mounted
 *
 * @return true if SD card is available for storage
 */
bool sd_storage_is_available(void);

/**
 * @brief Save photo to SD card
 *
 * Generates a unique filename based on MAC address and counter.
 * Photo is saved as JPEG in /sdcard/photos/ directory.
 *
 * @param data Pointer to image data
 * @param size Size of image data in bytes
 * @param out_filename Buffer to receive generated filename (can be NULL)
 * @param filename_size Size of filename buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sd_storage_save_photo(const uint8_t *data, size_t size,
                                char *out_filename, size_t filename_size);

/**
 * @brief Get count of photos saved since boot
 *
 * @return Number of photos saved to SD
 */
uint32_t sd_storage_get_photo_count(void);

/**
 * @brief Unmount and deinitialize SD card
 */
void sd_storage_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* SD_STORAGE_H */
