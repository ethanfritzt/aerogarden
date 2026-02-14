#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/ledc.h"

#define PUMP GPIO_NUM_27
#define LED_PANEL GPIO_NUM_33


void change_led(uint32_t duty)
{
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
}

void init_led(void)
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
        .freq_hz = 1,
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

void init_pump(void)
{
    // set the pump gpio config
    gpio_config_t io_config = {
        .pin_bit_mask = ((1ULL << PUMP)), // bit masked gpio pin for 64bit
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, // disable pullup
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // disable pulldown
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_config);

    // set the pump to off
    gpio_set_level(PUMP, 0);
}

void scheduler_task(void *arg)
{
    for ( ; ; )
    {
        change_led(1);
        vTaskDelay(5000);
    }
}

void app_main(void)
{
    // init components
    init_led();
    init_pump();

    xTaskCreate(scheduler_task, "scheduler", 4096, NULL, 5, NULL);
}

