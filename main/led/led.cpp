#include "led.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "LED_CTRL";

LedController::LedController(gpio_num_t idle_pin,
                             gpio_num_t success_pin,
                             gpio_num_t error_pin,
                             bool active_high)
    : pin_idle_(idle_pin),
      pin_success_(success_pin),
      pin_error_(error_pin),
      active_high_(active_high),
      background_state_(LedState::ALL_OFF),
      transient_state_(LedState::ALL_OFF),
      transient_ticks_remaining_(0),
      transient_step_(0),
      tick_counter_(0),
      timer_handle_(nullptr),
      lock_(portMUX_INITIALIZER_UNLOCKED) {}

LedController::~LedController() {
  if (timer_handle_ != nullptr) {
    esp_timer_stop(timer_handle_);
    esp_timer_delete(timer_handle_);
    timer_handle_ = nullptr;
  }
}

esp_err_t LedController::init() {
  uint64_t pin_mask = (1ULL << pin_idle_) | (1ULL << pin_success_) | (1ULL << pin_error_);

  gpio_config_t io_conf = {};
  io_conf.pin_bit_mask = pin_mask;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.intr_type = GPIO_INTR_DISABLE;

  esp_err_t err = gpio_config(&io_conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Loi cau hinh GPIO cho LED: %s", esp_err_to_name(err));
    return err;
  }

  set_raw_leds(true, false, false);
  vTaskDelay(pdMS_TO_TICKS(120));
  set_raw_leds(false, true, false);
  vTaskDelay(pdMS_TO_TICKS(120));
  set_raw_leds(false, false, true);
  vTaskDelay(pdMS_TO_TICKS(120));
  set_raw_leds(false, false, false);

  esp_timer_create_args_t timer_args = {};
  timer_args.callback = &LedController::on_timer_tick;
  timer_args.arg = this;
  timer_args.name = "led_ticker";

  err = esp_timer_create(&timer_args, &timer_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Loi tao esp_timer cho LED: %s", esp_err_to_name(err));
    return err;
  }

  err = esp_timer_start_periodic(timer_handle_, 50 * 1000ULL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Loi bat timer LED: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "LED khoi tao thanh cong (pins: Y=%d, G=%d, R=%d)", pin_idle_, pin_success_, pin_error_);
  return ESP_OK;
}

void LedController::set_pin_level(gpio_num_t pin, bool on) {
  gpio_set_level(pin, (active_high_ == on) ? 1 : 0);
}

void LedController::set_raw_leds(bool yellow, bool green, bool red) {
  set_pin_level(pin_idle_, yellow);
  set_pin_level(pin_success_, green);
  set_pin_level(pin_error_, red);
}

void LedController::off_all() {
  portENTER_CRITICAL(&lock_);
  background_state_ = LedState::ALL_OFF;
  transient_state_ = LedState::ALL_OFF;
  transient_ticks_remaining_ = 0;
  portEXIT_CRITICAL(&lock_);
  set_raw_leds(false, false, false);
}

void LedController::set_idle() {
  portENTER_CRITICAL(&lock_);
  background_state_ = LedState::IDLE;
  portEXIT_CRITICAL(&lock_);
}

void LedController::set_idle_offline() {
  portENTER_CRITICAL(&lock_);
  background_state_ = LedState::IDLE_OFFLINE;
  portEXIT_CRITICAL(&lock_);
}

void LedController::set_wifi_connecting() {
  portENTER_CRITICAL(&lock_);
  background_state_ = LedState::WIFI_CONNECTING;
  portEXIT_CRITICAL(&lock_);
}

void LedController::set_server_connecting() {
  portENTER_CRITICAL(&lock_);
  background_state_ = LedState::SERVER_CONNECTING;
  portEXIT_CRITICAL(&lock_);
}

void LedController::set_ap_portal() {
  portENTER_CRITICAL(&lock_);
  background_state_ = LedState::AP_PORTAL;
  portEXIT_CRITICAL(&lock_);
}

void LedController::set_hardware_error() {
  portENTER_CRITICAL(&lock_);
  background_state_ = LedState::HARDWARE_ERROR;
  portEXIT_CRITICAL(&lock_);
}

void LedController::set_syncing(bool syncing) {
  portENTER_CRITICAL(&lock_);
  if (syncing) {
    background_state_ = LedState::SYNCING;
  } else if (background_state_ == LedState::SYNCING) {
    background_state_ = LedState::IDLE;
  }
  portEXIT_CRITICAL(&lock_);
}

void LedController::update_status(bool wifi_connected, bool server_connected, bool is_ap_mode) {
  portENTER_CRITICAL(&lock_);
  if (background_state_ == LedState::HARDWARE_ERROR) {
    portEXIT_CRITICAL(&lock_);
    return;
  }

  if (is_ap_mode) {
    background_state_ = LedState::AP_PORTAL;
  } else if (!wifi_connected) {
    background_state_ = LedState::WIFI_CONNECTING;
  } else if (!server_connected) {
    background_state_ = LedState::IDLE_OFFLINE;
  } else {
    background_state_ = LedState::IDLE;
  }
  portEXIT_CRITICAL(&lock_);
}

