#include "wifi_manager.h"
#include "dns_server.h"
#include "webserver.h"
#include "config_loader.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <cstring>
#include <algorithm>
#include <map>

static const char *TAG = "WIFI_MGR";

#define NVS_NAMESPACE "wifi_config"
#define NVS_KEY_SSID  "ssid"
#define NVS_KEY_PASS  "pass"

static EventGroupHandle_t s_wifi_event_group = NULL;
const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_FAIL_BIT      = BIT1;

static WebServer *s_web_server = NULL;

WifiManager::WifiManager()
    : sta_netif_(NULL),
      ap_netif_(NULL),
      state_(WifiState::DISCONNECTED),
      ip_address_(""),
      current_ssid_(""),
      retry_count_(0),
      is_connecting_(false),
      initialized_(false) {}

WifiManager::~WifiManager() {
  if (s_web_server) {
    s_web_server->stop();
    delete s_web_server;
    s_web_server = NULL;
  }
  dns_server_stop();
}

void WifiManager::wifi_event_handler(void *arg, esp_event_base_t event_base,
                                     int32_t event_id, void *event_data) {
  WifiManager *self = (WifiManager *)arg;

  if (event_base == WIFI_EVENT) {
    switch (event_id) {
      case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "Wi-Fi Station khoi dong.");
        break;

      case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "Ket noi AP vat ly thanh cong, cho DHCP cap IP...");
        break;

      case WIFI_EVENT_STA_DISCONNECTED: {
        wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "Mat ket noi Wi-Fi (reason: %d)", disconn->reason);
        if (self->state_ == WifiState::CONNECTING) {
          if (self->retry_count_ < 3) {
            self->retry_count_++;
            ESP_LOGI(TAG, "Thu ket noi lai lan %d/3...", self->retry_count_);
            esp_wifi_connect();
            break;
          }
          self->state_ = WifiState::FAILED;
          xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        } else {
          self->state_ = WifiState::DISCONNECTED;
        }
        self->ip_address_ = "";
        break;
      }

      case WIFI_EVENT_AP_STACONNECTED: {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Client ket noi SoftAP: %02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5]);
        break;
      }

      case WIFI_EVENT_AP_STADISCONNECTED: {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Client ngat ket noi SoftAP: %02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5]);
        break;
      }

      default:
        break;
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    char ip_str[32];
    esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str));
    self->ip_address_ = std::string(ip_str);
    self->state_ = WifiState::CONNECTED;
    ESP_LOGI(TAG, "Wi-Fi da ket noi, IP: %s", ip_str);
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

esp_err_t WifiManager::init() {
  if (initialized_) return ESP_OK;

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Khoi tao NVS Flash that bai: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  sta_netif_ = esp_netif_create_default_wifi_sta();
  ap_netif_  = esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  s_wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                      &wifi_event_handler, this, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                      &wifi_event_handler, this, NULL));

  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  ESP_ERROR_CHECK(esp_wifi_start());

  esp_wifi_set_max_tx_power(60);

  initialized_ = true;
  ESP_LOGI(TAG, "Khoi tao WifiManager hoan tat (TX power = 15 dBm).");
  return ESP_OK;
}

bool WifiManager::load_credentials(std::string &ssid, std::string &password) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    return false;
  }

  char buf_ssid[64] = {0};
  char buf_pass[64] = {0};
  size_t len_ssid = sizeof(buf_ssid);
  size_t len_pass = sizeof(buf_pass);

  err = nvs_get_str(handle, NVS_KEY_SSID, buf_ssid, &len_ssid);
  if (err == ESP_OK && len_ssid > 1) {
    ssid = std::string(buf_ssid);
    nvs_get_str(handle, NVS_KEY_PASS, buf_pass, &len_pass);
    password = std::string(buf_pass);
    nvs_close(handle);
    return true;
  }

  nvs_close(handle);
  return false;
}

esp_err_t WifiManager::save_credentials(const std::string &ssid, const std::string &password) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;

  err = nvs_set_str(handle, NVS_KEY_SSID, ssid.c_str());
  if (err == ESP_OK) {
    err = nvs_set_str(handle, NVS_KEY_PASS, password.c_str());
  }
  if (err == ESP_OK) {
    err = nvs_commit(handle);
    ESP_LOGI(TAG, "Da luu thong tin Wi-Fi vao NVS: [ %s ]", ssid.c_str());
  }
  nvs_close(handle);
  return err;
}

