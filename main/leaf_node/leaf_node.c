/*
 * Leaf Node Module Implementation
 *
 * Manages the leaf node lifecycle:
 * 1. Boot/wake-up from ULP (PIR detected motion)
 * 2. Connect to mesh network and get IP
 * 3. Initialize camera
 * 4. Wait for PIR GPIO interrupt (new detection)
 * 5. Capture photo and upload to HTTPS server
 * 6. Return to deep sleep with ULP monitoring
 */

#include "leaf_node.h"
#include "ulp_pir.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

#include "camera_driver.h"
#include "esp_mesh_lite.h"
#include "sd_storage.h"

static const char *TAG = "leaf_node";

/* Server Configuration */
#define USE_HTTPS_UPLOAD                                                       \
  0 /* Set to 1 for HTTPS (remote), 0 for HTTP (local)                         \
     */

#define HTTPS_SERVER_URL "https://proyecto.lab.fabcontigiani.uno/upload/"
#define LOCAL_HTTP_SERVER_IP                                                   \
  "192.168.1.2" /* Change this to your local server IP */
#define LOCAL_HTTP_SERVER_PORT                                                 \
  "8000" /* Change this to your local server port */
#define LOCAL_HTTP_SERVER_URL                                                  \
  "http://" LOCAL_HTTP_SERVER_IP ":" LOCAL_HTTP_SERVER_PORT "/upload/"

#define HTTP_TIMEOUT_MS 30000

/* Let's Encrypt ISRG Root X1 certificate (valid until 2035) */
static const char *letsencrypt_root_ca =
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n";

/* RTC memory to persist data across deep sleep */
static RTC_DATA_ATTR uint32_t s_boot_count = 0;
static RTC_DATA_ATTR uint32_t s_motion_events = 0;

static bool s_initialized = false;
static bool s_camera_ready = false;

/* PIR GPIO interrupt */
static volatile bool s_pir_detected = false;
static SemaphoreHandle_t s_pir_semaphore = NULL;

/* Forward declarations */
static bool wait_for_network_ready(int max_wait_sec);
static bool init_camera_once(void);
static void setup_pir_gpio_interrupt(void);
static bool wait_for_pir_interrupt(int timeout_ms);
static void process_and_upload_photo(void);
static bool upload_photo_to_server(const uint8_t *image_data,
                                   size_t image_size);
static void pir_isr_handler(void *arg);

esp_err_t leaf_node_init(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "Leaf node already initialized");
    return ESP_OK;
  }

  s_boot_count++;
  ESP_LOGI(TAG, "=== LEAF NODE BOOT #%" PRIu32 " ===", s_boot_count);

  /* Check wake-up reason */
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
  case ESP_SLEEP_WAKEUP_ULP:
    ESP_LOGI(TAG, "Woke up from ULP (PIR motion detected!)");
    s_motion_events++;
    break;
  case ESP_SLEEP_WAKEUP_UNDEFINED:
    ESP_LOGI(TAG, "Fresh boot (power-on or reset)");
    break;
  default:
    ESP_LOGI(TAG, "Woke up from other source: %d", wakeup_reason);
    break;
  }

  /* Initialize ULP PIR subsystem (for deep sleep monitoring) */
  esp_err_t ret = ulp_pir_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize ULP PIR: %s", esp_err_to_name(ret));
    return ret;
  }

  /* Initialize SD storage (optional - continues without if unavailable) */
  ret = sd_storage_init();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG,
             "SD card not available - photos will only be uploaded via HTTP");
  }

  /* Create semaphore for PIR interrupt */
  s_pir_semaphore = xSemaphoreCreateBinary();
  if (s_pir_semaphore == NULL) {
    ESP_LOGE(TAG, "Failed to create PIR semaphore");
    return ESP_ERR_NO_MEM;
  }

  s_initialized = true;
  ESP_LOGI(TAG, "Leaf node initialized. Total motion events: %" PRIu32,
           s_motion_events);

  return ESP_OK;
}

