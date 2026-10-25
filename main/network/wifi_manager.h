#pragma once

#include <string>
#include <vector>
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"

struct WifiScanResult {
  std::string ssid;
  int8_t rssi;
  uint8_t auth_mode;
};

enum class WifiState {
  DISCONNECTED,
  CONNECTING,
  CONNECTED,
  AP_PORTAL_ACTIVE,
  FAILED
};

class WifiManager {
public:
  WifiManager();
  ~WifiManager();

  esp_err_t init();
  esp_err_t start();

  esp_err_t start_ap_portal(const char *ap_ssid = nullptr, const char *ap_pass = nullptr, uint8_t channel = 1, uint8_t max_connections = 4);
  void stop_ap_portal();
  bool is_web_server_running() const;

  bool connect(const std::string &ssid, const std::string &password, uint32_t timeout_ms = 12000);
  std::vector<WifiScanResult> scan_networks();

  bool is_connected() const { return state_ == WifiState::CONNECTED; }
  WifiState get_state() const { return state_; }
  std::string get_ip() const { return ip_address_; }
  std::string get_ssid() const { return current_ssid_; }

  static bool load_credentials(std::string &ssid, std::string &password);
  static esp_err_t save_credentials(const std::string &ssid, const std::string &password);
  static esp_err_t clear_credentials();

private:
  esp_netif_t *sta_netif_;
  esp_netif_t *ap_netif_;
  WifiState state_;
  std::string ip_address_;
  std::string current_ssid_;
  int retry_count_;
  bool is_connecting_;
  bool initialized_;

  static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data);
};