esp_err_t WifiManager::clear_credentials() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;

  nvs_erase_all(handle);
  nvs_commit(handle);
  nvs_close(handle);
  ESP_LOGI(TAG, "Da xoa toan bo thong tin Wi-Fi trong NVS.");
  return ESP_OK;
}

esp_err_t WifiManager::start_ap_portal(const char *ap_ssid, const char *ap_pass, uint8_t channel, uint8_t max_connections) {
  const auto &cfg = ConfigLoader::instance().get_config();
  std::string target_ssid = (ap_ssid && strlen(ap_ssid) > 0) ? ap_ssid : cfg.ap.ssid;
  std::string target_pass = (ap_pass != nullptr && strlen(ap_pass) > 0) ? ap_pass : cfg.ap.password;
  uint8_t target_channel = (channel >= 1 && channel <= 13) ? channel : cfg.ap.channel;
  uint8_t target_max_conn = (max_connections > 0) ? max_connections : cfg.ap.max_connections;

  ESP_LOGI(TAG, "Mo SoftAP portal [%s] (Channel %d, Max %d) tai http://192.168.4.1",
           target_ssid.c_str(), target_channel, target_max_conn);

  wifi_config_t ap_cfg;
  memset(&ap_cfg, 0, sizeof(ap_cfg));
  strncpy((char *)ap_cfg.ap.ssid, target_ssid.c_str(), sizeof(ap_cfg.ap.ssid) - 1);
  ap_cfg.ap.ssid_len = target_ssid.length();
  ap_cfg.ap.channel = target_channel;
  ap_cfg.ap.max_connection = target_max_conn;

  if (target_pass.empty() || target_pass.length() < 8) {
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
  } else {
    strncpy((char *)ap_cfg.ap.password, target_pass.c_str(), sizeof(ap_cfg.ap.password) - 1);
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
  }

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));

  dns_server_start("192.168.4.1");

  if (s_web_server == NULL) {
    s_web_server = new WebServer(this);
  }
  s_web_server->start();

  state_ = WifiState::AP_PORTAL_ACTIVE;
  return ESP_OK;
}

void WifiManager::stop_ap_portal() {
  if (s_web_server != NULL) {
    s_web_server->stop();
    delete s_web_server;
    s_web_server = NULL;
    ESP_LOGI(TAG, "Dung web server.");
  }
  dns_server_stop();

  esp_wifi_set_mode(WIFI_MODE_STA);
  ESP_LOGI(TAG, "Chuyen sang che do STA.");
}

bool WifiManager::is_web_server_running() const {
  return (s_web_server != NULL && s_web_server->is_running());
}

bool WifiManager::connect(const std::string &ssid, const std::string &password, uint32_t timeout_ms) {
  if (is_connecting_) {
    ESP_LOGW(TAG, "Tien trinh ket noi dang chay, bo qua yeu cau.");
    return false;
  }
  is_connecting_ = true;
  state_ = WifiState::DISCONNECTED;
  current_ssid_ = ssid;
  retry_count_ = 0;

  esp_wifi_disconnect();
  vTaskDelay(pdMS_TO_TICKS(200));

  xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
  state_ = WifiState::CONNECTING;

  wifi_config_t wifi_cfg;
  memset(&wifi_cfg, 0, sizeof(wifi_cfg));
  strncpy((char *)wifi_cfg.sta.ssid, ssid.c_str(), sizeof(wifi_cfg.sta.ssid));
  strncpy((char *)wifi_cfg.sta.password, password.c_str(), sizeof(wifi_cfg.sta.password));
  wifi_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
  wifi_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  wifi_cfg.sta.pmf_cfg.capable = true;
  wifi_cfg.sta.pmf_cfg.required = false;

  ESP_LOGI(TAG, "Ket noi toi AP: %s...", ssid.c_str());
  esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Loi set_config STA: %s", esp_err_to_name(err));
    is_connecting_ = false;
    state_ = WifiState::FAILED;
    return false;
  }

  err = esp_wifi_connect();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_wifi_connect that bai: %s", esp_err_to_name(err));
    is_connecting_ = false;
    state_ = WifiState::FAILED;
    esp_wifi_disconnect();
    return false;
  }

  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE,
                                         pdFALSE,
                                         pdMS_TO_TICKS(timeout_ms));

  is_connecting_ = false;

  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "Ket noi thanh cong toi Wi-Fi: %s", ssid.c_str());
    state_ = WifiState::CONNECTED;
    return true;
  } else {
    ESP_LOGW(TAG, "Ket noi %s that bai hoac timeout.", ssid.c_str());
    state_ = WifiState::FAILED;
    esp_wifi_disconnect();
    return false;
  }
}

