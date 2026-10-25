#pragma once

#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <cstdint>

#define DEFAULT_BUZZER_PIN GPIO_NUM_26

enum class BuzzerPattern {
    OFF,
    BEEP_SUCCESS,
    BEEP_ERROR,
    BEEP_BOOT,
    BEEP_LONG
};

class BuzzerController {
public:
    BuzzerController(gpio_num_t pin = DEFAULT_BUZZER_PIN, bool active_low = true);
    ~BuzzerController();

    esp_err_t init();
    void beep_success();
    void beep_error();
    void beep_boot();
    void play(BuzzerPattern pattern);
    void stop();

private:
    gpio_num_t pin_;
    bool active_low_;

    TaskHandle_t task_handle_;
    QueueHandle_t pattern_queue_;

    void set_sound_active(bool on);
    static void buzzer_task(void* arg);
};
