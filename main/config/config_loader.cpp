#include "config_loader.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <cstring>
#include <sstream>
#include <vector>

static const char *TAG = "CONFIG_LOADER";
#define NVS_CONFIG_NAMESPACE "app_config"
#define NVS_CONFIG_KEY       "config_json"

extern const uint8_t config_json_start[] asm("_binary_config_json_start");
extern const uint8_t config_json_end[] asm("_binary_config_json_end");

ConfigLoader &ConfigLoader::instance() {
  static ConfigLoader instance;
  return instance;
}

ConfigLoader::ConfigLoader() : loaded_(false) {}

std::string ConfigLoader::get_raw_json() const {
  size_t len = config_json_end - config_json_start;
  if (len == 0) {
    return "{}";
  }
  return std::string((const char *)config_json_start, len);
}

std::string config_get_json_val(const std::string &json, const std::string &key) {
#define get_json_val config_get_json_val
  std::string pattern = "\"" + key + "\"";
  size_t pos = json.find(pattern);
  if (pos == std::string::npos) return "";

  pos = json.find(':', pos);
  if (pos == std::string::npos) return "";

  size_t start = json.find_first_not_of(" \t\r\n", pos + 1);
  if (start == std::string::npos) return "";

  if (json[start] == '"') {
    start++;
    size_t end = json.find('"', start);
    if (end == std::string::npos) return "";
    return json.substr(start, end - start);
  } else {
    size_t end = json.find_first_of(",}\r\n", start);
    if (end == std::string::npos) end = json.length();
    std::string val = json.substr(start, end - start);
    while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();
    return val;
  }
}

static std::string get_json_block(const std::string &json, const std::string &section) {
  std::string pattern = "\"" + section + "\"";
  size_t pos = json.find(pattern);
  if (pos == std::string::npos) return "";

  pos = json.find('{', pos);
  if (pos == std::string::npos) return "";

  int depth = 1;
  size_t i = pos + 1;
  while (i < json.length() && depth > 0) {
    if (json[i] == '{') depth++;
    else if (json[i] == '}') depth--;
    i++;
  }
  return json.substr(pos, i - pos);
}

bool GpioConfig::is_safe_pin(int8_t pin, bool requires_output, const char *pin_name) {
  const char *name = pin_name ? pin_name : "GPIO";

  if (pin < 0) {
    if (!requires_output) return true;
    ESP_LOGW("GPIO_SAFETY", "[%s] Khong the dung chan am (%d) lam chan xuat tin hieu", name, pin);
    return false;
  }

  if (pin >= 6 && pin <= 11) {
    ESP_LOGE("GPIO_SAFETY", "NGUY HIEM: Chan %d (%s) la chan SPI Flash noi bo! Tu dong huy bo de tranh treo chip.", pin, name);
    return false;
  }

  if (pin == 1 || pin == 3) {
    ESP_LOGE("GPIO_SAFETY", "NGUY HIEM: Chan %d (%s) la chan UART0 giao tiep may tinh / nap code! Khong the su dung.", pin, name);
    return false;
  }

  if (pin == 0 || pin == 12) {
    ESP_LOGW("GPIO_SAFETY", "NGUY HIEM: Chan %d (%s) la chan Strapping khoi dong, co the lam hong bootloader hoac sai ap Flash.", pin, name);
    return false;
  }

  if (requires_output && (pin == 34 || pin == 35 || pin == 36 || pin == 39)) {
    ESP_LOGE("GPIO_SAFETY", "LOI: Chan %d (%s) la chan Chi-Doc (Input-Only), khong the xuat tin hieu cho LED/Buzzer/I2C.", pin, name);
    return false;
  }

  if (pin > 39) {
    ESP_LOGE("GPIO_SAFETY", "LOI: Chan %d (%s) vuot qua so luong chan thuc te cua ESP32 (0-39).", pin, name);
    return false;
  }

  return true;
}