void LedController::indicate_success(uint32_t duration_ms) {
  indicate_success_online(duration_ms);
}

void LedController::indicate_success_online(uint32_t duration_ms) {
  uint32_t ticks = (duration_ms + 49) / 50;
  if (ticks == 0) ticks = 1;

  portENTER_CRITICAL(&lock_);
  transient_state_ = LedState::SUCCESS_ONLINE;
  transient_ticks_remaining_ = ticks;
  transient_step_ = 0;
  portEXIT_CRITICAL(&lock_);
}

void LedController::indicate_success_offline(uint32_t duration_ms) {
  uint32_t ticks = (duration_ms < 400) ? 8 : (duration_ms / 50);

  portENTER_CRITICAL(&lock_);
  transient_state_ = LedState::SUCCESS_OFFLINE;
  transient_ticks_remaining_ = ticks;
  transient_step_ = 0;
  portEXIT_CRITICAL(&lock_);
}

void LedController::indicate_unknown_card(uint32_t duration_ms) {
  uint32_t ticks = (duration_ms < 500) ? 10 : (duration_ms / 50);

  portENTER_CRITICAL(&lock_);
  transient_state_ = LedState::UNKNOWN_CARD;
  transient_ticks_remaining_ = ticks;
  transient_step_ = 0;
  portEXIT_CRITICAL(&lock_);
}

void LedController::indicate_error(uint32_t duration_ms) {
  uint32_t ticks = (duration_ms + 49) / 50;
  if (ticks == 0) ticks = 1;

  portENTER_CRITICAL(&lock_);
  transient_state_ = LedState::ERROR;
  transient_ticks_remaining_ = ticks;
  transient_step_ = 0;
  portEXIT_CRITICAL(&lock_);
}

LedState LedController::get_state() const {
  if (transient_state_ != LedState::ALL_OFF && transient_ticks_remaining_ > 0) {
    return transient_state_;
  }
  return background_state_;
}

bool LedController::is_transient_active() const {
  return (transient_state_ != LedState::ALL_OFF && transient_ticks_remaining_ > 0);
}

void LedController::tick() {
  portENTER_CRITICAL(&lock_);
  tick_counter_++;

  bool y = false;
  bool g = false;
  bool r = false;

  if (transient_state_ != LedState::ALL_OFF && transient_ticks_remaining_ > 0) {
    transient_ticks_remaining_--;
    transient_step_++;

    switch (transient_state_) {
      case LedState::SUCCESS_ONLINE:
        g = true;
        break;

      case LedState::SUCCESS_OFFLINE: {
        uint32_t s = transient_step_;
        if ((s >= 1 && s <= 2) || (s >= 4 && s <= 5)) {
          g = true;
        }
        break;
      }

      case LedState::UNKNOWN_CARD: {
        uint32_t s = transient_step_;
        if ((s >= 1 && s <= 2) || (s >= 4 && s <= 5) || (s >= 7 && s <= 8)) {
          r = true;
        }
        break;
      }

      case LedState::ERROR:
        r = true;
        break;

      default:
        break;
    }

    if (transient_ticks_remaining_ == 0) {
      transient_state_ = LedState::ALL_OFF;
      transient_step_ = 0;
    }
  } else {
    switch (background_state_) {
      case LedState::IDLE:
        y = true;
        break;

      case LedState::IDLE_OFFLINE: {
        uint32_t phase = tick_counter_ % 30;
        y = (phase < 28);
        break;
      }

      case LedState::WIFI_CONNECTING: {
        uint32_t phase = tick_counter_ % 20;
        y = (phase < 10);
        break;
      }

      case LedState::SERVER_CONNECTING: {
        uint32_t phase = tick_counter_ % 6;
        y = (phase < 3);
        break;
      }

      case LedState::AP_PORTAL: {
        uint32_t phase = tick_counter_ % 10;
        if (phase < 5) {
          y = true;
        } else {
          g = true;
        }
        break;
      }

      case LedState::HARDWARE_ERROR: {
        uint32_t phase = tick_counter_ % 10;
        r = (phase < 5);
        break;
      }

      case LedState::SYNCING: {
        uint32_t phase = tick_counter_ % 4;
        g = (phase < 2);
        break;
      }

      case LedState::ALL_OFF:
      default:
        break;
    }
  }

  portEXIT_CRITICAL(&lock_);

  set_raw_leds(y, g, r);
}

void LedController::on_timer_tick(void *arg) {
  auto *self = static_cast<LedController *>(arg);
  if (self) {
    self->tick();
  }
}