std::vector<WifiScanResult> WifiManager::scan_networks() {
  std::vector<WifiScanResult> results;

  wifi_scan_config_t scan_config;
  memset(&scan_config, 0, sizeof(scan_config));
  scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  scan_config.scan_time.active.min = 40;
  scan_config.scan_time.active.max = 100;

  ESP_LOGI(TAG, "Quet cac mang Wi-Fi xung quanh...");
  esp_err_t err = esp_wifi_scan_start(&scan_config, true);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Loi quet Wi-Fi: %s", esp_err_to_name(err));
    return results;
  }

  uint16_t ap_count = 0;
  esp_wifi_scan_get_ap_num(&ap_count);
  if (ap_count == 0) {
    ESP_LOGI(TAG, "Khong tim thay mang Wi-Fi.");
    return results;
  }

  std::vector<wifi_ap_record_t> ap_records(ap_count);
  esp_wifi_scan_get_ap_records(&ap_count, ap_records.data());

  std::map<std::string, WifiScanResult> unique_aps;
  for (uint16_t i = 0; i < ap_count; ++i) {
    std::string ssid_str((char *)ap_records[i].ssid);
    if (ssid_str.empty()) continue;

    int8_t rssi = ap_records[i].rssi;
    uint8_t auth = ap_records[i].authmode;

    if (unique_aps.find(ssid_str) == unique_aps.end() || unique_aps[ssid_str].rssi < rssi) {
      unique_aps[ssid_str] = {ssid_str, rssi, auth};
    }
  }

  for (auto const &[k, v] : unique_aps) {
    results.push_back(v);
  }

  std::sort(results.begin(), results.end(), [](const WifiScanResult &a, const WifiScanResult &b) {
    return a.rssi > b.rssi;
  });

  ESP_LOGI(TAG, "Quet xong, tim thay %d mang.", (int)results.size());
  return results;
}

esp_err_t WifiManager::start() {
  init();

  ConfigLoader &cfg_loader = ConfigLoader::instance();
  cfg_loader.load();
  const AppConfig &cfg = cfg_loader.get_config();

  std::string target_ssid = "";
  std::string target_pass = "";

  if (cfg.has_wifi()) {
    target_ssid = cfg.wifi.ssid;
    target_pass = cfg.wifi.password;
    ESP_LOGI(TAG, "Dung Wi-Fi tu config.json: %s (tat SoftAP)", target_ssid.c_str());
  } else {
    std::string saved_ssid, saved_pass;
    if (load_credentials(saved_ssid, saved_pass) && !saved_ssid.empty()) {
      target_ssid = saved_ssid;
      target_pass = saved_pass;
      ESP_LOGI(TAG, "Dung Wi-Fi tu NVS: %s", target_ssid.c_str());
    }
  }

  if (!target_ssid.empty()) {
    esp_wifi_set_mode(WIFI_MODE_STA);

    struct ConnTaskParam {
      WifiManager *mgr;
      std::string ssid;
      std::string pass;
    };
    ConnTaskParam *param = new ConnTaskParam{this, target_ssid, target_pass};

    xTaskCreate([](void *pv) {
      ConnTaskParam *p = (ConnTaskParam *)pv;
      vTaskDelay(pdMS_TO_TICKS(300));
      bool ok = p->mgr->connect(p->ssid, p->pass, 12000);
      if (!ok) {
        ESP_LOGW(TAG, "Ket noi %s that bai, bat SoftAP theo config.json...", p->ssid.c_str());
        p->mgr->start_ap_portal();
      } else {
        ESP_LOGI(TAG, "Ket noi Wi-Fi thanh cong.");
      }
      delete p;
      vTaskDelete(NULL);
    }, "bg_sta_conn", 4096, param, 4, NULL);

  } else {
    ESP_LOGI(TAG, "Chua co cau hinh Wi-Fi, mo SoftAP portal theo config.json...");
    start_ap_portal();
  }

  return ESP_OK;
}