void ConfigLoader::parse_json(const std::string &raw_json) {
  std::string wifi_block = get_json_block(raw_json, "wifi");
  if (!wifi_block.empty()) {
    config_.wifi.ssid = get_json_val(wifi_block, "ssid");
    config_.wifi.password = get_json_val(wifi_block, "password");
    std::string auto_conn = get_json_val(wifi_block, "auto_connect");
    config_.wifi.auto_connect = (auto_conn != "false");
  } else {
    config_.wifi.ssid = get_json_val(raw_json, "ssid");
    config_.wifi.password = get_json_val(raw_json, "password");
  }

  std::string ap_block = get_json_block(raw_json, "ap");
  if (!ap_block.empty()) {
    std::string ap_s = get_json_val(ap_block, "ssid");
    if (!ap_s.empty()) config_.ap.ssid = ap_s;
    std::string ap_p = get_json_val(ap_block, "password");
    config_.ap.password = ap_p;
    std::string ap_ch = get_json_val(ap_block, "channel");
    if (!ap_ch.empty()) config_.ap.channel = (uint8_t)std::atoi(ap_ch.c_str());
    std::string ap_mc = get_json_val(ap_block, "max_connections");
    if (!ap_mc.empty()) config_.ap.max_connections = (uint8_t)std::atoi(ap_mc.c_str());
  }

  std::string server_block = get_json_block(raw_json, "server");
  if (!server_block.empty()) {
    config_.server.host = get_json_val(server_block, "host");
    std::string http_port_str = get_json_val(server_block, "http_port");
    if (!http_port_str.empty()) {
      config_.server.http_port = (uint16_t)std::strtoul(http_port_str.c_str(), nullptr, 10);
      config_.server.port = config_.server.http_port;
    }
    std::string tcp_port_str = get_json_val(server_block, "tcp_port");
    if (!tcp_port_str.empty()) {
      config_.server.tcp_port = (uint16_t)std::strtoul(tcp_port_str.c_str(), nullptr, 10);
    }
    std::string port_str = get_json_val(server_block, "port");
    if (!port_str.empty() && http_port_str.empty()) {
      config_.server.port = (uint16_t)std::strtoul(port_str.c_str(), nullptr, 10);
      config_.server.http_port = config_.server.port;
    }
    std::string ep = get_json_val(server_block, "endpoint");
    if (!ep.empty()) config_.server.endpoint = ep;
    std::string token = get_json_val(server_block, "auth_token");
    if (!token.empty()) config_.server.auth_token = token;
  }

  std::string gpio_block = get_json_block(raw_json, "gpio");
  if (!gpio_block.empty()) {
    auto parse_and_validate = [&](const char *key, int8_t &target, bool req_output, int8_t default_safe) {
      std::string val = get_json_val(gpio_block, key);
      if (!val.empty()) {
        int p = std::atoi(val.c_str());
        if (GpioConfig::is_safe_pin((int8_t)p, req_output, key)) {
          target = (int8_t)p;
        } else {
          target = default_safe;
          ESP_LOGW(TAG, "Chan GPIO [%s=%d] khong an toan! Tu dong dung chan mac dinh an toan %d", key, p, default_safe);
        }
      }
    };

    parse_and_validate("nfc_sda", config_.gpio.nfc_sda, true, 21);
    parse_and_validate("nfc_scl", config_.gpio.nfc_scl, true, 22);
    parse_and_validate("nfc_irq", config_.gpio.nfc_irq, false, 16);
    parse_and_validate("buzzer", config_.gpio.buzzer, true, 26);
    parse_and_validate("led_idle", config_.gpio.led_idle, true, 32);
    parse_and_validate("led_success", config_.gpio.led_success, true, 33);
    parse_and_validate("led_error", config_.gpio.led_error, true, 25);
  }

  std::string device_block = get_json_block(raw_json, "device");
  if (!device_block.empty()) {
    std::string dev_id = get_json_val(device_block, "device_id");
    if (!dev_id.empty()) config_.device.device_id = dev_id;
    std::string loc = get_json_val(device_block, "location");
    if (!loc.empty()) config_.device.location = loc;
  }

  std::string scan_block = get_json_block(raw_json, "scan");
  if (!scan_block.empty()) {
    std::string cd = get_json_val(scan_block, "cooldown_ms");
    if (!cd.empty()) config_.scan.cooldown_ms = (uint32_t)std::strtoul(cd.c_str(), nullptr, 10);
    std::string rt = get_json_val(scan_block, "release_timeout_ms");
    if (!rt.empty()) config_.scan.release_timeout_ms = (uint32_t)std::strtoul(rt.c_str(), nullptr, 10);
    std::string fd = get_json_val(scan_block, "feedback_duration_ms");
    if (!fd.empty()) config_.scan.feedback_duration_ms = (uint32_t)std::strtoul(fd.c_str(), nullptr, 10);
  }
}

