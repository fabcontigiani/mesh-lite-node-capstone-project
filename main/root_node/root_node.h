/*
 * Root Node Module Header
 * 
 * Handles root node specific functionality:
 * - Mesh network orchestration
 * - Connection to external router
 * - Data aggregation from leaf nodes
 * 
 * Root node NEVER enters deep sleep to maintain mesh network
 */

#ifndef ROOT_NODE_H
#define ROOT_NODE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize root node subsystems
 * 
 * Sets up root node specific functionality.
 * Root node does NOT use ULP or deep sleep.
 * 
 * @return ESP_OK on success
 */
esp_err_t root_node_init(void);

/**
 * @brief Start root node operation
 * 
 * Begins root node main loop:
 * - Maintains mesh network
 * - Handles data from leaf nodes
 * - Manages router connection
 */
void root_node_start(void);

/**
 * @brief Get count of connected child nodes
 * 
 * @return Number of nodes connected to this root
 */
uint32_t root_node_get_child_count(void);

/**
 * @brief Get total nodes in mesh network
 * 
 * @return Total node count (if node info report is enabled)
 */
uint32_t root_node_get_total_nodes(void);

#ifdef __cplusplus
}
#endif

#endif /* ROOT_NODE_H */
