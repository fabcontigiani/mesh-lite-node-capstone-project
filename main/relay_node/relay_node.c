/*
 * Relay Node Module Implementation
 *
 * Relay node extends the mesh network while maintaining energy efficiency:
 * - Uses Light Sleep + Modem Power Save instead of Deep Sleep
 * - Maintains WiFi connection (allows child nodes)
 * - PIR detection via GPIO interrupt (not ULP)
 * - Captures and uploads photos on motion detection (same as leaf)
 * - CPU automatically enters Light Sleep between activities
 *
 * Key difference from Leaf Node:
 * - Leaf: Deep Sleep + ULP = loses mesh connection, ~50µA
 * - Relay: Light Sleep + Modem PS = maintains mesh, ~15-25mA
 */

#include "relay_node.h"

#include <inttypes.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_pm.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "esp_mesh_lite.h"
#include "leaf_node/camera_driver.h"

static const char *TAG = "relay_node";

/* Use Kconfig values or defaults */
#ifdef CONFIG_RELAY_PIR_GPIO
#define RELAY_PIR_GPIO CONFIG_RELAY_PIR_GPIO
#else
#define RELAY_PIR_GPIO 12
#endif

#ifdef CONFIG_RELAY_AWAKE_TIME_MS
#define RELAY_AWAKE_TIME_MS CONFIG_RELAY_AWAKE_TIME_MS
#else
#define RELAY_AWAKE_TIME_MS 60000
#endif

#ifdef CONFIG_RELAY_STATUS_INTERVAL_MS
#define RELAY_STATUS_INTERVAL_MS CONFIG_RELAY_STATUS_INTERVAL_MS
#else
#define RELAY_STATUS_INTERVAL_MS 30000
#endif

/* Server Configuration (same as leaf_node) */
#define USE_HTTPS_UPLOAD 0 /* Set to 1 for HTTPS (remote), 0 for HTTP (local)  \
                            */

#define HTTPS_SERVER_URL "https://proyecto.lab.fabcontigiani.uno/upload/"
#define LOCAL_HTTP_SERVER_IP "192.168.1.2"
#define LOCAL_HTTP_SERVER_PORT "8000"
#define LOCAL_HTTP_SERVER_URL                                                  \
  "http://" LOCAL_HTTP_SERVER_IP ":" LOCAL_HTTP_SERVER_PORT "/upload/"

#define HTTP_TIMEOUT_MS 30000

/* Let's Encrypt ISRG Root X1 certificate (valid until 2035) */
static const char *letsencrypt_root_ca =
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n";

/* Module state */
static bool s_initialized = false;
static bool s_power_save_active = false;
static bool s_camera_ready = false;
static uint32_t s_motion_count = 0;

/* Synchronization */
static SemaphoreHandle_t s_pir_semaphore = NULL;
static TimerHandle_t s_status_timer = NULL;

/* Forward declarations */
static esp_err_t setup_power_management(void);
static void setup_pir_gpio_interrupt(void);
static void pir_isr_handler(void *arg);
static void status_timer_callback(TimerHandle_t timer);
static void relay_main_task(void *arg);
static bool init_camera_once(void);
static void process_and_upload_photo(void);
static bool upload_photo_to_server(const uint8_t *image_data,
                                   size_t image_size);

