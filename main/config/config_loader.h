#pragma once

#include <string>
#include <cstdint>
#include "esp_err.h"

struct WifiConfig {
  std::string ssid;
  std::string password;
  bool auto_connect{true};
};

struct ApConfig {
  std::string ssid{"ESP32-School-Setup"};
  std::string password{""};
  uint8_t channel{1};
  uint8_t max_connections{4};
};

struct ServerConfig {
  std::string host{"192.168.1.100"};
  uint16_t http_port{5000};
  uint16_t tcp_port{1337};
  uint16_t port{5000};
  std::string endpoint{"/api/scan"};
  std::string auth_token{"nfcschool_secret_token_2026"};
};

struct GpioConfig {
  int8_t nfc_sda{21};
  int8_t nfc_scl{22};
  int8_t nfc_irq{16};
  int8_t buzzer{26};
  int8_t led_idle{32};
  int8_t led_success{33};
  int8_t led_error{25};

  static bool is_safe_pin(int8_t pin, bool requires_output = true, const char *pin_name = nullptr);
};

struct DeviceConfig {
  std::string device_id{"ESP32_NFC_01"};
  std::string location{"Cổng trường"};
};

struct ScanConfig {
  uint32_t cooldown_ms{200};
  uint32_t release_timeout_ms{100};
  uint32_t feedback_duration_ms{200};
};

struct AppConfig {
  WifiConfig wifi;
  ApConfig ap;
  ServerConfig server;
  GpioConfig gpio;
  DeviceConfig device;
  ScanConfig scan;

  bool has_wifi() const {
    return !wifi.ssid.empty();
  }
};

std::string config_get_json_val(const std::string &json, const std::string &key);

class ConfigLoader {
public:
  static ConfigLoader &instance();

  esp_err_t load();
  esp_err_t save_config(const AppConfig &config);
  esp_err_t save_wifi(const std::string &ssid, const std::string &password);

  std::string to_json(const AppConfig &config) const;

  const AppConfig &get_config() const { return config_; }
  AppConfig &get_config() { return config_; }
  bool has_wifi_configured() const { return config_.has_wifi(); }
  std::string get_raw_json() const;

private:
  ConfigLoader();
  ~ConfigLoader() = default;
  ConfigLoader(const ConfigLoader &) = delete;
  ConfigLoader &operator=(const ConfigLoader &) = delete;

  AppConfig config_;
  bool loaded_;

  void parse_json(const std::string &json);
};
