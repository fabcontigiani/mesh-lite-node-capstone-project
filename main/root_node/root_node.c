/*
 * Root Node Module Implementation
 * 
 * Root node is the mesh coordinator - it:
 * - Connects to external WiFi router for internet access
 * - Maintains the mesh network (never sleeps)
 * - Receives and processes data from leaf nodes
 * - Can forward data to cloud/server
 */

#include "root_node.h"

#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_mesh_lite.h"
#include "cJSON.h"

static const char *TAG = "root_node";

// Handler para mensajes JSON recibidos desde leaf
static cJSON* root_mesh_msg_handler(cJSON *payload, uint32_t seq)
{
    cJSON *size_item = cJSON_GetObjectItem(payload, "photo_size");
    cJSON *event_item = cJSON_GetObjectItem(payload, "motion_event");
    
    if (size_item && cJSON_IsNumber(size_item)) {
        int photo_size = size_item->valueint;
        int motion_event = (event_item && cJSON_IsNumber(event_item)) ? event_item->valueint : -1;
        
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "  PHOTO INFO RECEIVED FROM LEAF");
        ESP_LOGI(TAG, "  Photo size: %d bytes", photo_size);
        if (motion_event >= 0) {
            ESP_LOGI(TAG, "  Motion event #: %d", motion_event);
        }
        ESP_LOGI(TAG, "========================================");
    } else {
        char *s = cJSON_PrintUnformatted(payload);
        ESP_LOGW(TAG, "Received unknown payload from leaf: %s", s ? s : "<null>");
        if (s) free(s);
    }
    return NULL; /* No response */
}

static bool s_initialized = false;
static TimerHandle_t s_status_timer = NULL;

/* Forward declarations */
static void status_timer_callback(TimerHandle_t timer);

esp_err_t root_node_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Root node already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "==============================");
    ESP_LOGI(TAG, "  ROOT NODE INITIALIZATION");
    ESP_LOGI(TAG, "==============================");
    ESP_LOGI(TAG, "This node will NEVER enter deep sleep");
    ESP_LOGI(TAG, "It maintains the mesh network 24/7");

    // Registrar handler de recepción de mensajes
    /* Registrar actions JSON para mensajes entrantes (tipo "photo_info" desde leaf) */
    static const esp_mesh_lite_msg_action_t root_actions[] = {
        {"photo_info", NULL, root_mesh_msg_handler},  // Tipo de mensaje enviado por leaf
        {NULL, NULL, NULL}  // Terminador obligatorio
    };
    esp_mesh_lite_msg_action_list_register(root_actions);

    /*
     * Configure SoftAP inactive time to 60 seconds.
     * This is how long the root node waits before removing
     * a disconnected leaf node from its routing table.
     * Default is ~300 seconds, but we want faster cleanup.
     */
    esp_err_t ret = esp_wifi_set_inactive_time(WIFI_IF_AP, 60);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SoftAP inactive timeout set to 60 seconds");
    } else {
        ESP_LOGW(TAG, "Failed to set inactive time: %s", esp_err_to_name(ret));
    }

    /* Create status timer to periodically print mesh info */
    s_status_timer = xTimerCreate(
        "root_status",
        pdMS_TO_TICKS(10000),  /* Every 10 seconds */
        pdTRUE,                /* Auto-reload */
        NULL,
        status_timer_callback
    );

    if (s_status_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create status timer");
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Root node initialized successfully");

    return ESP_OK;
}

void root_node_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Root node not initialized!");
        return;
    }

    ESP_LOGI(TAG, "Starting root node operation...");
    
    /* Start the status timer */
    xTimerStart(s_status_timer, 0);

    ESP_LOGI(TAG, "Root node running. Mesh network active.");
    ESP_LOGI(TAG, "Waiting for leaf nodes to connect...");

    /*
     * The root node runs indefinitely, handling:
     * - Mesh network management (automatic via mesh_lite)
     * - Data reception from leaf nodes (TODO: implement handlers)
     * - Cloud/server communication (TODO: implement)
     */
}

uint32_t root_node_get_child_count(void)
{
    wifi_sta_list_t sta_list = {0};
    esp_wifi_ap_get_sta_list(&sta_list);
    return sta_list.num;
}

uint32_t root_node_get_total_nodes(void)
{
#if CONFIG_MESH_LITE_NODE_INFO_REPORT
    return esp_mesh_lite_get_mesh_node_number();
#else
    /* If node info report is disabled, return direct children only */
    return root_node_get_child_count() + 1; /* +1 for self */
#endif
}

/* === Private Functions === */

static void status_timer_callback(TimerHandle_t timer)
{
    uint8_t primary = 0;
    uint8_t sta_mac[6] = {0};
    wifi_ap_record_t ap_info = {0};
    wifi_second_chan_t second = 0;
    wifi_sta_list_t sta_list = {0};

    esp_wifi_sta_get_ap_info(&ap_info);
    esp_wifi_get_mac(ESP_IF_WIFI_STA, sta_mac);
    esp_wifi_ap_get_sta_list(&sta_list);
    esp_wifi_get_channel(&primary, &second);

    ESP_LOGI(TAG, "=== ROOT NODE STATUS ===");
    ESP_LOGI(TAG, "Channel: %d | Level: %d (root)", primary, esp_mesh_lite_get_level());
    ESP_LOGI(TAG, "Self MAC: " MACSTR, MAC2STR(sta_mac));
    ESP_LOGI(TAG, "Router BSSID: " MACSTR " | RSSI: %d dBm", 
             MAC2STR(ap_info.bssid), 
             (ap_info.rssi != 0 ? ap_info.rssi : -120));
    ESP_LOGI(TAG, "Direct children: %d", sta_list.num);
    
#if CONFIG_MESH_LITE_NODE_INFO_REPORT
    ESP_LOGI(TAG, "Total mesh nodes: %"PRIu32, esp_mesh_lite_get_mesh_node_number());
#endif

    ESP_LOGI(TAG, "Free heap: %"PRIu32" bytes", esp_get_free_heap_size());

    /* List connected children */
    for (int i = 0; i < sta_list.num; i++) {
        ESP_LOGI(TAG, "  Child[%d]: " MACSTR, i, MAC2STR(sta_list.sta[i].mac));
    }
    
    ESP_LOGI(TAG, "========================");
}
