// pins_arduino.h is Arduino-ESP32 only — replaced with explicit ESP32-S3
// SPI2 IO_MUX defaults (matches the wiring used in aliro-esp32 main).
// The original rednblkx defaults pulled SS/MISO/MOSI/SCK from Arduino's
// per-board variant header (e.g. esp32s3-devkitc).
#define SS_PIN   10 // Slave Select Pin for SPI communication (CS)
#define MISO_PIN 13 // Master In, Slave Out Pin
#define MOSI_PIN 11 // Master Out, Slave In Pin
#define SCK_PIN  12 // Serial Clock Pin

// MQTT Broker Settings
#define MQTT_HOST "" //IP adress of mqtt broker
#define MQTT_PORT 1883 //Port of mqtt broker
#define MQTT_CLIENTID "" //client-id to connect to mqtt broker
#define MQTT_USERNAME ""  //username to connect to mqtt broker
#define MQTT_PASSWORD ""  //password to connect to mqtt broker

//MQTT Flags
#define MQTT_CUSTOM_STATE_ENABLED 0 // Flag to enable the use of custom states and relevant MQTT Topics
#define MQTT_DISCOVERY true //Enable or disable discovery for home assistant tags functionality, set to true to enable.
#define MQTT_NO_TAG_PUBLISH false // Disable publishing of tags to MQTT topic

//MQTT SSL/TLS Settings
#define MQTT_USE_SSL false // Enable/disable SSL/TLS for MQTT connection
#define MQTT_CA_CERT "" // CA certificate for server validation (PEM format)
#define MQTT_CLIENT_CERT "" // Client certificate for mutual authentication (PEM format)
#define MQTT_CLIENT_KEY "" // Client private key for mutual authentication (PEM format)
#define MQTT_ALLOW_INSECURE false // Allow connections without proper certificate validation (INSECURE - use only for testing)

// MQTT Topics
#define MQTT_LWT_TOPIC "status"
#define MQTT_CUSTOM_STATE_TOPIC "aliro/custom_state" // MQTT Topic for publishing custom lock state
#define MQTT_CUSTOM_STATE_CTRL_TOPIC "aliro/set_custom_state" // MQTT Control Topic with custom lock state
#define MQTT_AUTH_TOPIC "aliro/tap" // MQTT Topic for publishing HomeKey authentication data or RFID UID
#define MQTT_SET_STATE_TOPIC "aliro/set_state" // MQTT Control Topic for the HomeKit lock state (current and target)
#define MQTT_SET_TARGET_STATE_TOPIC "aliro/set_target_state" // MQTT Control Topic for the HomeKit lock target state
#define MQTT_SET_CURRENT_STATE_TOPIC "aliro/set_current_state" // MQTT Control Topic for the HomeKit lock current state
#define MQTT_STATE_TOPIC "aliro/state" // MQTT Topic for publishing the HomeKit lock target state
#define MQTT_PROX_BAT_TOPIC "aliro/set_battery_lvl" // MQTT Control Topic for setting the battery level to be shown in HomeKit
#define MQTT_ALIRO_ALT_ACTION_TOPIC "alt_action" // MQTT Topic for publishing the Alt Action

// MQTT Custom state
#define C_UNLOCK 0
#define C_LOCK 1
#define C_LOCKED 1
#define C_UNLOCKING 2
#define C_UNLOCKED 3
#define C_LOCKING 4
#define C_JAMMED 254
#define C_UNKNOWN 25

// Miscellaneous
#define ALIRO_KEY_COLOR TAN
// Web admin PIN default — first-boot value for the BasicAuth credential
// pair used by the admin web UI (not the Matter setup PIN, which is
// factory-immutable and lives in chip-factory NVS). Operators are expected
// to change this from the captive-portal/admin page after first commissioning.
#define SETUP_CODE "ChangeMe1497"
#define OTA_PWD "aliro-ota" // Password for the legacy multipart /ota upload endpoint
#define DEVICE_NAME "ALIRO" // Friendly device name surfaced in MQTT discovery + AP SSID suffix
#define ALIRO_ALWAYS_UNLOCK 0 // Flag indicating if a successful Aliro authentication should always set and publish the unlock state
#define ALIRO_ALWAYS_LOCK 0  // Flag indicating if a successful Aliro authentication should always set and publish the lock state
#define ALIRO_AUTH_PRECOMPUTE_ENABLED true // Enable Aliro auth precompute cache (faster taps, higher CPU/RAM)
// HS_STATUS_LED / HS_PIN are HomeSpan-specific — left at PIN_UNSET so any
// remaining references in the vendored ConfigManager schema compile, but
// they're not wired to anything: status LED is driven by lockind in main/.
#define HS_STATUS_LED 255 // HomeSpan Status LED GPIO pin (unused; lockind drives the LED)
#define HS_PIN 255 // HomeSpan config-mode button GPIO pin (unused; we use Matter BCW trigger via web/button)
#define BTR_PROX_BAT_ENABLED false // Enable or disable battery monitoring
#define BTR_PROX_BAT_LOW_THRESHOLD 10 // Battery low status threshold percentage

