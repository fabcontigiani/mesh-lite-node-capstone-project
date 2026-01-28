# Determine source files based on node type
set(COMMON_SRCS "main.c")

# Root node sources
set(ROOT_SRCS "root_node/root_node.c")

# Relay node sources
set(RELAY_SRCS "relay_node/relay_node.c")

# Leaf node sources (compiled only when ULP is enabled)
set(LEAF_SRCS "leaf_node/leaf_node.c" 
              "leaf_node/ulp_pir.c"
              "leaf_node/camera_driver.c")

# Combine all sources
set(SRCS ${COMMON_SRCS} ${ROOT_SRCS} ${RELAY_SRCS} ${LEAF_SRCS})

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "." "root_node" "relay_node" "leaf_node"
                       REQUIRES esp_driver_gpio ulp esp_driver_ledc
                       PRIV_REQUIRES esp_wifi 
                                     nvs_flash 
                                     esp_netif 
                                     esp_event
                                     driver
                                     esp_http_client
                                     espressif__iot_bridge 
                                     mesh_lite
                                     espressif__esp32-camera)

# ULP configuration for leaf nodes
# The ULP program monitors GPIO12 for PIR sensor
set(ulp_app_name ulp_pir_monitor)
set(ulp_sources "ulp/pir_monitor.S")
set(ulp_exp_dep_srcs "leaf_node/ulp_pir.c")

ulp_embed_binary(${ulp_app_name} "${ulp_sources}" "${ulp_exp_dep_srcs}")