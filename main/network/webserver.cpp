#include "webserver.h"
#include "wifi_manager.h"
#include "web_portal.h"
#include "logger.h"
#include "config_loader.h"
#include "esp_log.h"
#include "esp_system.h"
#include <string>
#include <sstream>
#include <cstring>

static const char *TAG = "WEB_SERVER";



WebServer::WebServer(WifiManager *wifi_manager)
    : wifi_manager_(wifi_manager), server_handle_(NULL) {}

WebServer::~WebServer() {
  stop();
}

esp_err_t WebServer::root_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Connection", "close");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");

  const char *data = PAGE_PORTAL_HTML;
  size_t total_len = sizeof(PAGE_PORTAL_HTML) - 1;
  size_t offset = 0;
  const size_t CHUNK_SIZE = 1460;

  while (offset < total_len) {
    size_t chunk_len = (total_len - offset < CHUNK_SIZE) ? (total_len - offset) : CHUNK_SIZE;
    esp_err_t ret = httpd_resp_send_chunk(req, data + offset, chunk_len);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Loi truyen chunk HTML toi client: %s", esp_err_to_name(ret));
      return ret;
    }
    offset += chunk_len;
  }
  return httpd_resp_send_chunk(req, NULL, 0);
}

esp_err_t WebServer::captive_portal_redirect_handler(httpd_req_t *req) {
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
  httpd_resp_set_hdr(req, "Connection", "close");
  const char *html = "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"0; url=http://192.168.4.1/\"></head><body><a href=\"http://192.168.4.1/\">Dang chuyen huong toi trang dang nhap...</a></body></html>";
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebServer::api_scan_handler(httpd_req_t *req) {
  WebServer *self = (WebServer *)req->user_ctx;
  if (!self || !self->wifi_manager_) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  std::vector<WifiScanResult> ap_list = self->wifi_manager_->scan_networks();

  std::string json = "[";
  for (size_t i = 0; i < ap_list.size(); ++i) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"";
    for (char c : ap_list[i].ssid) {
      if (c == '"' || c == '\\') json += '\\';
      json += c;
    }
    json += "\",\"rssi\":";
    json += std::to_string(ap_list[i].rssi);
    json += ",\"auth\":";
    json += std::to_string(ap_list[i].auth_mode);
    json += "}";
  }
  json += "]";

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebServer::api_connect_handler(httpd_req_t *req) {
  WebServer *self = (WebServer *)req->user_ctx;
  if (!self || !self->wifi_manager_) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  char buf[512];
  int total_len = req->content_len;
  if (total_len >= (int)sizeof(buf)) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload qua lon");
    return ESP_FAIL;
  }

  int cur_len = 0;
  while (cur_len < total_len) {
    int received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
    if (received <= 0) {
      if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    cur_len += received;
  }
  buf[cur_len] = '\0';

  std::string payload(buf);
  std::string ssid = config_get_json_val(payload, "ssid");
  std::string pass = config_get_json_val(payload, "password");
  std::string server_host = config_get_json_val(payload, "server_host");
  std::string tcp_port_str = config_get_json_val(payload, "tcp_port");
  std::string auth_token = config_get_json_val(payload, "auth_token");
  std::string cooldown_str = config_get_json_val(payload, "scan_cooldown_ms");
  std::string feedback_str = config_get_json_val(payload, "feedback_duration_ms");

  if (ssid.empty()) {
    httpd_resp_set_type(req, "application/json");
    const char *err_resp = "{\"error\":\"SSID khong duoc de trong\"}";
    return httpd_resp_send(req, err_resp, HTTPD_RESP_USE_STRLEN);
  }

  ESP_LOGI(TAG, "Nhan yeu cau ket noi Wi-Fi [%s], Server [%s]",
           ssid.c_str(), server_host.empty() ? "(giu nguyen)" : server_host.c_str());

  auto &cfg = ConfigLoader::instance().get_config();
  cfg.wifi.ssid = ssid;
  cfg.wifi.password = pass;
  cfg.wifi.auto_connect = true;
  if (!server_host.empty()) {
    cfg.server.host = server_host;
  }
  if (!tcp_port_str.empty()) {
    cfg.server.tcp_port = (uint16_t)std::strtoul(tcp_port_str.c_str(), nullptr, 10);
  }
  if (!auth_token.empty()) {
    cfg.server.auth_token = auth_token;
  }
  if (!cooldown_str.empty()) {
    cfg.scan.cooldown_ms = (uint32_t)std::strtoul(cooldown_str.c_str(), nullptr, 10);
  }
  if (!feedback_str.empty()) {
    cfg.scan.feedback_duration_ms = (uint32_t)std::strtoul(feedback_str.c_str(), nullptr, 10);
  }
  ConfigLoader::instance().save_config(cfg);
  WifiManager::save_credentials(ssid, pass);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  const char *ok_resp = "{\"success\":true,\"message\":\"Dang tien hanh ket noi...\"}";
  esp_err_t send_ret = httpd_resp_send(req, ok_resp, HTTPD_RESP_USE_STRLEN);

  struct ConnectParam {
    WifiManager *mgr;
    std::string ssid;
    std::string pass;
  };
  ConnectParam *param = new ConnectParam{self->wifi_manager_, ssid, pass};

  xTaskCreate(
      [](void *pv) {
        ConnectParam *p = (ConnectParam *)pv;
        vTaskDelay(pdMS_TO_TICKS(500));
        bool ok = p->mgr->connect(p->ssid, p->pass, 12000);
        if (ok) {
          ESP_LOGI(TAG, "Ket noi Wi-Fi [%s] thanh cong! Cho 2.5s de trinh duyet nhan IP...", p->ssid.c_str());
          vTaskDelay(pdMS_TO_TICKS(2500));
          p->mgr->stop_ap_portal();
          vTaskDelay(pdMS_TO_TICKS(200));
          ESP_LOGI(TAG, ">>> Chu dong RESTART ESP32: Ap dung cau hinh da luu va tat hoan toan Wi-Fi SoftAP <<<");
          esp_restart();
        } else {
          ESP_LOGW(TAG, "Ket noi Wi-Fi [%s] that bai hoac timeout, giu SoftAP de thu lai.", p->ssid.c_str());
        }
        delete p;
        vTaskDelete(NULL);
      },
      "wifi_conn_task", 4096, param, 5, NULL);

  return send_ret;
}

esp_err_t WebServer::api_status_handler(httpd_req_t *req) {
  WebServer *self = (WebServer *)req->user_ctx;
  if (!self || !self->wifi_manager_) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  bool connected = self->wifi_manager_->is_connected();
  std::string ip = self->wifi_manager_->get_ip();
  std::string ssid = self->wifi_manager_->get_ssid();
  WifiState st = self->wifi_manager_->get_state();

  const char *st_str = "disconnected";
  switch (st) {
    case WifiState::CONNECTING: st_str = "connecting"; break;
    case WifiState::CONNECTED: st_str = "connected"; break;
    case WifiState::FAILED: st_str = "failed"; break;
    case WifiState::AP_PORTAL_ACTIVE: st_str = "portal"; break;
    default: st_str = "disconnected"; break;
  }

  std::string json = "{";
  json += "\"connected\":" + std::string(connected ? "true" : "false") + ",";
  json += "\"status\":\"" + std::string(st_str) + "\",";
  json += "\"ssid\":\"" + ssid + "\",";
  json += "\"ip\":\"" + ip + "\"";
  json += "}";

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebServer::api_reset_handler(httpd_req_t *req) {
  WifiManager::clear_credentials();
  httpd_resp_set_type(req, "application/json");
  const char *resp = "{\"success\":true,\"message\":\"Da xoa cau hinh Wi-Fi\"}";
  return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebServer::api_logs_handler(httpd_req_t *req) {
  std::string json = SystemLogger::instance().get_logs_json(30);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebServer::api_config_handler(httpd_req_t *req) {
  std::string json = ConfigLoader::instance().to_json(ConfigLoader::instance().get_config());
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
}

static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err) {
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
  httpd_resp_set_hdr(req, "Connection", "close");
  const char *resp = "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"0; url=http://192.168.4.1/\"></head><body><a href=\"http://192.168.4.1/\">Redirecting to portal...</a></body></html>";
  return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebServer::start() {
  if (server_handle_ != NULL) {
    ESP_LOGW(TAG, "Web Server da dang chay!");
    return ESP_OK;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 20;
  config.max_open_sockets = 5;
  config.stack_size = 8192;
  config.lru_purge_enable = true;
  config.recv_wait_timeout = 5;
  config.send_wait_timeout = 5;

  ESP_LOGI(TAG, "Khoi dong web server toi uu (max_sockets=5, timeout=5s) cong %d", config.server_port);

  esp_err_t ret = httpd_start(&server_handle_, &config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Khoi dong web server that bai: %s", esp_err_to_name(ret));
    return ret;
  }

  const httpd_uri_t routes[] = {
      {"/", HTTP_GET, root_get_handler, this},
      {"/api/scan", HTTP_GET, api_scan_handler, this},
      {"/api/connect", HTTP_POST, api_connect_handler, this},
      {"/api/status", HTTP_GET, api_status_handler, this},
      {"/api/reset", HTTP_POST, api_reset_handler, this},
      {"/api/logs", HTTP_GET, api_logs_handler, this},
      {"/api/config", HTTP_GET, api_config_handler, this},
  };
  for (const auto &r : routes) {
    httpd_register_uri_handler(server_handle_, &r);
  }

  const char *captive_uris[] = {
      "/hotspot-detect.html", "/generate_204", "/gen_204",
      "/ncsi.txt", "/connecttest.txt", "/canonical.html", "/connectivity-check.html"
  };
  for (const char *uri : captive_uris) {
    httpd_uri_t cap_uri = {uri, HTTP_GET, captive_portal_redirect_handler, this};
    httpd_register_uri_handler(server_handle_, &cap_uri);
  }

  httpd_register_err_handler(server_handle_, HTTPD_404_NOT_FOUND, http_404_error_handler);

  ESP_LOGI(TAG, "Web server san sang.");
  return ESP_OK;
}

void WebServer::stop() {
  if (server_handle_ != NULL) {
    httpd_stop(server_handle_);
    server_handle_ = NULL;
    ESP_LOGI(TAG, "Web Server da dung.");
  }
}
