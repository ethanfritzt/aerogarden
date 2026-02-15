#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/ledc.h"

#include "mqtt_client.h"

#define PUMP GPIO_NUM_27
#define LED_PANEL GPIO_NUM_33

static volatile bool pump_enabled = false;
static volatile uint32_t led_duty = 0;

static void change_led(uint32_t duty)
{
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
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
        .freq_hz = 500,
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
        { .filter = "home/aerogarden/pump/state", .qos = 1 },
        { .filter = "home/aerogarden/led/state", .qos = 1 },
    };

    switch((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            esp_mqtt_client_subscribe_multiple(event->client, topic_list, sizeof(topic_list) / sizeof(topic_list[0]));
            break;
        case MQTT_EVENT_DATA:
            if (strcmp(event->topic, "home/aerogarden/pump/set") == 0) {
                if (strcmp(event->data, "ON") == 0) {
                    pump_enabled = true;

                    esp_mqtt_client_publish(event->client, "home/aerogarden/pump/state", "ON", 0, 1, 1);
                }
                
                if (strcmp(event->data, "OFF") == 0) {
                    pump_enabled = false;

                    esp_mqtt_client_publish(event->client, "home/aerogarden/pump/state", "OFF", 0, 1, 1);
                }
            }

            if (strcmp(event->topic, "home/aerogarden/led/set") == 0) {
                if (strcmp(event->data, "ON") == 0) {
                    change_led(led_duty);

                    esp_mqtt_client_publish(event->client, "home/aerogarden/led/state", "ON", 0, 1, 1);
                }

                if (strcmp(event->data, "OFF") == 0) {
                    change_led(led_duty);

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

void app_main(void)
{
    // init components
    init_led();
    init_pump();

    xTaskCreate(scheduler_task, "scheduler", 4096, NULL, 5, NULL);
}

