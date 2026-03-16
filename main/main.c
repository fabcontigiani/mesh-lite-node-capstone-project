/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <inttypes.h>

#include "esp_wifi.h"
#include "nvs_flash.h"

#include "esp_bridge.h"
#include "esp_mesh_lite.h"

/* Node-specific modules */
#include "leaf_node/leaf_node.h"
#include "relay_node/relay_node.h"
#include "root_node/root_node.h"

static const char *TAG = "mesh_main";

static esp_err_t esp_storage_init(void) {
  esp_err_t ret = nvs_flash_init();

  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated and needs to be erased
    // Retry nvs_flash_init
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }

  return ret;
}

static void wifi_init(void) {
  // Station
  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = CONFIG_ROUTER_SSID,
              .password = CONFIG_ROUTER_PASSWORD,
          },
  };
  esp_bridge_wifi_set_config(WIFI_IF_STA, &wifi_config);

  // Softap
  wifi_config_t wifi_softap_config = {
      .ap =
          {
              .ssid = CONFIG_BRIDGE_SOFTAP_SSID,
              .password = CONFIG_BRIDGE_SOFTAP_PASSWORD,
          },
  };
  esp_bridge_wifi_set_config(WIFI_IF_AP, &wifi_softap_config);
}

void app_wifi_set_softap_info(void) {
  char softap_ssid[33];
  char softap_psw[64];
  uint8_t softap_mac[6];
  size_t ssid_size = sizeof(softap_ssid);
  size_t psw_size = sizeof(softap_psw);
  esp_wifi_get_mac(WIFI_IF_AP, softap_mac);
  memset(softap_ssid, 0x0, sizeof(softap_ssid));
  memset(softap_psw, 0x0, sizeof(softap_psw));

  if (esp_mesh_lite_get_softap_ssid_from_nvs(softap_ssid, &ssid_size) ==
      ESP_OK) {
    ESP_LOGI(TAG, "Get ssid from nvs: %s", softap_ssid);
  } else {
#ifdef CONFIG_BRIDGE_SOFTAP_SSID_END_WITH_THE_MAC
    snprintf(softap_ssid, sizeof(softap_ssid), "%.25s_%02x%02x%02x",
             CONFIG_BRIDGE_SOFTAP_SSID, softap_mac[3], softap_mac[4],
             softap_mac[5]);
#else
    snprintf(softap_ssid, sizeof(softap_ssid), "%.32s",
             CONFIG_BRIDGE_SOFTAP_SSID);
#endif
    ESP_LOGI(TAG, "Get ssid from nvs failed, set ssid: %s", softap_ssid);
  }

  if (esp_mesh_lite_get_softap_psw_from_nvs(softap_psw, &psw_size) == ESP_OK) {
    ESP_LOGI(TAG, "Get psw from nvs: [HIDDEN]");
  } else {
    strlcpy(softap_psw, CONFIG_BRIDGE_SOFTAP_PASSWORD, sizeof(softap_psw));
    ESP_LOGI(TAG, "Get psw from nvs failed, set psw: [HIDDEN]");
  }

  esp_mesh_lite_set_softap_info(softap_ssid, softap_psw);
}

void app_main() {
  /**
   * @brief Set the log level for serial port printing.
   */
  esp_log_level_set("*", ESP_LOG_INFO);

  esp_storage_init();

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  esp_bridge_create_all_netif();

  wifi_init();

  esp_mesh_lite_config_t mesh_lite_config = ESP_MESH_LITE_DEFAULT_INIT();
  esp_mesh_lite_init(&mesh_lite_config);

  // Configure node role based on Kconfig
#if defined(CONFIG_MESH_ROOT)
  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "  NODE TYPE: ROOT");
  ESP_LOGI(TAG, "  - Will connect to router");
  ESP_LOGI(TAG, "  - Maintains mesh network 24/7");
  ESP_LOGI(TAG, "  - NEVER enters sleep mode");
  ESP_LOGI(TAG, "========================================");
  esp_mesh_lite_set_allowed_level(1);

  /* Initialize root node module */
  ESP_ERROR_CHECK(root_node_init());

#elif defined(CONFIG_MESH_RELAY)
  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "  NODE TYPE: RELAY");
  ESP_LOGI(TAG, "  - Network extender (allows children)");
  ESP_LOGI(TAG, "  - Uses Light Sleep + Modem Power Save");
  ESP_LOGI(TAG, "  - PIR via GPIO interrupt");
  ESP_LOGI(TAG, "========================================");
  esp_mesh_lite_set_disallowed_level(1); /* Cannot be root */
  /* Note: NOT calling esp_mesh_lite_set_leaf_node(true) - allows children */

  /* Initialize relay node module */
  ESP_ERROR_CHECK(relay_node_init());

#else /* CONFIG_MESH_LEAF (default) */
  ESP_LOGI(TAG, "========================================");
  ESP_LOGI(TAG, "  NODE TYPE: LEAF");
  ESP_LOGI(TAG, "  - Connects to mesh via root/parent");
  ESP_LOGI(TAG, "  - Uses ULP for low-power PIR monitoring");
  ESP_LOGI(TAG, "  - Enters deep sleep when idle");
  ESP_LOGI(TAG, "========================================");
  esp_mesh_lite_set_disallowed_level(1);

  /* Initialize leaf node module (sets up ULP) */
  ESP_ERROR_CHECK(leaf_node_init());
#endif

  app_wifi_set_softap_info();

  esp_mesh_lite_start();

  ESP_LOGI(TAG, "Mesh network started successfully");

  /* Start node-specific operation */
#if defined(CONFIG_MESH_ROOT)
  root_node_start();
#elif defined(CONFIG_MESH_RELAY)
  relay_node_start();
#else
  leaf_node_start();
#endif
}