// Ethernet Settings
#define ETH_ENABLED false // Enable or disable Ethernet connectivity
// See https://github.com/rednblkx/HomeKey-ESP32/blob/main/main/include/eth_structs.hpp for supported chips and presets
#define ETH_ACTIVE_PRESET 255 // Ethernet preset index (255 for custom pins)
#define ETH_PHY_TYPE 1 // Ethernet PHY type identifier
// Ethernet RMII Configuration
#define ETH_RMII_CONF_PHY_ADDR 0 // PHY Address
#define ETH_RMII_CONF_MDC_PIN 255 // Management Data Clock Pin
#define ETH_RMII_CONF_MDIO_PIN 255 // Management Data Input/Output Pin
#define ETH_RMII_CONF_POWER_PIN 255 // Power Pin
#define ETH_RMII_CONF_RMII_CLOCK_MODE 0 // RMII Clock Mode
// Ethernet SPI Configuration
#define ETH_SPI_CONF_SPI_FREQ_MHZ 20 // SPI Frequency
#define ETH_SPI_CONF_PIN_CS 255 // Chip Select Pin
#define ETH_SPI_CONF_PIN_IRQ 255 // Interrupt Request Pin
#define ETH_SPI_CONF_PIN_RST 255 // Reset Pin
#define ETH_SPI_CONF_PIN_SCK 255 // Serial Clock Pin
#define ETH_SPI_CONF_PIN_MISO 255 // Master In, Slave Out Pin
#define ETH_SPI_CONF_PIN_MOSI 255 // Master Out, Slave In Pin

// Actions
#define NFC_NEOPIXEL_PIN 255 // GPIO Pin used for NeoPixel
// 0="RGB", 1="RBG", 2="BRG", 3="BGR", 4="GBR", 5="GRB"
#define NEOPIXEL_TYPE 5 // NeoPixel Type
#define NEOPIXEL_SUCCESS_R 0 // Color value for Red - Success HK Auth
#define NEOPIXEL_SUCCESS_G 255 // Color value for Green - Success HK Auth
#define NEOPIXEL_SUCCESS_B 0 // Color value for Blue - Success HK Auth
#define NEOPIXEL_FAIL_R 255 // Color value for Red - Fail HK Auth
#define NEOPIXEL_FAIL_G 0 // Color value for Green - Fail HK Auth
#define NEOPIXEL_FAIL_B 0 // Color value for Blue - Fail HK Auth
#define NEOPIXEL_TAG_EVENT_R 0 // Color value for Red - Tag Event
#define NEOPIXEL_TAG_EVENT_G 0 // Color value for Green - Tag Event
#define NEOPIXEL_TAG_EVENT_B 0 // Color value for Blue - Tag Event
#define NEOPIXEL_TAG_EVENT_TIME 1000 // GPIO Delay time in ms - Tag Event
#define NEOPIXEL_SUCCESS_TIME 1000 // GPIO Delay time in ms - Success HK Auth
#define NEOPIXEL_FAIL_TIME 1000 // GPIO Delay time in ms - Success HK Auth
#define NFC_SUCCESS_PIN 255 // GPIO Pin pulled HIGH or LOW (see NFC_SUCCESS_HL) on success HK Auth
#define NFC_SUCCESS_HL 1 // Flag to define if NFC_SUCCESS_PIN should be held High or Low
#define NFC_SUCCESS_TIME 1000 // How long should NFC_SUCCESS_PIN be held High or Low
#define NFC_FAIL_PIN 255  // GPIO Pin pulled HIGH or LOW (see NFC_SUCCESS_HL) on failed HK Auth
#define NFC_FAIL_HL 1 // Flag to define if NFC_FAIL_PIN should be held High or Low
#define NFC_FAIL_TIME 1000 // How long should NFC_FAIL_PIN be held High or Low
#define TAG_EVENT_PIN 255 // GPIO Pin pulled HIGH or LOW (see TAG_EVENT_HL) on tag event
#define TAG_EVENT_TIMEOUT 1000 // How long should TAG_EVENT_PIN be held High or Low
#define TAG_EVENT_HL 1  // Flag to define if TAG_EVENT_PIN should be held High or Low
#define GPIO_ACTION_PIN 255 // GPIO Pin pulled HIGH or LOW (see GPIO_ACTION_HL) on action
#define GPIO_ACTION_LOCK_STATE 0 // GPIO Pin pulled HIGH or LOW (see GPIO_ACTION_HL) on action
#define GPIO_ACTION_UNLOCK_STATE 1 // GPIO Pin pulled HIGH or LOW (see GPIO_ACTION_HL) on action
#define GPIO_ACTION_MOMENTARY_STATE static_cast<uint8_t>(gpioMomentaryStateStatus::M_DISABLED)
#define GPIO_ACTION_MOMENTARY_TIMEOUT 5000 // GPIO Delay time in ms - Action
#define GPIO_ALIRO_ALT_ACTION_INIT_PIN 255 // GPIO Pin pulled HIGH or LOW (see GPIO_ALIRO_ALT_ACTION_INIT_HL) on action
#define GPIO_ALIRO_ALT_ACTION_INIT_TIMEOUT 5000 // GPIO Delay time in ms - Action
#define GPIO_ALIRO_ALT_ACTION_INIT_LED_PIN 255 // GPIO Pin pulled HIGH or LOW (see GPIO_ALIRO_ALT_ACTION_INIT_HL) on action
#define GPIO_ALIRO_ALT_ACTION_PIN 255 // GPIO Pin pulled HIGH or LOW (see GPIO_ALIRO_ALT_ACTION_HL) on action
#define GPIO_ALIRO_ALT_ACTION_TIMEOUT 5000 // GPIO Delay time in ms - Action
#define GPIO_ALIRO_ALT_ACTION_GPIO_STATE 1 // GPIO Pin pulled HIGH or LOW (see GPIO_ALIRO_ALT_ACTION_HL) on action
#define GPIO_ALIRO_CONTROLLED_STATE false // Defines if HomeKey can control the GPIO state
#define ALIRO_DUMB_SWITCH_MODE true // Bypass lock state external validation

// WebUI
#define WEB_AUTH_ENABLED false
#define WEB_AUTH_USERNAME "admin"
#define WEB_AUTH_PASSWORD "password"