void leaf_node_start(void) {
  if (!s_initialized) {
    ESP_LOGE(TAG, "Leaf node not initialized!");
    return;
  }

  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "  LEAF NODE STARTING");
  ESP_LOGI(TAG, "  Boot #%" PRIu32 " | Motion events: %" PRIu32, s_boot_count,
           s_motion_events);
  ESP_LOGI(TAG, "========================================");

  /*
   * STEP 1: Wait for mesh connection and IP address
   */
  ESP_LOGI(TAG, "[STEP 1/5] Connecting to mesh network...");
  if (!wait_for_network_ready(30)) {
    ESP_LOGE(TAG, "Network connection failed! Going to sleep...");
    leaf_node_request_sleep();
    return;
  }
  ESP_LOGI(TAG, "[STEP 1/5] Network ready!");

  /*
   * STEP 2: Initialize camera
   */
  ESP_LOGI(TAG, "[STEP 2/5] Initializing camera...");
  if (!init_camera_once()) {
    ESP_LOGE(TAG, "Camera init failed! Going to sleep...");
    leaf_node_request_sleep();
    return;
  }
  ESP_LOGI(TAG, "[STEP 2/5] Camera ready!");

  /*
   * STEP 3: Setup PIR GPIO interrupt
   */
  ESP_LOGI(TAG, "[STEP 3/5] Setting up PIR GPIO interrupt on GPIO%d...",
           PIR_GPIO_NUM);
  setup_pir_gpio_interrupt();
  ESP_LOGI(TAG, "[STEP 3/5] PIR interrupt configured!");

  /*
   * STEP 4: Wait for PIR trigger or timeout
   */
  ESP_LOGI(TAG, "[STEP 4/5] Waiting for PIR motion detection (max %d ms)...",
           LEAF_AWAKE_TIME_MS);

  if (wait_for_pir_interrupt(LEAF_AWAKE_TIME_MS)) {
    /*
     * STEP 5: PIR detected - capture and upload
     */
    ESP_LOGI(TAG,
             "[STEP 5/5] Motion detected! Capturing and uploading photo...");
    process_and_upload_photo();
  } else {
    ESP_LOGI(TAG,
             "[STEP 5/5] No motion detected during timeout, skipping photo.");
  }

  /* Go to deep sleep */
  ESP_LOGI(TAG, "All done! Entering deep sleep...");
  leaf_node_request_sleep();
}

bool leaf_node_check_wakeup_reason(void) { return ulp_pir_was_triggered(); }

void leaf_node_request_sleep(void) {
  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "  ENTERING DEEP SLEEP");
  ESP_LOGI(TAG, "  ULP will monitor PIR on GPIO%d", PIR_GPIO_NUM);
  ESP_LOGI(TAG, "========================================");

  /* Remove GPIO interrupt before switching to RTC mode */
  gpio_isr_handler_remove(PIR_GPIO_NUM);
  gpio_reset_pin(PIR_GPIO_NUM);

  /* Add small delay to allow logs to flush */
  vTaskDelay(pdMS_TO_TICKS(100));

  ulp_pir_enter_deep_sleep();
  /* Never returns */
}

uint32_t leaf_node_get_motion_count(void) { return s_motion_events; }

/* === Private Functions === */

/**
 * Wait for mesh connection and IP address
 * Returns true if connected, false if timeout
 */
static bool wait_for_network_ready(int max_wait_sec) {
  int waited_sec = 0;
  int level = 0;

  /* Wait for mesh connection */
  ESP_LOGI(TAG, "  Waiting for mesh level > 0...");
  while (waited_sec < max_wait_sec) {
    level = esp_mesh_lite_get_level();
    if (level > 0) {
      ESP_LOGI(TAG, "  Mesh connected at level %d after %d seconds", level,
               waited_sec);
      break;
    }
    if (waited_sec % 5 == 0) {
      ESP_LOGI(TAG, "  Waiting for mesh... (%d/%d sec)", waited_sec,
               max_wait_sec);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    waited_sec++;
  }

  if (level <= 0) {
    ESP_LOGE(TAG, "  Mesh connection timeout after %d seconds", max_wait_sec);
    return false;
  }

  /* Wait for IP address */
  ESP_LOGI(TAG, "  Waiting for IP address...");
  esp_netif_ip_info_t ip_info = {0};
  esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

  while (waited_sec < max_wait_sec) {
    if (sta_netif && esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK) {
      if (ip_info.ip.addr != 0) {
        ESP_LOGI(TAG, "  Got IP: " IPSTR " after %d seconds",
                 IP2STR(&ip_info.ip), waited_sec);
        break;
      }
    }
    if (waited_sec % 5 == 0) {
      ESP_LOGI(TAG, "  Waiting for IP... (%d/%d sec)", waited_sec,
               max_wait_sec);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    waited_sec++;
  }

  if (ip_info.ip.addr == 0) {
    ESP_LOGE(TAG, "  IP address timeout after %d seconds", max_wait_sec);
    return false;
  }

  /* Small delay to stabilize */
  ESP_LOGI(TAG, "  Waiting 2 seconds to stabilize...");
  vTaskDelay(pdMS_TO_TICKS(2000));

  return true;
}

/**
 * Initialize camera (only once per boot)
 */
static bool init_camera_once(void) {
  if (s_camera_ready) {
    ESP_LOGI(TAG, "  Camera already initialized");
    return true;
  }

  if (!camera_is_supported()) {
    ESP_LOGW(TAG, "  Camera not supported on this board");
    return false;
  }

  if (camera_init() != ESP_OK) {
    ESP_LOGE(TAG, "  Camera init failed");
    return false;
  }

  s_camera_ready = true;
  ESP_LOGI(TAG, "  Camera initialized successfully");
  return true;
}

/**
 * Setup GPIO interrupt for PIR sensor
 * Note: Must release RTC GPIO first since ULP was using it
 */
static void setup_pir_gpio_interrupt(void) {
  /* Release GPIO from RTC mode (ULP was using it) */
  rtc_gpio_deinit(PIR_GPIO_NUM);

  /* Configure as normal GPIO input with interrupt */
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << PIR_GPIO_NUM),
      .mode = GPIO_MODE_INPUT,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .intr_type =
          GPIO_INTR_POSEDGE, /* Trigger on rising edge (PIR goes HIGH) */
  };
  gpio_config(&io_conf);

  /* Install GPIO ISR service and add handler */
  gpio_install_isr_service(0);
  gpio_isr_handler_add(PIR_GPIO_NUM, pir_isr_handler, NULL);

  /* Clear any pending interrupts */
  s_pir_detected = false;
  xSemaphoreTake(s_pir_semaphore, 0); /* Clear semaphore if set */

  ESP_LOGI(TAG, "  PIR GPIO interrupt ready on GPIO%d", PIR_GPIO_NUM);
}

