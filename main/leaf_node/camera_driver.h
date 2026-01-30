/**
 * @file camera_driver.h
 * @brief ESP32-CAM camera driver interface
 */

#pragma once

#include "esp_camera.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the camera module
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t camera_init(void);

/**
 * @brief Capture a photo and return the frame buffer
 * @return Pointer to camera frame buffer on success, NULL on failure
 * @note Caller must call camera_return_frame_buffer() to free the buffer
 */
camera_fb_t *camera_capture_photo(void);

/**
 * @brief Return the frame buffer to the camera driver
 * @param fb Frame buffer to return
 */
void camera_return_frame_buffer(camera_fb_t *fb);

/**
 * @brief Warm up the camera sensor by discarding initial frames
 *
 * After waking from sleep, the camera sensor needs to auto-calibrate.
 * This function captures and discards several frames to allow the
 * sensor to stabilize before taking the actual photo.
 *
 * @param num_frames Number of frames to capture and discard (default: 10)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t camera_warmup(int num_frames);

/**
 * @brief Capture a photo with automatic warmup
 *
 * This function warms up the sensor first, then returns the final frame.
 * Use this instead of camera_capture_photo() after waking from sleep.
 *
 * @return Pointer to camera frame buffer on success, NULL on failure
 * @note Caller must call camera_return_frame_buffer() to free the buffer
 */
camera_fb_t *camera_capture_photo_with_warmup(void);

/**
 * @brief Check if camera is supported on this platform
 * @return true if camera is supported, false otherwise
 */
bool camera_is_supported(void);

#ifdef __cplusplus
}
#endif