std::string ConfigLoader::to_json(const AppConfig &cfg) const {
  std::string j = "{\n";
  j += "  \"wifi\": {\n";
  j += "    \"ssid\": \"" + cfg.wifi.ssid + "\",\n";
  j += "    \"password\": \"" + cfg.wifi.password + "\",\n";
  j += "    \"auto_connect\": " + std::string(cfg.wifi.auto_connect ? "true" : "false") + "\n";
  j += "  },\n";
  j += "  \"ap\": {\n";
  j += "    \"ssid\": \"" + cfg.ap.ssid + "\",\n";
  j += "    \"password\": \"" + cfg.ap.password + "\",\n";
  j += "    \"channel\": " + std::to_string(cfg.ap.channel) + ",\n";
  j += "    \"max_connections\": " + std::to_string(cfg.ap.max_connections) + "\n";
  j += "  },\n";
  j += "  \"server\": {\n";
  j += "    \"host\": \"" + cfg.server.host + "\",\n";
  j += "    \"http_port\": " + std::to_string(cfg.server.http_port) + ",\n";
  j += "    \"tcp_port\": " + std::to_string(cfg.server.tcp_port) + ",\n";
  j += "    \"port\": " + std::to_string(cfg.server.port) + ",\n";
  j += "    \"endpoint\": \"" + cfg.server.endpoint + "\",\n";
  j += "    \"auth_token\": \"" + cfg.server.auth_token + "\"\n";
  j += "  },\n";
  j += "  \"gpio\": {\n";
  j += "    \"nfc_sda\": " + std::to_string(cfg.gpio.nfc_sda) + ",\n";
  j += "    \"nfc_scl\": " + std::to_string(cfg.gpio.nfc_scl) + ",\n";
  j += "    \"nfc_irq\": " + std::to_string(cfg.gpio.nfc_irq) + ",\n";
  j += "    \"buzzer\": " + std::to_string(cfg.gpio.buzzer) + ",\n";
  j += "    \"led_idle\": " + std::to_string(cfg.gpio.led_idle) + ",\n";
  j += "    \"led_success\": " + std::to_string(cfg.gpio.led_success) + ",\n";
  j += "    \"led_error\": " + std::to_string(cfg.gpio.led_error) + "\n";
  j += "  },\n";
  j += "  \"device\": {\n";
  j += "    \"device_id\": \"" + cfg.device.device_id + "\",\n";
  j += "    \"location\": \"" + cfg.device.location + "\"\n";
  j += "  },\n";
  j += "  \"scan\": {\n";
  j += "    \"cooldown_ms\": " + std::to_string(cfg.scan.cooldown_ms) + ",\n";
  j += "    \"release_timeout_ms\": " + std::to_string(cfg.scan.release_timeout_ms) + ",\n";
  j += "    \"feedback_duration_ms\": " + std::to_string(cfg.scan.feedback_duration_ms) + "\n";
  j += "  }\n";
  j += "}";
  return j;
}

static uint32_t compute_build_config_hash(const std::string &rom_json) {
  uint32_t hash = 2166136261u;
  for (char c : rom_json) {
    hash ^= (uint8_t)c;
    hash *= 16777619u;
  }
  const char *build_time = __DATE__ " " __TIME__;
  for (size_t i = 0; build_time[i] != '\0'; ++i) {
    hash ^= (uint8_t)build_time[i];
    hash *= 16777619u;
  }
  return hash;
}

