/*
 * Leaf Node Module Header
 * 
 * Handles leaf node specific functionality:
 * - Deep sleep with ULP PIR monitoring
 * - Wake-up handling and mesh reconnection
 * - Motion event processing
 */

#ifndef LEAF_NODE_H
#define LEAF_NODE_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Time to stay awake after motion detection before sleeping again (ms) */
#define LEAF_AWAKE_TIME_MS          30000   /* 30 seconds */

/* Minimum time connected to mesh before allowing sleep (ms) */
#define LEAF_MIN_MESH_TIME_MS       5000    /* 5 seconds */

/**
 * @brief Initialize leaf node subsystems
 * 
 * Sets up ULP PIR monitoring and prepares for low power operation.
 * Should be called after mesh_lite is initialized.
 * 
 * @return ESP_OK on success
 */
esp_err_t leaf_node_init(void);

/**
 * @brief Start leaf node operation
 * 
 * Begins the leaf node main loop:
 * - If woken by PIR: process motion event, then sleep
 * - If fresh boot: wait for mesh connection, then sleep
 */
void leaf_node_start(void);

/**
 * @brief Check wake-up reason and handle accordingly
 * 
 * @return true if woken by PIR motion detection
 */
bool leaf_node_check_wakeup_reason(void);

/**
 * @brief Request leaf node to enter deep sleep
 * 
 * Will disconnect from mesh and enter ULP-monitored deep sleep
 */
void leaf_node_request_sleep(void);

/**
 * @brief Get motion detection count since power-on
 * 
 * This counter persists across deep sleep cycles
 * 
 * @return Number of PIR-triggered wake-ups
 */
uint32_t leaf_node_get_motion_count(void);

#ifdef __cplusplus
}
#endif

#endif /* LEAF_NODE_H */