/**
 * Wait for PIR interrupt with timeout
 * Returns true if PIR triggered, false if timeout
 */
static bool wait_for_pir_interrupt(int timeout_ms) {
  ESP_LOGI(TAG, "  Monitoring PIR... (timeout: %d ms)", timeout_ms);

  /* Check if PIR is already HIGH */
  if (gpio_get_level(PIR_GPIO_NUM) == 1) {
    ESP_LOGI(TAG, "  PIR already HIGH at start!");
    return true;
  }

  /* Wait for interrupt or timeout */
  if (xSemaphoreTake(s_pir_semaphore, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
    ESP_LOGI(TAG, "  *** PIR INTERRUPT DETECTED! ***");
    return true;
  }

  ESP_LOGI(TAG, "  PIR timeout after %d ms", timeout_ms);
  return false;
}

/**
 * PIR GPIO ISR Handler - called on rising edge
 */
static void IRAM_ATTR pir_isr_handler(void *arg) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  s_pir_detected = true;
  xSemaphoreGiveFromISR(s_pir_semaphore, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
}

/**
 * Capture photo, save to SD, and upload to server
 */
static void process_and_upload_photo(void) {
  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "  MOTION EVENT #%" PRIu32, s_motion_events);
  ESP_LOGI(TAG, "========================================");

  /* Capture photo (with warmup to let sensor auto-calibrate after sleep) */
  camera_fb_t *fb = camera_capture_photo_with_warmup();
  if (!fb) {
    ESP_LOGE(TAG, "Photo capture failed");
    return;
  }

  ESP_LOGI(TAG, "Photo captured: %zu bytes", fb->len);

  /* Save to SD card (if available) */
  if (sd_storage_is_available()) {
    char filename[64];
    esp_err_t sd_ret =
        sd_storage_save_photo(fb->buf, fb->len, filename, sizeof(filename));
    if (sd_ret == ESP_OK) {
      ESP_LOGI(TAG, "Photo saved to SD: %s", filename);
    } else {
      ESP_LOGW(TAG, "Failed to save photo to SD: %s", esp_err_to_name(sd_ret));
    }
  } else {
    ESP_LOGD(TAG, "SD card not available, skipping local save");
  }

  /* Upload to HTTP/HTTPS server */
  bool success = upload_photo_to_server(fb->buf, fb->len);

  if (success) {
    ESP_LOGI(TAG, "Photo uploaded successfully!");
  } else {
    ESP_LOGE(TAG, "Photo upload failed!");
    if (sd_storage_is_available()) {
      ESP_LOGI(TAG, "Photo is still saved on SD card");
    }
  }

  camera_return_frame_buffer(fb);
}

/**
 * Upload photo to server using multipart/form-data
 * Uses HTTPS (remote) or HTTP (local) based on USE_HTTPS_UPLOAD flag
 *
 * @param image_data Pointer to JPEG image data
 * @param image_size Size of image in bytes
 * @return true if upload successful, false otherwise
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
           "img_%02x%02x%02x%02x%02x%02x_%" PRIu32 ".jpg", mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5], s_motion_events);

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

  /* Configure HTTP client - use certificate only for HTTPS */
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

    /* Send in parts - streaming without duplicating memory */
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
