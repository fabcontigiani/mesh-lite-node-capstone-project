/*
 * ULP PIR Monitor Header
 * 
 * Functions and variables for ULP-based PIR sensor monitoring
 * Used only on LEAF nodes for ultra-low power operation
 */

#ifndef ULP_PIR_H
#define ULP_PIR_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GPIO pin connected to PIR sensor */
#define PIR_GPIO_NUM    GPIO_NUM_12

/* ULP wake-up check period in microseconds (100ms) */
#define ULP_WAKEUP_PERIOD_US    100000

/**
 * @brief Initialize ULP PIR monitoring subsystem
 * 
 * Configures GPIO12 as RTC input, loads ULP program,
 * and prepares for deep sleep operation.
 * 
 * @return ESP_OK on success
 */
esp_err_t ulp_pir_init(void);

/**
 * @brief Enter deep sleep with ULP PIR monitoring
 * 
 * Stops WiFi, starts ULP program, and enters deep sleep.
 * Main CPU will wake when PIR detects motion.
 */
void ulp_pir_enter_deep_sleep(void);

/**
 * @brief Check if wake-up was caused by PIR/ULP
 * 
 * @return true if PIR triggered wake-up, false otherwise
 */
bool ulp_pir_was_triggered(void);

/**
 * @brief Get total count of PIR wake-ups
 * 
 * Counter persists across deep sleep cycles (stored in RTC memory)
 * 
 * @return Number of times PIR has triggered wake-up
 */
uint32_t ulp_pir_get_wakeup_count(void);

/**
 * @brief Clear the PIR triggered flag
 * 
 * Should be called after processing a PIR wake-up event
 */
void ulp_pir_clear_triggered(void);

#ifdef __cplusplus
}
#endif

#endif /* ULP_PIR_H */
