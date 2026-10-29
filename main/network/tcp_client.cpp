#include "tcp_client.h"
#include "config_loader.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "mbedtls/build_info.h"
#include "mbedtls/md.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "wifi_manager.h"
#include <cstring>

static const char *TAG = "TCP_CLIENT";
static constexpr size_t LOG_QUEUE_SIZE = 30;
static constexpr const char *NVS_OFFLINE_NAMESPACE = "off_scans";
static constexpr const char *NVS_OFFLINE_KEY = "items";
static constexpr size_t MAX_OFFLINE_NVS_ITEMS = 50;

static void nvs_save_offline_item(const OfflineScanItem &item) {
  nvs_handle_t handle;
  if (nvs_open(NVS_OFFLINE_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;

  OfflineScanItem items[MAX_OFFLINE_NVS_ITEMS];
  size_t required_size = sizeof(items);
  size_t count = 0;

  if (nvs_get_blob(handle, NVS_OFFLINE_KEY, items, &required_size) == ESP_OK) {
    count = required_size / sizeof(OfflineScanItem);
  }

  if (count < MAX_OFFLINE_NVS_ITEMS) {
    items[count] = item;
    count++;
    nvs_set_blob(handle, NVS_OFFLINE_KEY, items, count * sizeof(OfflineScanItem));
    nvs_commit(handle);
  }
  nvs_close(handle);
}

static void nvs_load_offline_items(QueueHandle_t queue) {
  if (!queue) return;
  nvs_handle_t handle;
  if (nvs_open(NVS_OFFLINE_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;

  OfflineScanItem items[MAX_OFFLINE_NVS_ITEMS];
  size_t required_size = sizeof(items);

  if (nvs_get_blob(handle, NVS_OFFLINE_KEY, items, &required_size) == ESP_OK) {
    size_t count = required_size / sizeof(OfflineScanItem);
    for (size_t i = 0; i < count; ++i) {
      xQueueSend(queue, &items[i], 0);
    }
    if (count > 0) {
      ESP_LOGI(TAG, "Da phuc hoi %d ban ghi offline tu Flash NVS vao hang doi", (int)count);
    }
  }
  nvs_close(handle);
}

static void nvs_clear_offline_items() {
  nvs_handle_t handle;
  if (nvs_open(NVS_OFFLINE_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
    nvs_erase_key(handle, NVS_OFFLINE_KEY);
    nvs_commit(handle);
    nvs_close(handle);
  }
}

static std::string compute_hmac_sha256(const std::string &key, const std::string &message) {
  const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!md_info) {
    return "";
  }
  unsigned char hmac_out[32];
  int ret = mbedtls_md_hmac(
      md_info,
      (const unsigned char *)key.data(), key.size(),
      (const unsigned char *)message.data(), message.size(),
      hmac_out
  );
  if (ret != 0) return "";

  char hex[65];
  for (int i = 0; i < 32; i++) {
    sprintf(hex + i * 2, "%02x", hmac_out[i]);
  }
  hex[64] = '\0';
  return std::string(hex);
}

static std::string extract_json_field(const std::string &json, const std::string &key) {
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

TcpClient &TcpClient::instance() {
  static TcpClient instance;
  return instance;
}

TcpClient::TcpClient()
    : socket_fd_(-1),
      running_(false),
      connected_(false),
      authenticated_(false),
      log_queue_(nullptr),
      offline_queue_(nullptr),
      socket_mutex_(nullptr),
      task_handle_(nullptr) {
  log_queue_ = xQueueCreate(LOG_QUEUE_SIZE, sizeof(StreamLogMsg));
  offline_queue_ = xQueueCreate(100, sizeof(OfflineScanItem));
  socket_mutex_ = xSemaphoreCreateMutex();
}

TcpClient::~TcpClient() {
  stop();
  if (log_queue_) {
    vQueueDelete(log_queue_);
    log_queue_ = nullptr;
  }
  if (offline_queue_) {
    vQueueDelete(offline_queue_);
    offline_queue_ = nullptr;
  }
  if (socket_mutex_) {
    vSemaphoreDelete(socket_mutex_);
    socket_mutex_ = nullptr;
  }
}

esp_err_t TcpClient::start() {
  if (running_) return ESP_OK;
  running_ = true;

  nvs_load_offline_items(offline_queue_);

  BaseType_t ret = xTaskCreate(task_entry, "tcp_stream_task", 4096, this, 4, &task_handle_);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Loi khoi tao task TCP Client");
    running_ = false;
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "Khoi dong TCP Client stream log...");
  return ESP_OK;
}

void TcpClient::stop() {
  running_ = false;
  disconnect_socket();
  if (task_handle_) {
    vTaskDelete(task_handle_);
    task_handle_ = nullptr;
  }
}

bool TcpClient::queue_log(const char *level, const char *tag, const char *message) {
  if (!log_queue_ || !running_) return false;

  StreamLogMsg item;
  strncpy(item.level, level ? level : "INFO", sizeof(item.level) - 1);
  item.level[sizeof(item.level) - 1] = '\0';

  strncpy(item.tag, tag ? tag : "SYS", sizeof(item.tag) - 1);
  item.tag[sizeof(item.tag) - 1] = '\0';

  strncpy(item.message, message ? message : "", sizeof(item.message) - 1);
  item.message[sizeof(item.message) - 1] = '\0';

  item.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

  if (xQueueSend(log_queue_, &item, 0) != pdTRUE) {
    StreamLogMsg dummy;
    xQueueReceive(log_queue_, &dummy, 0);
    xQueueSend(log_queue_, &item, 0);
  }
  return true;
}

bool TcpClient::queue_offline_scan(const std::string &uid) {
  if (!offline_queue_) return false;
  OfflineScanItem item;
  strncpy(item.uid, uid.c_str(), sizeof(item.uid) - 1);
  item.uid[sizeof(item.uid) - 1] = '\0';
  item.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

  if (xQueueSend(offline_queue_, &item, 0) != pdTRUE) {
    OfflineScanItem dummy;
    xQueueReceive(offline_queue_, &dummy, 0);
    xQueueSend(offline_queue_, &item, 0);
  }
  nvs_save_offline_item(item);
  ESP_LOGI(TAG, "Da luu quet the vao bo dem tam offline: UID [%s] (tong cho dong bo: %d)",
           item.uid, (int)uxQueueMessagesWaiting(offline_queue_));
  return true;
}

size_t TcpClient::get_pending_offline_count() const {
  return offline_queue_ ? uxQueueMessagesWaiting(offline_queue_) : 0;
}

void TcpClient::task_entry(void *arg) {
  static_cast<TcpClient *>(arg)->run();
}

bool TcpClient::connect_to_backend() {
  const auto &cfg = ConfigLoader::instance().get_config();
  if (cfg.server.host.empty()) {
    return false;
  }

  struct sockaddr_in dest_addr;
  dest_addr.sin_addr.s_addr = inet_addr(cfg.server.host.c_str());
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(cfg.server.tcp_port);

  if (dest_addr.sin_addr.s_addr == INADDR_NONE) {
    ESP_LOGI(TAG, "Dang phan giai Hostname [%s] qua DNS...", cfg.server.host.c_str());
    struct hostent *he = gethostbyname(cfg.server.host.c_str());
    if (he != nullptr && he->h_addr_list[0] != nullptr) {
      dest_addr.sin_addr.s_addr = *(reinterpret_cast<in_addr_t *>(he->h_addr_list[0]));
      char resolved_ip[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &dest_addr.sin_addr, resolved_ip, sizeof(resolved_ip));
      ESP_LOGI(TAG, "Phan giai DNS thanh cong: %s -> %s", cfg.server.host.c_str(), resolved_ip);
    } else {
      ESP_LOGW(TAG, "Khong the phan giai Hostname [%s]. Vui long dat dia chi IP ro rang trong config.", cfg.server.host.c_str());
      return false;
    }
  }

  int s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (s < 0) {
    return false;
  }

  struct timeval tv;
  tv.tv_sec = 3;
  tv.tv_usec = 0;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  int keepalive = 1;
  int keepidle = 10;
  int keepinterval = 5;
  int keepcount = 3;
  setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
  setsockopt(s, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
  setsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, &keepinterval, sizeof(keepinterval));
  setsockopt(s, IPPROTO_TCP, TCP_KEEPCNT, &keepcount, sizeof(keepcount));

  int err = connect(s, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
  if (err != 0) {
    close(s);
    return false;
  }

  socket_fd_ = s;
  connected_ = true;
  ESP_LOGI(TAG, "Da ket noi TCP toi Backend [%s:%d]", cfg.server.host.c_str(), cfg.server.tcp_port);
  return true;
}

bool TcpClient::perform_handshake() {
  const auto &cfg = ConfigLoader::instance().get_config();

  std::string client_nonce = "esp32_" + std::to_string(esp_timer_get_time());
  std::string step1_req = "{\"action\":\"handshake_step1\",\"device_id\":\"";
  step1_req += cfg.device.device_id;
  step1_req += "\",\"client_nonce\":\"";
  step1_req += client_nonce;
  step1_req += "\"}\n";

  if (send(socket_fd_, step1_req.c_str(), step1_req.length(), 0) > 0) {
    char rx_buf[1024];
    int len = recv(socket_fd_, rx_buf, sizeof(rx_buf) - 1, 0);
    if (len > 0) {
      rx_buf[len] = '\0';
      std::string resp(rx_buf);

      if (resp.find("\"handshake_step2\"") != std::string::npos) {
        std::string server_nonce = extract_json_field(resp, "server_nonce");
        std::string server_proof = extract_json_field(resp, "server_proof");
        std::string server_ts = extract_json_field(resp, "timestamp");

        std::string expected_server_proof = compute_hmac_sha256(
            cfg.server.auth_token,
            "STEP2:" + cfg.device.device_id + ":" + client_nonce + ":" + server_nonce + ":" + server_ts
        );

        if (server_proof != expected_server_proof) {
          ESP_LOGW(TAG, "Canh bao: Server proof khong hop le! Nghi ngo gia mao backend.");
          return false;
        }

        std::string client_proof = compute_hmac_sha256(
            cfg.server.auth_token,
            "STEP3:" + cfg.device.device_id + ":" + server_nonce + ":" + client_nonce
        );

        std::string step3_req = "{\"action\":\"handshake_step3\",\"device_id\":\"";
        step3_req += cfg.device.device_id;
        step3_req += "\",\"client_proof\":\"";
        step3_req += client_proof;
        step3_req += "\"}\n";

        if (send(socket_fd_, step3_req.c_str(), step3_req.length(), 0) > 0) {
          len = recv(socket_fd_, rx_buf, sizeof(rx_buf) - 1, 0);
          if (len > 0) {
            rx_buf[len] = '\0';
            std::string step3_ack(rx_buf);
            if (step3_ack.find("\"AUTHENTICATED\"") != std::string::npos) {
              session_token_ = extract_json_field(step3_ack, "session_token");
              authenticated_ = true;
              ESP_LOGI(TAG, "3-Step Mutual Handshake (TLS/HMAC) thanh cong! Token: %s", session_token_.c_str());
              return true;
            }
          }
        }
        ESP_LOGW(TAG, "3-Step Handshake step 3 that bai");
        return false;
      }
    }
  }

  ESP_LOGI(TAG, "Thu lai bang 1-step handshake truyen thong...");
  std::string req = "{\"action\":\"handshake\",\"device_id\":\"";
  req += cfg.device.device_id;
  req += "\",\"auth_token\":\"";
  req += cfg.server.auth_token;
  req += "\",\"nonce\":\"esp32_";
  req += std::to_string(esp_timer_get_time());
  req += "\"}\n";

  if (send(socket_fd_, req.c_str(), req.length(), 0) < 0) {
    return false;
  }

  char rx_buf[512];
  int len = recv(socket_fd_, rx_buf, sizeof(rx_buf) - 1, 0);
  if (len <= 0) {
    return false;
  }
  rx_buf[len] = '\0';

  if (strstr(rx_buf, "\"AUTHENTICATED\"") != nullptr) {
    authenticated_ = true;
    ESP_LOGI(TAG, "Xac thuc bat tay TCP 1-step voi backend thanh cong!");
    return true;
  }

  ESP_LOGW(TAG, "Xac thuc bat tay TCP that bai: %s", rx_buf);
  return false;
}

void TcpClient::disconnect_socket() {
  if (socket_mutex_) xSemaphoreTake(socket_mutex_, portMAX_DELAY);
  if (socket_fd_ >= 0) {
    shutdown(socket_fd_, 0);
    close(socket_fd_);
    socket_fd_ = -1;
  }
  connected_ = false;
  authenticated_ = false;
  rx_cmd_buffer_.clear();
  if (socket_mutex_) xSemaphoreGive(socket_mutex_);
}

void TcpClient::handle_server_command(const std::string &cmd_json) {
  std::string action = extract_json_field(cmd_json, "action");
  if (action.empty()) action = extract_json_field(cmd_json, "cmd");

  if (action == "pong" || action == "ping_ack" || action == "heartbeat_ack") {
    return;
  }

  const auto &current_cfg = ConfigLoader::instance().get_config();
  std::string target_dev = extract_json_field(cmd_json, "device_id");

  if (!target_dev.empty() && target_dev != "all" && target_dev != current_cfg.device.device_id) {
    ESP_LOGD(TAG, "Bo qua lenh cho thiet bi khac: [%s] (hien tai la [%s])",
             target_dev.c_str(), current_cfg.device.device_id.c_str());
    return;
  }

  if (action == "update_config" || action == "set_config") {
    ESP_LOGI(TAG, "Nhan lenh cap nhat cau hinh tu Server/Discord!");
    auto &cfg = ConfigLoader::instance().get_config();
    bool has_changes = false;

    std::string cooldown_str = extract_json_field(cmd_json, "cooldown_ms");
    if (!cooldown_str.empty()) {
      cfg.scan.cooldown_ms = (uint32_t)std::strtoul(cooldown_str.c_str(), nullptr, 10);
      has_changes = true;
    }

    std::string feedback_str = extract_json_field(cmd_json, "feedback_duration_ms");
    if (!feedback_str.empty()) {
      cfg.scan.feedback_duration_ms = (uint32_t)std::strtoul(feedback_str.c_str(), nullptr, 10);
      has_changes = true;
    }

    std::string release_str = extract_json_field(cmd_json, "release_timeout_ms");
    if (!release_str.empty()) {
      cfg.scan.release_timeout_ms = (uint32_t)std::strtoul(release_str.c_str(), nullptr, 10);
      has_changes = true;
    }

    std::string loc_str = extract_json_field(cmd_json, "location");
    if (!loc_str.empty()) {
      cfg.device.location = loc_str;
      has_changes = true;
    }

    std::string new_dev_id = extract_json_field(cmd_json, "new_device_id");
    if (!new_dev_id.empty()) {
      cfg.device.device_id = new_dev_id;
      has_changes = true;
    }

    std::string wifi_ssid = extract_json_field(cmd_json, "wifi_ssid");
    std::string wifi_pass = extract_json_field(cmd_json, "wifi_password");
    if (!wifi_ssid.empty()) {
      cfg.wifi.ssid = wifi_ssid;
      cfg.wifi.password = wifi_pass;
      cfg.wifi.auto_connect = true;
      has_changes = true;
      WifiManager::save_credentials(wifi_ssid, wifi_pass);
    }

    std::string srv_host = extract_json_field(cmd_json, "server_host");
    if (!srv_host.empty()) {
      cfg.server.host = srv_host;
      has_changes = true;
    }

    std::string srv_port = extract_json_field(cmd_json, "tcp_port");
    if (!srv_port.empty()) {
      cfg.server.tcp_port = (uint16_t)std::strtoul(srv_port.c_str(), nullptr, 10);
      has_changes = true;
    }

    if (has_changes) {
      ConfigLoader::instance().save_config(cfg);
      ESP_LOGI(TAG, "Da ghi cau hinh moi vao NVS Flash: Cooldown=%lu ms, Vi tri=%s",
               (unsigned long)cfg.scan.cooldown_ms, cfg.device.location.c_str());
    }

    std::string ack = "{\"action\":\"config_ack\",\"device_id\":\"";
    ack += cfg.device.device_id;
    ack += "\",\"status\":\"SUCCESS\",\"message\":\"Da cap nhat cau hinh thanh cong\"";
    ack += ",\"cooldown_ms\":";
    ack += std::to_string(cfg.scan.cooldown_ms);
    ack += ",\"feedback_duration_ms\":";
    ack += std::to_string(cfg.scan.feedback_duration_ms);
    ack += ",\"location\":\"";
    ack += cfg.device.location;
    ack += "\"}\n";

    if (socket_mutex_ && xSemaphoreTake(socket_mutex_, pdMS_TO_TICKS(500)) == pdTRUE) {
      send(socket_fd_, ack.c_str(), ack.length(), 0);
      xSemaphoreGive(socket_mutex_);
    }

    std::string reboot_str = extract_json_field(cmd_json, "reboot");
    if (reboot_str == "true" || reboot_str == "1") {
      ESP_LOGW(TAG, "Yeu cau reboot sau khi doi cau hinh. Khoi dong lai sau 1 giay...");
      vTaskDelay(pdMS_TO_TICKS(1000));
      esp_restart();
    }
    return;
  }

  if (action == "reboot") {
    ESP_LOGW(TAG, "Nhan lenh REBOOT tu Server/Discord!");
    std::string ack = "{\"action\":\"reboot_ack\",\"device_id\":\"";
    ack += current_cfg.device.device_id;
    ack += "\",\"status\":\"SUCCESS\",\"message\":\"Dau doc dang khoi dong lai...\"}\n";

    if (socket_mutex_ && xSemaphoreTake(socket_mutex_, pdMS_TO_TICKS(500)) == pdTRUE) {
      send(socket_fd_, ack.c_str(), ack.length(), 0);
      xSemaphoreGive(socket_mutex_);
    }

    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return;
  }

  if (action == "get_config") {
    std::string resp = "{\"action\":\"config_info\",\"device_id\":\"";
    resp += current_cfg.device.device_id;
    resp += "\",\"location\":\"";
    resp += current_cfg.device.location;
    resp += "\",\"cooldown_ms\":";
    resp += std::to_string(current_cfg.scan.cooldown_ms);
    resp += ",\"feedback_duration_ms\":";
    resp += std::to_string(current_cfg.scan.feedback_duration_ms);
    resp += ",\"release_timeout_ms\":";
    resp += std::to_string(current_cfg.scan.release_timeout_ms);
    resp += ",\"wifi_ssid\":\"";
    resp += current_cfg.wifi.ssid;
    resp += "\",\"server_host\":\"";
    resp += current_cfg.server.host;
    resp += "\",\"tcp_port\":";
    resp += std::to_string(current_cfg.server.tcp_port);
    resp += "}\n";

    if (socket_mutex_ && xSemaphoreTake(socket_mutex_, pdMS_TO_TICKS(500)) == pdTRUE) {
      send(socket_fd_, resp.c_str(), resp.length(), 0);
      xSemaphoreGive(socket_mutex_);
    }
    return;
  }
}

void TcpClient::run() {
  uint32_t last_heartbeat_ms = 0;

  while (running_) {
    if (!connected_ || socket_fd_ < 0) {
      esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
      esp_netif_ip_info_t ip_info;
      if (!netif || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK || ip_info.ip.addr == 0) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }

      if (!connect_to_backend() || !perform_handshake()) {
        disconnect_socket();
        vTaskDelay(pdMS_TO_TICKS(3000));
        continue;
      }
      last_heartbeat_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    }

    OfflineScanItem off_item;
    while (connected_ && authenticated_ && xQueueReceive(offline_queue_, &off_item, 0) == pdTRUE) {
      const auto &cfg = ConfigLoader::instance().get_config();
      uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
      uint32_t elapsed_ms = (now_ms >= off_item.timestamp_ms) ? (now_ms - off_item.timestamp_ms) : 0;

      std::string json = "{\"action\":\"offline_scan\",\"uid\":\"";
      json += off_item.uid;
      json += "\",\"device_id\":\"";
      json += cfg.device.device_id;
      json += "\",\"elapsed_ms\":";
      json += std::to_string(elapsed_ms);
      json += "}\n";

      if (socket_mutex_ && xSemaphoreTake(socket_mutex_, pdMS_TO_TICKS(500)) == pdTRUE) {
        int res = send(socket_fd_, json.c_str(), json.length(), 0);
        xSemaphoreGive(socket_mutex_);
        if (res < 0) {
          ESP_LOGW(TAG, "Loi khi dong bo offline scan, dat lai vao hang doi");
          xQueueSendToFront(offline_queue_, &off_item, 0);
          disconnect_socket();
          break;
        }
        ESP_LOGI(TAG, "Da dong bo thanh cong ban ghi offline: UID [%s] (quet cach day %u ms)", off_item.uid, (unsigned)elapsed_ms);
        if (uxQueueMessagesWaiting(offline_queue_) == 0) {
          nvs_clear_offline_items();
        }
      }
      vTaskDelay(pdMS_TO_TICKS(20));
    }

    StreamLogMsg item;
    while (xQueueReceive(log_queue_, &item, pdMS_TO_TICKS(100)) == pdTRUE) {
      const auto &cfg = ConfigLoader::instance().get_config();
      std::string json = "{\"action\":\"client_log\",\"device_id\":\"";
      json += cfg.device.device_id;
      json += "\",\"level\":\"";
      json += item.level;
      json += "\",\"tag\":\"";
      json += item.tag;
      json += "\",\"message\":\"";
      for (int i = 0; item.message[i] != '\0'; i++) {
        char c = item.message[i];
        if (c == '"' || c == '\\') json += '\\';
        json += c;
      }
      json += "\",\"time_ms\":";
      json += std::to_string(item.timestamp_ms);
      json += "}\n";

      if (socket_mutex_ && xSemaphoreTake(socket_mutex_, pdMS_TO_TICKS(200)) == pdTRUE) {
        int res = send(socket_fd_, json.c_str(), json.length(), 0);
        xSemaphoreGive(socket_mutex_);
        if (res < 0) {
          ESP_LOGW(TAG, "Mat ket noi khi stream log toi backend");
          disconnect_socket();
          break;
        }
      }
    }

    // Heartbeat Ping định kỳ mỗi 10 giây kèm dữ liệu tình trạng phần cứng (Telemetry)
    uint32_t current_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if (connected_ && authenticated_ && (current_ms - last_heartbeat_ms >= 10000)) {
      last_heartbeat_ms = current_ms;
      const auto &cfg = ConfigLoader::instance().get_config();
      int rssi = -100;
      wifi_ap_record_t ap_info;
      if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
      }
      uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
      uint32_t free_heap = esp_get_free_heap_size();
      size_t pending_off = uxQueueMessagesWaiting(offline_queue_);

      std::string ping_json = "{\"action\":\"ping\",\"device_id\":\"";
      ping_json += cfg.device.device_id;
      ping_json += "\",\"uptime_s\":";
      ping_json += std::to_string(uptime_s);
      ping_json += ",\"free_heap\":";
      ping_json += std::to_string(free_heap);
      ping_json += ",\"rssi\":";
      ping_json += std::to_string(rssi);
      ping_json += ",\"pending_offline\":";
      ping_json += std::to_string(pending_off);
      ping_json += "}\n";

      if (socket_mutex_ && xSemaphoreTake(socket_mutex_, pdMS_TO_TICKS(200)) == pdTRUE) {
        int res = send(socket_fd_, ping_json.c_str(), ping_json.length(), 0);
        xSemaphoreGive(socket_mutex_);
        if (res < 0) {
          ESP_LOGW(TAG, "Loi khi gui Heartbeat Ping, ngat socket de ket noi lai");
          disconnect_socket();
          continue;
        }
      }
    }

    // Đọc dữ liệu phản hồi (Pong / ACK / Lệnh từ Server hoặc Bot Discord) không chặn
    if (connected_ && socket_fd_ >= 0) {
      char drain_buf[512];
      int r = recv(socket_fd_, drain_buf, sizeof(drain_buf) - 1, MSG_DONTWAIT);
      if (r == 0) {
        ESP_LOGW(TAG, "Server da chu dong ngat ket noi TCP");
        disconnect_socket();
        continue;
      } else if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        ESP_LOGW(TAG, "Socket error tren recv: %d", errno);
        disconnect_socket();
        continue;
      } else if (r > 0) {
        drain_buf[r] = '\0';
        rx_cmd_buffer_ += drain_buf;
        size_t newline_pos;
        while ((newline_pos = rx_cmd_buffer_.find('\n')) != std::string::npos) {
          std::string line = rx_cmd_buffer_.substr(0, newline_pos);
          rx_cmd_buffer_.erase(0, newline_pos + 1);
          while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
          if (!line.empty()) {
            handle_server_command(line);
          }
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

bool TcpClient::send_scan_blocking(const std::string &uid, std::string &response_out, uint32_t timeout_ms) {
  if (!connected_ || socket_fd_ < 0 || !authenticated_) {
    return false;
  }

  const auto &cfg = ConfigLoader::instance().get_config();
  std::string json = "{\"action\":\"scan\",\"uid\":\"";
  json += uid;
  json += "\",\"device_id\":\"";
  json += cfg.device.device_id;
  json += "\"}\n";

  if (!socket_mutex_ || xSemaphoreTake(socket_mutex_, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
    return false;
  }

  int res = send(socket_fd_, json.c_str(), json.length(), 0);
  if (res < 0) {
    xSemaphoreGive(socket_mutex_);
    disconnect_socket();
    return false;
  }

  char rx_buf[512];
  int len = recv(socket_fd_, rx_buf, sizeof(rx_buf) - 1, 0);
  xSemaphoreGive(socket_mutex_);

  if (len > 0) {
    rx_buf[len] = '\0';
    response_out = std::string(rx_buf);
    return true;
  }

  return false;
}
