# Guía de Implementación: Modos de Bajo Consumo para ESP-MESH-LITE

> **Estado**: ✅ Implementado  
> **Última actualización**: Enero 2026

## Resumen

Este documento describe la arquitectura de bajo consumo propuesta para nodos intermedios en la red mesh. El objetivo es permitir que nodos que actúan como "relay" (retransmisores) puedan ahorrar energía sin desconectarse de la mesh.

---

## Problema Actual

| Rol | Comportamiento | Limitación |
|-----|----------------|------------|
| **Root** | Siempre encendido | N/A |
| **Leaf** | Deep sleep + ULP | Se desconecta de mesh al dormir |

**Problema**: Si un nodo leaf tiene otros nodos conectados como hijos, cuando entra en deep sleep, esos nodos hijos pierden acceso total a la red mesh.

---

## Arquitectura Propuesta: 3 Roles

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│    ROOT     │────▶│   RELAY     │────▶│    LEAF     │
│  (Nivel 1)  │     │ (Intermedio)│     │ (Terminal)  │
├─────────────┤     ├─────────────┤     ├─────────────┤
│ Active Mode │     │ Light Sleep │     │ Deep Sleep  │
│ ~160-260mA* │     │ + Modem PS  │     │ + ULP       │
│ Sin dormir  │     │ ~15-25mA*   │     │ ~10µA*      │
│ No camera   │     │ ✓ Camera    │     │ ✓ Camera    │
│ ✓ Permite   │     │ ✓ Permite   │     │ ✗ No        │
│   hijos     │     │   hijos     │     │   hijos     │
└─────────────┘     └─────────────┘     └─────────────┘
```

> [!CAUTION]
> **(*) Valores hipotéticos**: Los consumos indicados son estimaciones teóricas basadas en la documentación de Espressif. Los valores reales dependen del hardware específico (reguladores, LEDs, otros componentes) y deben medirse con multímetro.

---

## Modos de Bajo Consumo del ESP32

| Modo | WiFi | CPU | Consumo | Reconexión |
|------|------|-----|---------|------------|
| **Active** | ✅ On | ✅ On | ~160-260mA | N/A |
| **Modem Sleep** | ⚡ DTIM-based | ✅ On | ~20mA | Instantánea |
| **Light Sleep** | ⚡ Mantiene sesión | 💤 Duerme | ~0.8mA | ~50-100ms |
| **Deep Sleep** | ❌ Pierde sesión | 💤 Duerme | ~10µA | 1-5 segundos |

---

## Comportamiento por Rol

### Root Node
- **Siempre activo** - nunca entra en ningún modo de ahorro
- Mantiene conexión con router y mesh
- `esp_mesh_lite_set_allowed_level(1)`

### Relay Node
- **Mantiene conexión mesh** mientras ahorra energía
- WiFi en Modem Sleep (despierta para beacons DTIM)
- CPU en Light Sleep entre tareas
- Puede tener nodos hijos conectados
- PIR via GPIO interrupt (no usa ULP)
- **Captura y sube fotos** via HTTP/HTTPS (igual que Leaf)
- `esp_mesh_lite_set_disallowed_level(1)`

### Leaf Node
- **Deep Sleep + ULP** para mínimo consumo
- Se desconecta completamente de mesh al dormir
- Sin capacidad de tener nodos hijos
- **Captura y sube fotos** via HTTP/HTTPS
- `esp_mesh_lite_set_leaf_node(true)` (opcional)

---

## Configuración Requerida (sdkconfig)

```kconfig
# Power Management (obligatorio para light sleep)
CONFIG_PM_ENABLE=y
CONFIG_FREERTOS_USE_TICKLESS_IDLE=y

# Opcional - puede causar IRAM overflow con cámara
# CONFIG_ESP_WIFI_SLP_IRAM_OPT=y
```

---

## APIs Clave

```c
// WiFi Modem Power Save
esp_wifi_set_ps(WIFI_PS_MIN_MODEM);  // Menos agresivo, más estable
esp_wifi_set_ps(WIFI_PS_MAX_MODEM);  // Más ahorro, puede causar desconexiones