esp_err_t relay_node_init(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "Relay node already initialized");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "==============================");
  ESP_LOGI(TAG, "  RELAY NODE INITIALIZATION");
  ESP_LOGI(TAG, "==============================");
  ESP_LOGI(TAG, "This node uses Light Sleep + Modem PS");
  ESP_LOGI(TAG, "It can have child nodes connected");
  ESP_LOGI(TAG, "It captures photos on PIR detection");

  /* Create PIR semaphore */
  s_pir_semaphore = xSemaphoreCreateBinary();
  if (s_pir_semaphore == NULL) {
    ESP_LOGE(TAG, "Failed to create PIR semaphore");
    return ESP_ERR_NO_MEM;
  }

  /* Configure SoftAP inactive time (like root node) */
  esp_err_t ret = esp_wifi_set_inactive_time(WIFI_IF_AP, 60);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "SoftAP inactive timeout: 60 seconds");
  } else {
    ESP_LOGW(TAG, "Failed to set SoftAP inactive time: %s",
             esp_err_to_name(ret));
  }

  /* Also set STA inactive time for stability */
  ret = esp_wifi_set_inactive_time(WIFI_IF_STA, 60);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "STA inactive timeout: 60 seconds");
  }

  /* Create status timer */
  s_status_timer =
      xTimerCreate("relay_status", pdMS_TO_TICKS(RELAY_STATUS_INTERVAL_MS),
                   pdTRUE, /* Auto-reload */
                   NULL, status_timer_callback);

  if (s_status_timer == NULL) {
    ESP_LOGE(TAG, "Failed to create status timer");
    return ESP_ERR_NO_MEM;
  }

  s_initialized = true;
  ESP_LOGI(TAG, "Relay node initialized successfully");
  ESP_LOGI(TAG, "PIR GPIO: %d | Awake time: %d ms", RELAY_PIR_GPIO,
           RELAY_AWAKE_TIME_MS);

  return ESP_OK;
}

void relay_node_start(void) {
  if (!s_initialized) {
    ESP_LOGE(TAG, "Relay node not initialized!");
    return;
  }

  ESP_LOGI(TAG, "Starting relay node operation...");

  /* Setup PIR GPIO interrupt */
  setup_pir_gpio_interrupt();

  /* Initialize camera */
  if (!init_camera_once()) {
    ESP_LOGW(TAG, "Camera init failed - photos won't be captured");
  }

  /* Setup power management (Light Sleep + Modem PS) */
  esp_err_t ret = setup_power_management();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Power management setup failed, running without power save");
  }

  /* Start status timer */
  xTimerStart(s_status_timer, 0);

  /* Create main task */
  xTaskCreate(relay_main_task, "relay_main", 4096, NULL, 5, NULL);

  ESP_LOGI(
      TAG,
      "Relay node running. Waiting for PIR events and child connections...");
}

uint32_t relay_node_get_child_count(void) {
  wifi_sta_list_t sta_list = {0};
  esp_wifi_ap_get_sta_list(&sta_list);
  return sta_list.num;
}

bool relay_node_is_power_save_active(void) { return s_power_save_active; }

uint32_t relay_node_get_motion_count(void) { return s_motion_count; }

/* === Private Functions === */

/**
 * Initialize camera (only once)
 */
static bool init_camera_once(void) {
  if (s_camera_ready) {
    ESP_LOGI(TAG, "Camera already initialized");
    return true;
  }

  if (!camera_is_supported()) {
    ESP_LOGW(TAG, "Camera not supported on this board");
    return false;
  }

  if (camera_init() != ESP_OK) {
    ESP_LOGE(TAG, "Camera init failed");
    return false;
  }

  s_camera_ready = true;
  ESP_LOGI(TAG, "Camera initialized successfully");
  return true;
}

/**
 * Setup Power Management for Light Sleep + Modem Power Save
 */
static esp_err_t setup_power_management(void) {
  esp_err_t ret;

  ESP_LOGI(TAG, "Configuring Power Management...");

  /*
   * Enable WiFi Modem Power Save
   * WIFI_PS_MIN_MODEM is less aggressive but more stable
   * WIFI_PS_MAX_MODEM saves more power but can cause SA Query timeouts
   */
  ret = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set WiFi power save: %s", esp_err_to_name(ret));
    return ret;
  }
  ESP_LOGI(TAG, "WiFi Power Save: MIN_MODEM enabled");

#ifdef CONFIG_PM_ENABLE
  /*
   * Configure automatic Light Sleep
   * CPU will sleep between FreeRTOS tasks when idle
   */
  esp_pm_config_t pm_config = {
      .max_freq_mhz = 240, .min_freq_mhz = 80, .light_sleep_enable = true};

  ret = esp_pm_configure(&pm_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to configure PM: %s", esp_err_to_name(ret));
    return ret;
  }
  ESP_LOGI(TAG, "Light Sleep: enabled (80-240 MHz)");
  s_power_save_active = true;
