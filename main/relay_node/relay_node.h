/*
 * Relay Node Module Header
 *
 * Handles relay node specific functionality:
 * - Network extension (allows child nodes)
 * - Light Sleep + Modem Power Save for energy efficiency
 * - PIR detection via GPIO interrupt (not ULP)
 *
 * Relay node maintains mesh connectivity while saving power,
 * unlike leaf nodes which disconnect during deep sleep.
 *
 * See docs/POWER_MANAGEMENT.md for detailed information.
 */

#ifndef RELAY_NODE_H
#define RELAY_NODE_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize relay node subsystems
 *
 * Sets up:
 * - Power Management (Light Sleep + Modem PS)
 * - PIR GPIO interrupt
 * - Status reporting timer
 *
 * Should be called after mesh_lite is initialized.
 *
 * @return ESP_OK on success
 */
esp_err_t relay_node_init(void);

/**
 * @brief Start relay node operation
 *
 * Begins relay node main loop:
 * - Monitors PIR for motion detection
 * - Maintains mesh connectivity
 * - Allows child nodes to connect
 * - CPU enters Light Sleep between events
 */
void relay_node_start(void);

/**
 * @brief Get count of connected child nodes
 *
 * @return Number of nodes connected to this relay's SoftAP
 */
uint32_t relay_node_get_child_count(void);

/**
 * @brief Check if relay node is in power save mode
 *
 * @return true if power save is active
 */
bool relay_node_is_power_save_active(void);

/**
 * @brief Get motion detection count since boot
 *
 * @return Number of PIR-triggered events
 */
uint32_t relay_node_get_motion_count(void);

#ifdef __cplusplus
}
#endif

#endif /* RELAY_NODE_H */