esp_err_t ConfigLoader::save_config(const AppConfig &cfg) {
  config_ = cfg;
  std::string json_str = to_json(config_);
  uint32_t current_hash = compute_build_config_hash(get_raw_json());

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
  if (err == ESP_OK) {
    err = nvs_set_str(handle, NVS_CONFIG_KEY, json_str.c_str());
    if (err == ESP_OK) {
      nvs_set_u32(handle, "build_hash", current_hash);
      err = nvs_commit(handle);
      ESP_LOGI(TAG, "Da luu cau hinh vao NVS (SSID: %s)", config_.wifi.ssid.c_str());
    }
    nvs_close(handle);
  } else {
    ESP_LOGE(TAG, "Khong mo duoc NVS de luu cau hinh: %s", esp_err_to_name(err));
  }
  return err;
}

esp_err_t ConfigLoader::save_wifi(const std::string &ssid, const std::string &password) {
  config_.wifi.ssid = ssid;
  config_.wifi.password = password;
  config_.wifi.auto_connect = true;
  return save_config(config_);
}

esp_err_t ConfigLoader::load() {
  std::string rom_json = get_raw_json();
  if (!rom_json.empty() && rom_json != "{}") {
    parse_json(rom_json);
    loaded_ = true;
    ESP_LOGI(TAG, "Nap cau hinh tu ROM: SSID [%s], Server [%s:%d]",
             config_.wifi.ssid.c_str(),
             config_.server.host.c_str(),
             config_.server.port);
  }

  uint32_t current_hash = compute_build_config_hash(rom_json);
  uint32_t saved_hash = 0;

  nvs_handle_t hash_handle;
  bool is_new_flash = false;
  if (nvs_open(NVS_CONFIG_NAMESPACE, NVS_READWRITE, &hash_handle) == ESP_OK) {
    esp_err_t h_err = nvs_get_u32(hash_handle, "build_hash", &saved_hash);
    if (h_err != ESP_OK || saved_hash != current_hash) {
      is_new_flash = true;
      ESP_LOGI(TAG, "Firmware/config.json moi sau khi flash. Tu dong ghi de NVS.");

      nvs_erase_all(hash_handle);
      nvs_set_u32(hash_handle, "build_hash", current_hash);
      nvs_commit(hash_handle);

      nvs_handle_t w_handle;
      if (nvs_open("wifi_config", NVS_READWRITE, &w_handle) == ESP_OK) {
        nvs_erase_all(w_handle);
        nvs_commit(w_handle);
        nvs_close(w_handle);
      }
    }
    nvs_close(hash_handle);
  }

  if (is_new_flash) {
    save_config(config_);
    return ESP_OK;
  }

  nvs_handle_t handle;
  if (nvs_open(NVS_CONFIG_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
    size_t req_size = 0;
    if (nvs_get_str(handle, NVS_CONFIG_KEY, nullptr, &req_size) == ESP_OK && req_size > 2) {
      std::vector<char> buf(req_size);
      if (nvs_get_str(handle, NVS_CONFIG_KEY, buf.data(), &req_size) == ESP_OK) {
        std::string nvs_json(buf.data());
        std::string nvs_ssid = get_json_val(nvs_json, "ssid");
        std::string nvs_pass = get_json_val(nvs_json, "password");
        std::string nvs_host = get_json_val(nvs_json, "host");
        std::string nvs_tcp = get_json_val(nvs_json, "tcp_port");
        std::string nvs_token = get_json_val(nvs_json, "auth_token");

        if (!nvs_ssid.empty()) {
          config_.wifi.ssid = nvs_ssid;
          config_.wifi.password = nvs_pass;
          config_.wifi.auto_connect = true;
          ESP_LOGI(TAG, "Ghi de Wi-Fi tu NVS: SSID [%s]", nvs_ssid.c_str());
        }
        if (!nvs_host.empty() && nvs_host != "192.168.1.100") {
          config_.server.host = nvs_host;
          ESP_LOGI(TAG, "Ghi de Server Host tu NVS Web Portal: [%s]", nvs_host.c_str());
        }
        if (!nvs_tcp.empty()) {
          config_.server.tcp_port = (uint16_t)std::strtoul(nvs_tcp.c_str(), nullptr, 10);
        }
        if (!nvs_token.empty()) {
          config_.server.auth_token = nvs_token;
        }
      }
    }
    nvs_close(handle);
  }

  return ESP_OK;
}
