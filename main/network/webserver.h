#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

class WifiManager;

class WebServer {
public:
  WebServer(WifiManager *wifi_manager);
  ~WebServer();

  esp_err_t start();
  void stop();
  bool is_running() const { return server_handle_ != NULL; }

private:
  WifiManager *wifi_manager_;
  httpd_handle_t server_handle_;

  static esp_err_t root_get_handler(httpd_req_t *req);
  static esp_err_t captive_portal_redirect_handler(httpd_req_t *req);
  static esp_err_t api_scan_handler(httpd_req_t *req);
  static esp_err_t api_connect_handler(httpd_req_t *req);
  static esp_err_t api_status_handler(httpd_req_t *req);
  static esp_err_t api_reset_handler(httpd_req_t *req);
  static esp_err_t api_logs_handler(httpd_req_t *req);
  static esp_err_t api_config_handler(httpd_req_t *req);
};
