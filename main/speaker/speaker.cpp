#include "speaker.h"
#include "esp_log.h"

static const char* TAG = "BUZZER_CTRL";

BuzzerController::BuzzerController(gpio_num_t pin, bool active_low)
    : pin_(pin),
      active_low_(active_low),
      task_handle_(nullptr),
      pattern_queue_(nullptr) {}

BuzzerController::~BuzzerController() {
    stop();
    if (task_handle_ != nullptr) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
    if (pattern_queue_ != nullptr) {
        vQueueDelete(pattern_queue_);
        pattern_queue_ = nullptr;
    }
}

esp_err_t BuzzerController::init() {
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << pin_);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = active_low_ ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = active_low_ ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Loi cau hinh GPIO cho Buzzer: %s", esp_err_to_name(err));
        return err;
    }

    set_sound_active(false);

    pattern_queue_ = xQueueCreate(8, sizeof(BuzzerPattern));
    if (pattern_queue_ == nullptr) {
        ESP_LOGE(TAG, "Khong the tao Queue cho Buzzer");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ret = xTaskCreate(&BuzzerController::buzzer_task,
                                 "buzzer_task",
                                 2048,
                                 this,
                                 2,
                                 &task_handle_);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Khong the tao Task cho Buzzer");
        vQueueDelete(pattern_queue_);
        pattern_queue_ = nullptr;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Buzzer khoi tao tren GPIO %d (%s)",
             pin_, active_low_ ? "active-low" : "active-high");
    return ESP_OK;
}

void BuzzerController::set_sound_active(bool on) {
    gpio_set_level(pin_, (active_low_ != on) ? 1 : 0);
}

void BuzzerController::play(BuzzerPattern pattern) {
    if (pattern_queue_ != nullptr) {
        xQueueSend(pattern_queue_, &pattern, 0);
    }
}

void BuzzerController::beep_success() {
    play(BuzzerPattern::BEEP_SUCCESS);
}

void BuzzerController::beep_error() {
    play(BuzzerPattern::BEEP_ERROR);
}

void BuzzerController::beep_boot() {
    play(BuzzerPattern::BEEP_BOOT);
}

void BuzzerController::stop() {
    if (pattern_queue_ != nullptr) {
        xQueueReset(pattern_queue_);
    }
    set_sound_active(false);
}

void BuzzerController::buzzer_task(void* arg) {
    auto* self = static_cast<BuzzerController*>(arg);
    BuzzerPattern pattern;

    while (true) {
        if (xQueueReceive(self->pattern_queue_, &pattern, portMAX_DELAY) == pdTRUE) {
            switch (pattern) {
                case BuzzerPattern::BEEP_SUCCESS:
                    self->set_sound_active(true);
                    vTaskDelay(pdMS_TO_TICKS(70));
                    self->set_sound_active(false);
                    break;

                case BuzzerPattern::BEEP_ERROR:
                    for (int i = 0; i < 3; i++) {
                        self->set_sound_active(true);
                        vTaskDelay(pdMS_TO_TICKS(60));
                        self->set_sound_active(false);
                        if (i < 2) {
                            vTaskDelay(pdMS_TO_TICKS(60));
                        }
                    }
                    break;

                case BuzzerPattern::BEEP_BOOT:
                    for (int i = 0; i < 2; i++) {
                        self->set_sound_active(true);
                        vTaskDelay(pdMS_TO_TICKS(40));
                        self->set_sound_active(false);
                        if (i < 1) {
                            vTaskDelay(pdMS_TO_TICKS(50));
                        }
                    }
                    break;

                case BuzzerPattern::BEEP_LONG:
                    self->set_sound_active(true);
                    vTaskDelay(pdMS_TO_TICKS(400));
                    self->set_sound_active(false);
                    break;

                case BuzzerPattern::OFF:
                default:
                    self->set_sound_active(false);
                    break;
            }
        }
    }
}