#else
  ESP_LOGW(TAG, "CONFIG_PM_ENABLE not set - Light Sleep disabled");
  ESP_LOGW(TAG, "Add CONFIG_PM_ENABLE=y to sdkconfig for full power savings");
  s_power_save_active = false;
#endif

  return ESP_OK;
}

/**
 * Setup PIR GPIO interrupt (normal GPIO, not RTC/ULP)
 */
static void setup_pir_gpio_interrupt(void) {
  ESP_LOGI(TAG, "Configuring PIR GPIO interrupt on GPIO%d...", RELAY_PIR_GPIO);

  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << RELAY_PIR_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .intr_type = GPIO_INTR_POSEDGE, /* Rising edge = PIR detected */
  };
  gpio_config(&io_conf);

  /* Install ISR service and add handler */
  gpio_install_isr_service(0);
  gpio_isr_handler_add(RELAY_PIR_GPIO, pir_isr_handler, NULL);

  ESP_LOGI(TAG, "PIR GPIO interrupt ready on GPIO%d", RELAY_PIR_GPIO);
}

/**
 * PIR GPIO ISR Handler
 */
static void IRAM_ATTR pir_isr_handler(void *arg) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(s_pir_semaphore, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * Main relay task - monitors PIR and handles events
 */
static void relay_main_task(void *arg) {
  ESP_LOGI(TAG, "Relay main task started");

  while (1) {
    /*
     * Wait for PIR interrupt
     * During this wait, CPU will automatically enter Light Sleep
     * if CONFIG_PM_ENABLE is set
     */
    if (xSemaphoreTake(s_pir_semaphore, pdMS_TO_TICKS(RELAY_AWAKE_TIME_MS)) ==
        pdTRUE) {
      /* PIR detected! */
      s_motion_count++;
      ESP_LOGI(TAG, "========================================");
      ESP_LOGI(TAG, "  PIR MOTION DETECTED! (#%" PRIu32 ")", s_motion_count);
      ESP_LOGI(TAG, "========================================");

      /* Capture and upload photo */
      process_and_upload_photo();

      /* Small debounce delay */
      vTaskDelay(pdMS_TO_TICKS(2000));

      /* Clear any pending semaphore (debounce) */
      xSemaphoreTake(s_pir_semaphore, 0);
    }
    /* Timeout - just continue monitoring */
  }
}

/**
 * Capture photo and upload to server
 */
static void process_and_upload_photo(void) {
  ESP_LOGI(TAG, "Processing motion event #%" PRIu32 "...", s_motion_count);

  if (!s_camera_ready) {
    ESP_LOGW(TAG, "Camera not ready - skipping photo capture");
    return;
  }

  /* Capture photo */
  camera_fb_t *fb = camera_capture_photo();
  if (!fb) {
    ESP_LOGE(TAG, "Photo capture failed");
    return;
  }

  ESP_LOGI(TAG, "Photo captured: %zu bytes", fb->len);

  /* Upload to server */
  bool success = upload_photo_to_server(fb->buf, fb->len);

  if (success) {
    ESP_LOGI(TAG, "Photo uploaded successfully!");
  } else {
    ESP_LOGE(TAG, "Photo upload failed!");
  }

  camera_return_frame_buffer(fb);
}

/**
 * Upload photo to server using multipart/form-data
 */
static bool upload_photo_to_server(const uint8_t *image_data,
                                   size_t image_size) {
#if USE_HTTPS_UPLOAD
  const char *upload_url = HTTPS_SERVER_URL;
  ESP_LOGI(TAG, "Uploading %zu bytes via HTTPS to %s", image_size, upload_url);
#else
  const char *upload_url = LOCAL_HTTP_SERVER_URL;
  ESP_LOGI(TAG, "Uploading %zu bytes via HTTP to %s", image_size, upload_url);
#endif

  /* Get device MAC for unique filename */
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  /* Generate filename */
  char filename[64];
  snprintf(filename, sizeof(filename),
           "relay_%02x%02x%02x%02x%02x%02x_%" PRIu32 ".jpg", mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5], s_motion_count);

  /* Multipart form data boundary */
  const char *boundary = "----ESP32CamBoundary";

  /* Build form start and end */
  char form_start[256];
  char form_end[64];

  snprintf(form_start, sizeof(form_start),
           "--%s\r\n"
           "Content-Disposition: form-data; name=\"image\"; filename=\"%s\"\r\n"
           "Content-Type: image/jpeg\r\n\r\n",
           boundary, filename);

  snprintf(form_end, sizeof(form_end), "\r\n--%s--\r\n", boundary);

  /* Calculate total content length */
  int content_length = strlen(form_start) + image_size + strlen(form_end);

  /* Configure HTTP client */
  esp_http_client_config_t config = {
      .url = upload_url,
      .method = HTTP_METHOD_POST,
      .timeout_ms = HTTP_TIMEOUT_MS,
#if USE_HTTPS_UPLOAD
      .cert_pem = letsencrypt_root_ca,
#endif
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL) {
    ESP_LOGE(TAG, "Failed to initialize HTTP client");
    return false;
  }

  /* Set Content-Type header with boundary */
  char content_type[128];
  snprintf(content_type, sizeof(content_type),
           "multipart/form-data; boundary=%s", boundary);
  esp_http_client_set_header(client, "Content-Type", content_type);

  bool success = false;

  /* Open connection with known Content-Length */
  esp_err_t err = esp_http_client_open(client, content_length);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "HTTP connection opened, sending data...");

    int written = 0;

    /* Send form start */
    written = esp_http_client_write(client, form_start, strlen(form_start));
    if (written < 0) {
      ESP_LOGE(TAG, "Failed to write form_start");
      goto cleanup;
    }

    /* Send image data */
    written =
        esp_http_client_write(client, (const char *)image_data, image_size);
    if (written < 0) {
      ESP_LOGE(TAG, "Failed to write image data");
      goto cleanup;
    }
    ESP_LOGI(TAG, "Sent %d bytes of image data", written);

    /* Send form end */
    written = esp_http_client_write(client, form_end, strlen(form_end));
    if (written < 0) {
      ESP_LOGE(TAG, "Failed to write form_end");
      goto cleanup;
    }

    /* Get response */
    esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    ESP_LOGI(TAG, "HTTP response status: %d", status_code);

    if (status_code >= 200 && status_code < 300) {
      success = true;
    } else {
      ESP_LOGW(TAG, "Server returned error status: %d", status_code);
    }
  } else {
    ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
  }

