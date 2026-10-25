#pragma once

#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <cstdint>

#define DEFAULT_LED_IDLE_PIN GPIO_NUM_32
#define DEFAULT_LED_SUCCESS_PIN GPIO_NUM_33
#define DEFAULT_LED_ERROR_PIN GPIO_NUM_25

enum class LedState {
  ALL_OFF,
  IDLE,
  IDLE_OFFLINE,
  WIFI_CONNECTING,
  SERVER_CONNECTING,
  AP_PORTAL,
  HARDWARE_ERROR,
  SYNCING,
  SUCCESS,
  SUCCESS_ONLINE = SUCCESS,
  SUCCESS_OFFLINE,
  UNKNOWN_CARD,
  ERROR
};

class LedController {
public:
  LedController(gpio_num_t idle_pin = DEFAULT_LED_IDLE_PIN,
                gpio_num_t success_pin = DEFAULT_LED_SUCCESS_PIN,
                gpio_num_t error_pin = DEFAULT_LED_ERROR_PIN,
                bool active_high = true);

  ~LedController();

  esp_err_t init();
  void off_all();

  void set_idle();
  void set_idle_offline();
  void set_wifi_connecting();
  void set_server_connecting();
  void set_ap_portal();
  void set_hardware_error();
  void set_syncing(bool syncing);

  void update_status(bool wifi_connected, bool server_connected, bool is_ap_mode);

  void indicate_success(uint32_t duration_ms = 500);
  void indicate_success_online(uint32_t duration_ms = 500);
  void indicate_success_offline(uint32_t duration_ms = 500);
  void indicate_unknown_card(uint32_t duration_ms = 600);
  void indicate_error(uint32_t duration_ms = 500);

  LedState get_state() const;
  bool is_transient_active() const;

private:
  gpio_num_t pin_idle_;
  gpio_num_t pin_success_;
  gpio_num_t pin_error_;
  bool active_high_;

  LedState background_state_;
  LedState transient_state_;
  uint32_t transient_ticks_remaining_;
  uint32_t transient_step_;
  uint32_t tick_counter_;

  esp_timer_handle_t timer_handle_;
  portMUX_TYPE lock_;

  void set_pin_level(gpio_num_t pin, bool on);
  void set_raw_leds(bool yellow, bool green, bool red);
  void tick();

  static void on_timer_tick(void *arg);
};
