#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/ledc.h"

#include "mqtt_client.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"

#define PUMP GPIO_NUM_27
#define LED_PANEL GPIO_NUM_33

static volatile bool pump_enabled = false;
static volatile uint32_t led_duty = 0;

// gamma correctness
// correctness value = (linear_value/max_value)^gamma * max_value
static uint32_t gamma_correct(uint32_t value)
{
    float gamma = 2.2;

    // max duty resolution is (2 ** duty_resolution)
    // we are using LEDC_TIMER_12_BIT 
    // so (2 ** 12)
    const int max_duty_resolution = pow(2, 12);

    // return the correctness value
    return (uint32_t)(pow((float)value / max_duty_resolution, gamma) * max_duty_resolution);
}

static void change_led(uint32_t duty)
{
    uint32_t corrected = gamma_correct(duty);
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, corrected);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
}

static void sunrise_led(void)
{
    uint32_t start_value = 0;

    // max value defined by (2 ** duty_resolution)
    uint32_t  max_value = pow(2, 12);

    for (int i = start_value; i <= max_value; i ++) {
        led_duty = i;

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void init_led(void)
{
    // 3 Steps 
    // 1. Timer Configuration - sets signal freq and duty cycle
    // 2. Channel Configuration - associates with the timer and gpio to output signal
    // 3. Change PWM Signal - final step

    // 1.
    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_12_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_config);

    // 2. & 3.
    ledc_channel_config_t channel_config = {
        .gpio_num = LED_PANEL,
        .speed_mode  = LEDC_HIGH_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0, // setting the duty to 0 will init the led off
        .hpoint = 0
    };
    ledc_channel_config(&channel_config);
}

static void init_pump(void)
{
    // set the pump gpio config
    gpio_config_t io_config = {
        .pin_bit_mask = 1ULL << 27, // bit masked gpio pin for 64bit
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_config);

    // set the pump to off
    gpio_set_level(PUMP, 0);
}

void scheduler_task(void *arg)
{
    for ( ; ; ) {
        gpio_set_level(PUMP, pump_enabled);
        change_led(led_duty);

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    esp_mqtt_topic_t topic_list[] = {
        { .filter = "home/aerogarden/pump/set", .qos = 1 },
        { .filter = "home/aerogarden/led/set", .qos = 1 },
    };

    switch(event->event_id) {
        case MQTT_EVENT_CONNECTED:
            esp_mqtt_client_subscribe_multiple(event->client, topic_list, sizeof(topic_list) / sizeof(topic_list[0]));
            break;
        case MQTT_EVENT_DATA:
            if (strncmp(event->topic, "home/aerogarden/pump/set", event->topic_len) == 0) {
                if (strncmp(event->data, "ON", event->data_len) == 0) {
                    pump_enabled = true;

                    esp_mqtt_client_publish(event->client, "home/aerogarden/pump/state", "ON", 0, 1, 1);
                }
                
                if (strncmp(event->data, "OFF", event->data_len) == 0) {
                    pump_enabled = false;

                    esp_mqtt_client_publish(event->client, "home/aerogarden/pump/state", "OFF", 0, 1, 1);
                }
            }

            if (strncmp(event->topic, "home/aerogarden/led/set", event->topic_len) == 0) {
                if (strncmp(event->data, "ON", event->data_len) == 0) {
                    sunrise_led();

                    esp_mqtt_client_publish(event->client, "home/aerogarden/led/state", "ON", 0, 1, 1);
                }

                if (strncmp(event->data, "OFF", event->data_len) == 0) {
                    led_duty = 0;

                    esp_mqtt_client_publish(event->client, "home/aerogarden/led/state", "OFF", 0, 1, 1);
                }
            }
            break;
        default:
            break;
    }
}

void mqtt_app_start(void)
{
    // connect to broker
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_MQTT_BROKER_URI, 
        .credentials.username = CONFIG_MQTT_USER,
        .credentials.authentication.password = CONFIG_MQTT_PASS,
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);

    // register event handler
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    // start mqtt client
    esp_mqtt_client_start(client);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) { 
        esp_wifi_connect();
        mqtt_app_start();
    }
}

void wifi_init_sta(void)
{
    // init network interface layer
    esp_netif_init();

    // set up event loop for system events (WIFI, IP, etc...)
    esp_event_loop_create_default();

    // create default station interface
    // device (this esp32) conencts to AP
    esp_netif_create_default_wifi_sta();

    // According to docs we should always use WIFI_INIT_CONFIG_DEFAULT macro
    // to initialize
    // incase future fields get added to wifi_init_config_t
    // we will overwrite necessary values later
    wifi_init_config_t default_wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&default_wifi_cfg);

    // register wifi event handler
    // WIFI event
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id);

    // IP event
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_got_ip);

    // overwrite the necessary values
    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASS
        }
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);

    // start wifi driver
    esp_wifi_start();
}


void app_main(void)
{
    // initialize NVS
    // needed for esp_wifi & esp_mqtt
    nvs_flash_init();

    wifi_init_sta();

    // init components
    init_led();
    init_pump();

    xTaskCreate(scheduler_task, "scheduler", 4096, NULL, 5, NULL);
}

