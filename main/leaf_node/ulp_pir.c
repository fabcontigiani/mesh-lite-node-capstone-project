/*
 * ULP PIR Monitor Implementation
 * 
 * Handles ULP initialization, deep sleep entry, and wake-up detection
 * for PIR-based motion sensing on LEAF nodes
 */

#include "ulp_pir.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "driver/rtc_io.h"
#include "soc/rtc_cntl_reg.h"

/* ULP header - provided by ulp component */
#include "ulp.h"

/* Generated ULP exports */
#include "ulp_pir_monitor.h"

static const char *TAG = "ulp_pir";

/* ULP program binary - generated from pir_monitor.S */
extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_pir_monitor_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_pir_monitor_bin_end");

static bool s_ulp_initialized = false;

esp_err_t ulp_pir_init(void)
{
    if (s_ulp_initialized) {
        ESP_LOGW(TAG, "ULP PIR already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing ULP PIR monitor on GPIO%d", PIR_GPIO_NUM);

    /* Initialize GPIO12 as RTC GPIO for ULP access */
    esp_err_t ret = rtc_gpio_init(PIR_GPIO_NUM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init RTC GPIO: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configure as input */
    ret = rtc_gpio_set_direction(PIR_GPIO_NUM, RTC_GPIO_MODE_INPUT_ONLY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO direction: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Enable pull-down (PIR outputs HIGH when triggered) */
    ret = rtc_gpio_pulldown_en(PIR_GPIO_NUM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable pulldown: %s", esp_err_to_name(ret));
        return ret;
    }
    rtc_gpio_pullup_dis(PIR_GPIO_NUM);

    /* Hold GPIO state during deep sleep */
    ret = rtc_gpio_hold_en(PIR_GPIO_NUM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable GPIO hold: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Load ULP program binary into RTC memory */
    size_t ulp_size = (ulp_main_bin_end - ulp_main_bin_start) / sizeof(uint32_t);
    ESP_LOGI(TAG, "Loading ULP program (%d words)", ulp_size);
    
    ret = ulp_load_binary(0, ulp_main_bin_start, ulp_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load ULP binary: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Set ULP wake-up period */
    ret = ulp_set_wakeup_period(0, ULP_WAKEUP_PERIOD_US);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set wakeup period: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Clear triggered flag */
    ulp_pir_triggered = 0;

    s_ulp_initialized = true;
    ESP_LOGI(TAG, "ULP PIR monitor initialized successfully");
    
    return ESP_OK;
}

void ulp_pir_enter_deep_sleep(void)
{
    if (!s_ulp_initialized) {
        ESP_LOGE(TAG, "ULP not initialized! Call ulp_pir_init() first");
        return;
    }

    ESP_LOGI(TAG, "Preparing to enter deep sleep with ULP PIR monitoring...");
    ESP_LOGI(TAG, "Wake-up count so far: %"PRIu32, ulp_pir_get_wakeup_count());

    /* Disconnect and stop WiFi to save power */
    ESP_LOGI(TAG, "Stopping WiFi...");
    esp_wifi_disconnect();
    esp_wifi_stop();

    /* Clear triggered flag before sleeping */
    ulp_pir_triggered = 0;

    /* Start ULP program */
    ESP_LOGI(TAG, "Starting ULP program...");
    esp_err_t ret = ulp_run(&ulp_entry - RTC_SLOW_MEM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start ULP: %s", esp_err_to_name(ret));
        return;
    }

    /* Enable ULP as wake-up source */
    ret = esp_sleep_enable_ulp_wakeup();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable ULP wakeup: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Entering deep sleep... ZzZz");
    
    /* Enter deep sleep - will wake when PIR triggers */
    esp_deep_sleep_start();
    
    /* Never reaches here */
}

bool ulp_pir_was_triggered(void)
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    
    if (cause == ESP_SLEEP_WAKEUP_ULP) {
        /* Check if PIR flag is set */
        return (ulp_pir_triggered & 0xFFFF) != 0;
    }
    
    return false;
}

uint32_t ulp_pir_get_wakeup_count(void)
{
    /* ULP variables are 16-bit, mask to get value */
    return ulp_wakeup_counter & 0xFFFF;
}

void ulp_pir_clear_triggered(void)
{
    ulp_pir_triggered = 0;
}
