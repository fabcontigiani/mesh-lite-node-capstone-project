# Arquitectura del Proyecto - TrapcamMesh

## Índice
1. [Visión General](#visión-general)
2. [Estructura del Proyecto](#estructura-del-proyecto)
3. [Componentes Principales](#componentes-principales)
4. [Sistema de Configuración](#sistema-de-configuración)
5. [Sistema de Build](#sistema-de-build)
6. [Flujo de Ejecución](#flujo-de-ejecución)
7. [Conceptos Clave de ESP-IDF](#conceptos-clave-de-esp-idf)

---

## Visión General

Este proyecto está basado en el framework **ESP-IDF (Espressif IoT Development Framework)** y utiliza **ESP-MESH-LITE** para crear una red mallada (mesh) de dispositivos ESP32. El objetivo es establecer una red autónoma donde múltiples dispositivos ESP32 se interconectan formando una topología mesh auto-organizada y auto-sanante.

### ¿Qué es ESP-MESH-LITE?

ESP-MESH-LITE es una solución de red mallada que funciona en modo **SoftAP + Station** simultáneamente:
- **Station (STA)**: Se conecta al router o a otro nodo padre
- **SoftAP (Access Point)**: Permite que otros dispositivos se conecten a él como nodos hijos

La principal ventaja sobre ESP-MESH tradicional es que **cada nodo puede acceder a internet de forma independiente**, sin que los datos pasen por el nodo raíz, simplificando enormemente el desarrollo de aplicaciones.

---

## Estructura del Proyecto

### Directorios Principales

```
trapcam_mesh/
├── main/                      # Aplicación principal modularizada
│   ├── main.c                 # Punto de entrada, selecciona modo root/relay/leaf
│   ├── Kconfig.projbuild      # Opciones de configuración (choice MESH_NODE_ROLE)
│   ├── CMakeLists.txt         # Configuración de build con ULP
│   ├── root_node/             # Módulo para nodo raíz
│   │   ├── root_node.c        # Lógica del nodo root (siempre activo)
│   │   └── root_node.h        # Interfaz pública del módulo root
│   ├── relay_node/            # Módulo para nodo relay
│   │   ├── relay_node.c       # Lógica del nodo relay (light sleep + camera)
│   │   └── relay_node.h       # Interfaz pública del módulo relay
│   ├── leaf_node/             # Módulo para nodos hoja
│   │   ├── leaf_node.c        # Lógica del nodo leaf (deep sleep)
│   │   ├── leaf_node.h        # Interfaz pública del módulo leaf
│   │   ├── camera_driver.c    # Driver para cámara ESP32-CAM (compartido)
│   │   ├── camera_driver.h    # Interfaz del módulo de cámara
│   │   ├── sd_storage.c       # Driver para almacenamiento en SD card
│   │   ├── sd_storage.h       # Interfaz del módulo de almacenamiento SD
│   │   ├── ulp_pir.c          # Interfaz C para el programa ULP
│   │   └── ulp_pir.h          # Declaraciones del módulo ULP PIR
│   └── ulp/                   # Programas del coprocesador ULP
│       └── pir_monitor.S      # Monitor PIR en ensamblador ULP
├── components/                # Componentes locales (symlinks)
├── managed_components/        # Componentes descargados automáticamente
├── esp-mesh-lite/            # Submodulo de ESP-MESH-LITE
├── build/                     # Archivos generados por compilación
├── docs/                      # Documentación del proyecto
└── Archivos de configuración
```

---

## Componentes Principales

### 1. **main/** - Aplicación Principal (Modularizada)

El código está organizado en módulos separados para **nodo root**, **nodo relay** y **nodo leaf**, permitiendo comportamientos diferenciados según el rol en la red mesh.

#### **1.1 main.c - Punto de Entrada**

**Función:** Inicializa el sistema y delega al módulo correspondiente según configuración.

**Flujo de Ejecución en `app_main()`:**

1. **Inicialización común:**
   - NVS (Non-Volatile Storage)
   - Event loop del sistema
   - Interfaces de red (netif)
   - Configuración WiFi (STA + AP)

2. **Configuración Mesh según rol:**
   ```c
   #if defined(CONFIG_MESH_ROOT)
       esp_mesh_lite_set_allowed_level(1);  // Solo nivel 1 (root)
   #elif defined(CONFIG_MESH_RELAY)
       esp_mesh_lite_set_disallowed_level(1);  // No root, pero permite hijos
   #else  // CONFIG_MESH_LEAF
       esp_mesh_lite_set_disallowed_level(1);  // Nodo terminal
   #endif
   ```

3. **Delegación al módulo correspondiente:**
   ```c
   #if defined(CONFIG_MESH_ROOT)
       root_node_init(); root_node_start();
   #elif defined(CONFIG_MESH_RELAY)
       relay_node_init(); relay_node_start();
   #else
       leaf_node_init(); leaf_node_start();
   #endif
   ```

#### **1.2 root_node/ - Módulo Nodo Raíz**

**Archivos:** `root_node.c`, `root_node.h`

**Responsabilidades:**
- Mantener la red mesh activa 24/7
- **Nunca entrar en deep sleep**
- Orquestar la comunicación entre nodos
- Reportar estado del sistema periódicamente

**Funciones Principales:**
- `root_node_init()`: Inicialización del módulo root
- `root_node_start()`: Inicia timer de monitoreo (cada 10s)

**Comportamiento:**
- Siempre conectado al router WiFi
- Nivel 1 en la jerarquía mesh
- SoftAP activo para aceptar nodos hijos
- Timer imprime: canal, nivel, MAC, RSSI, hijos conectados

#### **1.3 relay_node/ - Módulo Nodo Relay**

**Archivos:** `relay_node.c`, `relay_node.h`

**Responsabilidades:**
- Extender la red mesh (permite nodos hijos conectados)
- Ahorrar energía con Light Sleep + Modem Power Save
- Monitorear PIR via GPIO interrupt (no ULP)
- **Capturar y subir fotos** via HTTP/HTTPS (igual que leaf_node)
- Mantener conectividad mesh 24/7

**Funciones Principales:**
- `relay_node_init()`: Configura Power Management, cámara y PIR GPIO
- `relay_node_start()`: Inicia task principal y timer de status
- `relay_node_get_child_count()`: Retorna nodos hijos conectados

**Comportamiento:**
- CPU en Light Sleep automático entre eventos
- WiFi en Modem Power Save (despierta para DTIM beacons)
- SoftAP activo para aceptar nodos hijos
- Captura foto al detectar PIR y la sube al servidor
- Consumo estimado: ~15-25mA *(hipotético, medir en hardware real)*

> **Ver [POWER_MANAGEMENT.md](./POWER_MANAGEMENT.md)** para detalles completos sobre modos de bajo consumo.

#### **1.4 leaf_node/ - Módulo Nodo Hoja**

**Archivos:** `leaf_node.c`, `leaf_node.h`, `ulp_pir.c`, `ulp_pir.h`, `camera_driver.c/h`

**Responsabilidades:**
- Operar en modo de ultra bajo consumo
- Monitorear sensor PIR durante deep sleep
- Despertar solo al detectar movimiento
- Reconectarse a la red mesh tras despertar

**Funciones Principales:**
- `leaf_node_init()`: Configura ULP y GPIO del PIR
- `leaf_node_start()`: Inicia lógica de sueño/despertar
- `leaf_node_request_sleep()`: Solicita entrada a deep sleep

**Ciclo de Operación:**
```
1. Despertar (PIR o timer)
2. Conectar a red mesh
3. Transmitir datos si hay detección
4. Esperar CONFIG_LEAF_AWAKE_TIME_MS
5. Entrar en deep sleep con ULP activo
```

#### **1.5 ulp/ - Coprocesador Ultra Low Power**

**Archivo:** `pir_monitor.S`

**Función:** Monitorear GPIO del sensor PIR mientras el CPU principal duerme.

**Características:**
- Ejecuta mientras CPU principal duerme
- **Usa polling** porque el ULP no soporta interrupciones GPIO
- Periodo de polling configurable (default 100ms)
- Despierta CPU al detectar señal HIGH en el GPIO

> **¿Por qué polling y no interrupciones?** El coprocesador ULP FSM es muy simple y no tiene capacidad de manejar interrupciones de hardware. Por eso se implementa un ciclo de polling que lee el estado del GPIO periódicamente.

**Ver sección [Sistema ULP](#sistema-ulp-ultra-low-power) para detalles completos.**

---

### 2. **components/** - Componentes Locales

Este directorio contiene **symlinks** (enlaces simbólicos) a los componentes de `esp-mesh-lite/components/`. Creados por el script `setup_components.sh`.

#### **2.1 mesh_lite**

**Función:** Implementación del protocolo ESP-MESH-LITE.

**Características:**
- Auto-organización: Los nodos encuentran automáticamente su lugar en la jerarquía
- Auto-sanación: Si un nodo padre falla, los hijos buscan nueva ruta
- Jerarquía flexible: Nodo puede ser root, padre, hijo o leaf
- Acceso independiente a internet: Cada nodo tiene su propia conexión
- Comunicación inter-nodos: Broadcast, unicast, mensajes a root

**Conceptos:**
- **Root Node (Nodo Raíz)**: Nodo conectado directamente al router
- **Parent Node (Nodo Padre)**: Nodo que tiene hijos conectados a su SoftAP
- **Child Node (Nodo Hijo)**: Nodo conectado al SoftAP de otro nodo
- **Level (Nivel)**: Profundidad en jerarquía (root=1, hijo de root=2, etc.)

#### **2.2 wifi_provisioning**

**Función:** Permite configurar credenciales WiFi del router sin hardcodearlas.

**Métodos de aprovisionamiento:**
- BLE (Bluetooth Low Energy)
- SoftAP (usuario se conecta al AP del ESP32)
- Console (comandos por puerto serie)

**Beneficio:** Usuario final puede configurar WiFi desde app móvil sin recompilar firmware.

---

### 3. **managed_components/** - Dependencias Administradas

Estos componentes son descargados automáticamente por el **IDF Component Manager** según `idf_component.yml`.

#### **3.1 espressif__iot_bridge**

**Función:** Proporciona funcionalidad de "bridge" (puente) de red.

**Capacidades:**
- NAT (Network Address Translation): Permite que nodos hijos accedan a internet a través del padre
- IP Forwarding: Reenvío de paquetes entre interfaces (STA ↔ AP)
- DHCP Server: Asigna IPs a dispositivos conectados al SoftAP
- DNS Relay: Reenvía consultas DNS

**Por qué es importante:** Es el corazón que permite que ESP-MESH-LITE funcione. Cada nodo actúa como router para sus hijos.

#### **3.2 espressif__qrcode**

**Función:** Generación de códigos QR en consola serial.

**Uso típico:** Mostrar QR con credenciales WiFi para aprovisionamiento rápido desde app móvil.

#### **3.3 espressif__esp_modem**

**Función:** Soporte para módems celulares (4G/LTE).

**Uso:** Si se desea que el nodo raíz obtenga internet desde módem en lugar de WiFi router.

---

### 4. **esp-mesh-lite/** - Submódulo Git

**Función:** Repositorio oficial de Espressif con código fuente de ESP-MESH-LITE.

**Estructura:**
- `components/`: Código de los componentes mesh_lite y wifi_provisioning
- `examples/`: Ejemplos de uso (este proyecto está basado en el ejemplo `mesh_local_control`)
- `docs/`: Documentación detallada

**Por qué submódulo:** Permite actualizar a nuevas versiones de mesh-lite sin copiar archivos manualmente.

---

### 5. **build/** - Directorio de Compilación

Generado por CMake/Ninja al ejecutar `idf.py build`. Contiene:

- **bootloader/**: Bootloader compilado (primera etapa de arranque)
- **partition_table**: Tabla de particiones (ubicación de app, nvs, etc.)
- **trapcam_mesh.elf**: Ejecutable con símbolos de debug
- **trapcam_mesh.bin**: Binario final para flashear
- **compile_commands.json**: Usado por IDEs para autocompletado y análisis
- **sdkconfig.h**: Configuración en formato C header
- **flash_args**: Argumentos para herramienta de flasheo

---

## Sistema de Configuración

ESP-IDF usa **Kconfig** para configuración centralizada. Similar a la configuración del kernel Linux.

### Archivos de Configuración

#### **sdkconfig**
- Archivo principal generado por `idf.py menuconfig`
- Contiene TODAS las opciones de configuración (miles de líneas)
- No se debe editar manualmente
- Específico de cada máquina (no commitear a Git)

#### **sdkconfig.defaults**
- Valores por defecto del proyecto
- Se aplica en builds limpios
- SÍ se commitea a Git
- Ejemplo:
  ```
  CONFIG_FREERTOS_HZ=1000              # Tick rate del scheduler
  CONFIG_LWIP_IP_FORWARD=y             # Habilitar forwarding de IP
  CONFIG_LWIP_IPV4_NAPT=y              # Habilitar NAT
  CONFIG_MESH_LITE_ENABLE=y            # Habilitar mesh lite
  ```

#### **Kconfig.projbuild**
- Define opciones de configuración específicas del proyecto
- Se integra en `idf.py menuconfig`
- Opciones principales de este proyecto:

**Configuración de Red:**
```
CONFIG_ROUTER_SSID           # SSID del router WiFi
CONFIG_ROUTER_PASSWORD       # Password del router
CONFIG_MESH_ROOT             # true=nodo raíz, false=nodo hoja
```

**Configuración de Nodo Leaf (solo si CONFIG_MESH_ROOT=n):**
```
CONFIG_LEAF_PIR_GPIO         # GPIO del sensor PIR (default: 12)
CONFIG_LEAF_AWAKE_TIME_MS    # Tiempo activo antes de dormir (default: 30000ms)
CONFIG_LEAF_ULP_POLL_PERIOD_US  # Periodo de polling ULP (default: 100000µs)
```

### Acceso a configuración en código

```c
// Las opciones CONFIG_* se convierten en #define en sdkconfig.h
#include "sdkconfig.h"

char* ssid = CONFIG_ROUTER_SSID;      // Acceso directo
char* pass = CONFIG_ROUTER_PASSWORD;
```

---

## Sistema de Build

### CMake + Ninja

ESP-IDF usa CMake como sistema de build, que genera archivos para Ninja (herramienta de build rápida).

#### **CMakeLists.txt (raíz)**
```cmake
cmake_minimum_required(VERSION 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(trapcam_mesh)
```

**Función:**
- Define el nombre del proyecto
- Incluye el sistema de build de ESP-IDF
- ESP-IDF automáticamente descubre componentes en `components/` y `main/`

#### **main/CMakeLists.txt**
```cmake
idf_component_register(
    SRCS "main.c"
         "root_node/root_node.c"
         "leaf_node/leaf_node.c"
         "leaf_node/ulp_pir.c"
    INCLUDE_DIRS "." "root_node" "leaf_node"
    REQUIRES ulp esp_timer nvs_flash ...
)

# Configuración del programa ULP
set(ulp_app_name ulp_pir_monitor)
set(ulp_sources "ulp/pir_monitor.S")
set(ulp_exp_dep_srcs "leaf_node/ulp_pir.c")
ulp_embed_binary(${ulp_app_name} "${ulp_sources}" "${ulp_exp_dep_srcs}")
```

**Función:**
- Registra los archivos fuente de todos los módulos
- Define directorios de includes para cada submódulo
- Configura el embedding del programa ULP en el binario final
- Especifica dependencias con otros componentes (incluyendo `ulp`)

#### **main/idf_component.yml**
```yaml
dependencies:
  idf: ">=5.0"
  mesh_lite:
    version: "*"
    override_path: "../../../components/mesh_lite"
```

**Función:**
- Manifiesto del IDF Component Manager
- Especifica dependencias con versiones
- `override_path` indica usar versión local en lugar de descargar

### Proceso de Build

```bash
idf.py build
```

**Pasos internos:**
1. CMake analiza todos los CMakeLists.txt
2. Descarga managed_components si es necesario
3. Genera Kconfig y procesa sdkconfig
4. Genera build.ninja con reglas de compilación
5. Ninja ejecuta compilación paralela
6. Genera .elf, .bin, .map
7. Calcula direcciones de flash y genera flasher_args.json

---

## Flujo de Ejecución

### 1. Arranque del Hardware

1. **ROM Bootloader** (en chip):
   - Lee bootloader de segunda etapa desde flash
   - Verifica integridad (opcional)
   - Salta a bootloader de segunda etapa

2. **Bootloader de Segunda Etapa** (`build/bootloader/`):
   - Lee partition table
   - Verifica app (opcional)
   - Habilita seguridad (opcional)
   - Carga app en RAM y ejecuta

3. **App** (`trapcam_mesh.bin`):
   - Inicializa heap, stacks
   - Inicia FreeRTOS scheduler
   - Llama a `app_main()`

### 2. Inicialización de la Red Mesh

1. **Configuración Inicial:**
   ```
   WiFi STA: Intenta conectarse a router
   WiFi AP:  Inicia SoftAP para aceptar nodos hijos
   ```

2. **Determinación de Rol:**
   - Si conecta al router → Se vuelve **Root Node**
   - Si no hay router pero hay mesh → Busca **Parent Node**
   - Si está solo → Se vuelve Root sin internet (mesh standalone)

3. **Jerarquía Dinámica:**
   ```
   Level 1: Root (conectado a router)
     ├─ Level 2: Child 1
     │    ├─ Level 3: Child 1.1
     │    └─ Level 3: Child 1.2
     └─ Level 2: Child 2
   ```

4. **Auto-sanación:**
   - Si Child 1 pierde conexión con Root
   - Child 1.1 y 1.2 buscan nuevo padre (pueden conectarse a Child 2)
   - Red se reorganiza automáticamente

### 3. Monitoreo del Sistema

- **Timer de información**: Cada 10 segundos se imprime el estado de la red
- **Información mostrada**:
  - Canal WiFi actual
  - Nivel en jerarquía mesh
  - MAC address propia
  - BSSID y RSSI del nodo padre
  - Lista de nodos hijos conectados
  - Memoria libre disponible

---

## Sistema ULP (Ultra Low Power)

El coprocesador ULP FSM permite monitorear sensores mientras el CPU principal está en deep sleep, manteniendo un consumo reducido.

### Arquitectura del Sistema ULP

```
┌─────────────────────────────────────────────────────────────────┐
│                         ESP32                                   │
│  ┌──────────────┐                      ┌──────────────────────┐ │
│  │   CPU Main   │◄──── wake signal ────│    ULP Coprocessor   │ │
│  │  (sleeping)  │                      │     (running)        │ │
│  └──────────────┘                      └──────────┬───────────┘ │
│                                                   │             │
│                                        ┌──────────▼───────────┐ │
│                                        │   RTC GPIO 15        │ │
│                                        │   (GPIO 12)          │ │
│                                        └──────────┬───────────┘ │
└───────────────────────────────────────────────────┼─────────────┘
                                                    │
                                           ┌────────▼────────┐
                                           │   Sensor PIR    │
                                           │   (HC-SR501)    │
                                           └─────────────────┘
```

### Programa ULP: pir_monitor.S

El programa ULP está escrito en ensamblador y realiza las siguientes operaciones:

```asm
entry:
    /* Lee estado del GPIO 12 (RTC_GPIO15) */
    READ_RTC_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S + 15, 1)
    
    /* Si HIGH → despertar CPU */
    jumpr wake_up, 1, GE
    
    /* Si LOW → continuar durmiendo */
    halt

wake_up:
    /* Marcar flag de detección */
    move r3, pir_triggered
    move r2, 1
    st r2, r3, 0
    
    /* Incrementar contador de wakeups */
    move r3, wakeup_counter
    ld r2, r3, 0
    add r2, r2, 1
    st r2, r3, 0
    
    /* Despertar CPU principal */
    wake
    halt
```

**Variables exportadas al programa principal:**
- `ulp_wakeup_counter`: Contador de veces que ULP despertó al CPU
- `ulp_pir_triggered`: Flag indicando si fue por detección PIR

### Mapeo de GPIO a RTC GPIO

| GPIO Normal | RTC GPIO | Uso en ULP |
|-------------|----------|------------|
| GPIO 0      | RTC_GPIO11 | Strapping |
| GPIO 2      | RTC_GPIO12 | Strapping |
| GPIO 4      | RTC_GPIO10 | Disponible |
| **GPIO 12** | **RTC_GPIO15** | **PIR Sensor** |
| GPIO 13     | RTC_GPIO14 | Disponible |
| GPIO 14     | RTC_GPIO16 | Disponible |
| GPIO 15     | RTC_GPIO13 | Strapping |
| GPIO 25     | RTC_GPIO6  | DAC1 |
| GPIO 26     | RTC_GPIO7  | DAC2 |
| GPIO 27     | RTC_GPIO17 | Disponible |
| GPIO 32     | RTC_GPIO9  | XTAL32 |
| GPIO 33     | RTC_GPIO8  | XTAL32 |
| GPIO 34     | RTC_GPIO4  | Input only |
| GPIO 35     | RTC_GPIO5  | Input only |
| GPIO 36     | RTC_GPIO0  | Input only |
| GPIO 39     | RTC_GPIO3  | Input only |

### Configuración sdkconfig para ULP

```ini
# Habilitar coprocesador ULP
CONFIG_ULP_COPROC_ENABLED=y
CONFIG_ULP_COPROC_TYPE_FSM=y
CONFIG_ULP_COPROC_RESERVE_MEM=512
```

### Inicialización ULP en C (ulp_pir.c)

```c
#include "ulp.h"
#include "ulp_pir_monitor.h"
#include "driver/rtc_io.h"

// Referencia al binario ULP generado
extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_pir_monitor_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_pir_monitor_bin_end");

esp_err_t ulp_pir_init(void)
{
    // Configurar GPIO como entrada RTC con pull-down
    rtc_gpio_init(GPIO_NUM_12);
    rtc_gpio_set_direction(GPIO_NUM_12, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_en(GPIO_NUM_12);
    rtc_gpio_hold_en(GPIO_NUM_12);

    // Cargar programa ULP en memoria RTC
    size_t size = (ulp_main_bin_end - ulp_main_bin_start) / sizeof(uint32_t);
    ulp_load_binary(0, ulp_main_bin_start, size);

    // Configurar periodo de polling (cada 100ms)
    ulp_set_wakeup_period(0, CONFIG_LEAF_ULP_POLL_PERIOD_US);

    return ESP_OK;
}

void ulp_pir_enter_deep_sleep(void)
{
    // Habilitar wakeup por ULP
    esp_sleep_enable_ulp_wakeup();
    
    // Iniciar programa ULP
    ulp_run(&ulp_entry - RTC_SLOW_MEM);
    
    // Entrar en deep sleep
    esp_deep_sleep_start();
}
```

### Flujo de Operación Deep Sleep

```
┌─────────────────────────────────────────────────────────────────┐
│                    NODO LEAF - CICLO DE VIDA                    │
└─────────────────────────────────────────────────────────────────┘

     ┌──────────┐
     │  BOOT    │
     └────┬─────┘
          │
          ▼
     ┌──────────┐     ┌─────────────────────────────────────────┐
     │  INIT    │────►│ • Verificar causa de wakeup             │
     └────┬─────┘     │ • Inicializar ULP si primer boot        │
          │           │ • Configurar GPIO PIR                   │
          │           └─────────────────────────────────────────┘
          ▼
     ┌──────────┐     ┌─────────────────────────────────────────┐
     │ CONNECT  │────►│ • Conectar a red mesh                   │
     └────┬─────┘     │ • Esperar asignación de IP              │
          │           │ • Sincronizar con nodo root             │
          │           └─────────────────────────────────────────┘
          ▼
     ┌──────────┐     ┌─────────────────────────────────────────┐
     │  ACTIVE  │────►│ • Si wakeup por PIR: enviar alerta      │
     └────┬─────┘     │ • Procesar comandos pendientes          │
          │           │ • Mantener activo CONFIG_LEAF_AWAKE_TIME│
          │           └─────────────────────────────────────────┘
          ▼
     ┌──────────┐     ┌─────────────────────────────────────────┐
     │  SLEEP   │────►│ • Desconectar WiFi                      │
     └────┬─────┘     │ • Habilitar ULP wakeup                  │
          │           │ • Entrar en deep sleep                  │
          │           └─────────────────────────────────────────┘
          │
          │ (ULP detecta PIR HIGH)
          │
          └──────────────────────┐
                                 │
                                 ▼
                            ┌──────────┐
                            │  WAKEUP  │──────► Vuelve a INIT
                            └──────────┘
```

### Consumo de Energía (Medido)

| Estado | Consumo | Duración típica |
|--------|---------|-----------------|
| Active (WiFi TX) | ~200mA | 2-5s |
| Active (WiFi idle) | ~100mA | CONFIG_LEAF_AWAKE_TIME |
| Deep Sleep + ULP | **10-15mA** | Indefinido |
| Wakeup transition | ~70mA | ~100ms |

> **Nota:** El consumo en deep sleep de 10-15mA (medido con multímetro) es mayor que el teórico del chip ESP32 (~10µA) debido a otros componentes de la placa: regulador de voltaje, LEDs, etc. Para reducir consumo, considerar usar módulos ESP32 "bare" o deshabilitar componentes no esenciales.

**Ejemplo de cálculo (batería 2000mAh):**
- Consumo en sleep: ~12mA promedio
- Autonomía teórica: 2000/12 ≈ **166 horas** (~7 días)

---

## Conceptos Clave de ESP-IDF

### FreeRTOS

**¿Qué es?** Sistema operativo en tiempo real (RTOS) incluido en ESP-IDF.

**Conceptos:**
- **Task (Tarea)**: Hilo de ejecución con prioridad y stack propio
  ```c
  xTaskCreate(tcp_client_write_task, "tcp_client", 4096, NULL, 5, NULL);
  //          función               nombre        stack  args  prio handle
  ```
- **Scheduler**: Decide qué tarea ejecutar según prioridades
- **Tick**: Unidad de tiempo (CONFIG_FREERTOS_HZ=1000 → 1ms por tick)
- **Queue**: Cola thread-safe para pasar datos entre tareas
- **Semaphore/Mutex**: Sincronización entre tareas
- **Timer**: Callback ejecutado periódicamente

### Event Loop

**Función:** Sistema de eventos asíncronos tipo pub/sub.

**Componentes:**
- **Event Base**: Categoría (IP_EVENT, WIFI_EVENT, etc.)
- **Event ID**: Evento específico (IP_EVENT_STA_GOT_IP, WIFI_EVENT_STA_CONNECTED)
- **Handler**: Función callback ejecutada al recibir evento

**Ejemplo:**
```c
ESP_ERROR_CHECK(esp_event_handler_instance_register(
    IP_EVENT,                          // Base
    IP_EVENT_STA_GOT_IP,              // ID
    &ip_event_sta_got_ip_handler,     // Handler
    NULL,                              // Arg
    NULL                               // Instance handle
));
```

### Netif (Network Interface)

**Función:** Abstracción de interfaces de red.

**Tipos:**
- `WIFI_STA_DEF`: Interfaz WiFi Station
- `WIFI_AP_DEF`: Interfaz WiFi Access Point
- `ETH_DEF`: Interfaz Ethernet (si hay)

**Capa:**
```
App ↔ [Netif] ↔ LwIP (TCP/IP stack) ↔ WiFi Driver ↔ Hardware
```

### NVS (Non-Volatile Storage)

**Función:** Sistema de almacenamiento clave-valor en flash.

**Uso típico:**
- Credenciales WiFi
- Configuración de usuario
- Contadores persistentes
- Calibración

**Particiones:**
- Se define en `partition_table` (típicamente 24KB)
- Soporta wear leveling (distribución de escrituras)

### LwIP (Lightweight IP)

**Función:** Stack TCP/IP embebido.

**Características habilitadas en este proyecto:**
- `CONFIG_LWIP_IP_FORWARD=y`: Forwarding entre interfaces (STA → AP)
- `CONFIG_LWIP_IPV4_NAPT=y`: NAT (cambiar IP origen/destino en paquetes)

**Flujo de paquete (nodo hijo):**
```
App (socket) → LwIP → Netif STA → WiFi → Nodo Padre
                                            ↓
                                    NAT (cambia IP src)
                                            ↓
                                        Router → Internet
```

---

## Resumen de Flujo Completo

1. **Power ON** → ROM Bootloader → 2nd Stage Bootloader → App

2. **`app_main()`**:
   - Inicializa NVS, netif, event loop
   - Configura WiFi (STA + AP)
   - Inicializa mesh-lite
   - Inicia mesh-lite
   - Crea timer de monitoreo

3. **Mesh Auto-organización**:
   - Busca router o nodos mesh existentes
   - Determina rol (root/child)
   - Asigna nivel en jerarquía
   - Inicia SoftAP para aceptar hijos

4. **Operación Normal**:
   - Timer imprime estado cada 10s
   - Mesh monitorea y auto-sana conexiones
   - Nodos hijos pueden entrar/salir dinámicamente

---

## Archivos de Configuración del Proyecto

### setup_components.sh

**Función:** Script bash que crea symlinks desde `esp-mesh-lite/components/` a `components/`.

**Por qué:** CMake de ESP-IDF busca componentes en `components/`. Los symlinks evitan duplicar código.

**Ejecución:** Una sola vez tras clonar repo:
```bash
./setup_components.sh
```

### .gitmodules

Define submódulos Git (esp-mesh-lite). Al clonar con `--recursive` se descargan automáticamente.

---

## Consideraciones de Desarrollo

### Debugging

- **Logs**: Usar `ESP_LOGI()`, `ESP_LOGW()`, `ESP_LOGE()`
- **Log level**: Configurar por tag con `esp_log_level_set()`
- **JTAG**: Soporta GDB para debugging hardware
- **Core dumps**: Guarda estado en flash tras crash

### OTA (Over-The-Air Updates)

- Partition table debe incluir partición `ota_data` y dos particiones `app`
- Usar componente `esp_https_ota`
- Mesh permite actualizar todos los nodos desde root

### Seguridad

- Flash encryption: Cifra firmware en flash
- Secure boot: Verifica firma digital del bootloader/app
- WiFi WPA2/WPA3: Credenciales cifradas
- TLS: Para comunicación TCP segura

---

## Próximos Pasos Sugeridos

1. ~~**Agregar cámara**~~: ✅ Integrado en relay_node y leaf_node
2. ~~**Probar en hardware**~~: ✅ Verificado comportamiento root/relay/leaf
3. ~~**Calibrar PIR**~~: ✅ Ajustada sensibilidad y tiempos
4. ~~**Almacenamiento SD**~~: ✅ Implementado guardado de fotos en tarjeta SD
5. **Optimizar consumo**: Medir consumo real y ajustar tiempos de awake
6. **Provisioning BLE**: Integrar wifi_provisioning para configuración sin recompilar
7. **Nodos relay puros**: Agregar opción de relay sin cámara (solo extensor de red)

---

## Comandos Útiles de Desarrollo

### Compilación y Flash

```bash
# Compilar proyecto
idf.py build

# Flashear y monitorear
idf.py flash monitor

# Solo monitorear (Ctrl+] para salir)
idf.py monitor

# Limpiar build completo (regenera sdkconfig)
idf.py fullclean
```

### Configuración

```bash
# Abrir menú de configuración
idf.py menuconfig

# Cambiar a modo ROOT
# → Mesh Configuration → [x] Configure this device as root node

# Configurar leaf node (solo visible si ROOT está deshabilitado)
# → Mesh Configuration → Leaf Node Configuration → Ajustar parámetros
```

### Depuración

```bash
# Ver logs con nivel debug
idf.py monitor -p /dev/ttyUSB0

# Configurar nivel de log por componente (en código)
esp_log_level_set("LEAF_NODE", ESP_LOG_DEBUG);
esp_log_level_set("ULP_PIR", ESP_LOG_DEBUG);
```

---

## Recursos Adicionales

- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/)
- [ESP-MESH-LITE User Guide](https://github.com/espressif/esp-mesh-lite/blob/release/v1.0/components/mesh_lite/User_Guide.md)
- [Power Management Guide](./POWER_MANAGEMENT.md) — Modos de bajo consumo y nodo Relay propuesto
- [FreeRTOS Documentation](https://www.freertos.org/Documentation/RTOS_book.html)
- [LwIP Wiki](https://lwip.fandom.com/wiki/LwIP_Wiki)

---

**Documento creado:** Enero 2026  
**Última actualización:** Enero 2026  
**Versión del proyecto:** Basado en ESP-MESH-LITE v1.0 y ESP-IDF v5.5.2  
**Características:** Arquitectura modular Root/Leaf con soporte ULP y cámara para detección de movimiento