cleanup:
  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  return success;
}

/**
 * Periodic status timer callback
 */
static void status_timer_callback(TimerHandle_t timer) {
  uint8_t primary = 0;
  uint8_t sta_mac[6] = {0};
  wifi_ap_record_t ap_info = {0};
  wifi_second_chan_t second = 0;
  wifi_sta_list_t sta_list = {0};

  esp_wifi_sta_get_ap_info(&ap_info);
  esp_wifi_get_mac(ESP_IF_WIFI_STA, sta_mac);
  esp_wifi_ap_get_sta_list(&sta_list);
  esp_wifi_get_channel(&primary, &second);

  ESP_LOGI(TAG, "=== RELAY NODE STATUS ===");
  ESP_LOGI(TAG, "Channel: %d | Level: %d", primary, esp_mesh_lite_get_level());
  ESP_LOGI(TAG, "Self MAC: " MACSTR, MAC2STR(sta_mac));

  if (ap_info.rssi != 0) {
    ESP_LOGI(TAG, "Parent BSSID: " MACSTR " | RSSI: %d dBm",
             MAC2STR(ap_info.bssid), ap_info.rssi);
  }

  ESP_LOGI(TAG, "Direct children: %d", sta_list.num);
  ESP_LOGI(TAG, "Motion events: %" PRIu32 " | Camera: %s", s_motion_count,
           s_camera_ready ? "OK" : "FAILED");
  ESP_LOGI(TAG, "Power save: %s", s_power_save_active ? "ACTIVE" : "DISABLED");
  ESP_LOGI(TAG, "Free heap: %" PRIu32 " bytes", esp_get_free_heap_size());

  /* List connected children */
  for (int i = 0; i < sta_list.num; i++) {
    ESP_LOGI(TAG, "  Child[%d]: " MACSTR, i, MAC2STR(sta_list.sta[i].mac));
  }

  ESP_LOGI(TAG, "=========================");
}