// Light Sleep automático
esp_pm_config_t pm_config = {
    .max_freq_mhz = 240,
    .min_freq_mhz = 80,
    .light_sleep_enable = true
};
esp_pm_configure(&pm_config);

// Marcar nodo como leaf (sin hijos)
esp_mesh_lite_set_leaf_node(true);

// Tolerancia a pérdida de beacons
esp_wifi_set_inactive_time(WIFI_IF_STA, 60);
esp_wifi_set_inactive_time(WIFI_IF_AP, 60);
```

---

## Consideraciones de Implementación

### WiFi Power Save y Mesh

> [!WARNING]
> `WIFI_PS_MAX_MODEM` puede causar desconexiones por SA Query timeout cuando el nodo actúa como relay. Usar `WIFI_PS_MIN_MODEM` o `WIFI_PS_NONE` para mayor estabilidad.

- Modem Sleep funciona con mesh porque mantiene la sesión WiFi activa
- El radio despierta en intervalos DTIM para recibir beacons
- Los nodos hijos no notan que el padre está en power save

### Light Sleep y SoftAP
- El SoftAP puede mantenerse activo durante light sleep
- El ESP32 despierta automáticamente cuando hay tráfico entrante
- Requiere `CONFIG_PM_ENABLE=y` en sdkconfig

### IRAM Overflow
- `CONFIG_ESP_WIFI_SLP_IRAM_OPT=y` puede causar overflow con cámara+mesh
- Light sleep funciona sin esta opción, solo con mayor latencia de wakeup

---

## Flujo de Decisión en Campo

```
¿El nodo necesita estar SIEMPRE disponible?
    └─ SÍ → CONFIG_MESH_ROOT (flashear como root)

¿El nodo puede tener otros nodos conectados como hijos?
    └─ SÍ → CONFIG_MESH_RELAY (usa modem sleep + light sleep)
    └─ NO → CONFIG_MESH_LEAF (usa deep sleep + ULP)
```

---

## Estimación de Consumo (Hipotético)

> [!WARNING]
> Los siguientes valores son **estimaciones teóricas** basadas en documentación de Espressif. El consumo real debe medirse en hardware.

| Rol | Modo Activo | Modo Ahorro | Consumo Promedio* |
|-----|-------------|-------------|-------------------|
| Root | 100% | N/A | ~180mA |
| Relay | 10% activo | 90% light sleep | ~15-25mA |
| Leaf | 0.1% activo | 99.9% deep sleep | ~50µA |

---

## Verificación

1. **Test de conectividad**: Con relay en power save, verificar que leaf downstream puede enviar datos al root
2. **Test de consumo**: Medir corriente en cada modo con multímetro
3. **Test de latencia**: Medir tiempo de respuesta del relay cuando está en light sleep
4. **Test de estabilidad**: Monitorear desconexiones por SA Query timeout o beacon timeout

---

## Estructura de Archivos

```
main/
├── Kconfig.projbuild       # choice MESH_NODE_ROLE (ROOT, RELAY, LEAF)
├── main.c                  # Lógica de selección de rol
├── root_node/
│   ├── root_node.c
│   └── root_node.h
├── relay_node/
│   ├── relay_node.c        # Light sleep + modem PS + camera
│   └── relay_node.h
└── leaf_node/
    ├── leaf_node.c         # Deep sleep + ULP + camera  
    ├── leaf_node.h
    ├── camera_driver.c     # Driver ESP32-CAM compartido
    ├── camera_driver.h
    ├── ulp_pir.c
    └── ulp_pir.h
```

---

## Funcionalidad Futura: Almacenamiento SD

> [!NOTE]
> **Planificado para implementación futura**: Las fotos se guardarán en la tarjeta SD además de subirse por HTTP. Esto permitirá:
> - Backup local de imágenes
> - Recuperación de fotos en caso de falla de red
> - Revisión offline de capturas

---

## Referencias

- [ESP-IDF Power Management](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/power_management.html)
- [ESP-IDF Sleep Modes](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/sleep_modes.html)
- [ESP-MESH-LITE User Guide](../esp-mesh-lite/components/mesh_lite/User_Guide.md)
