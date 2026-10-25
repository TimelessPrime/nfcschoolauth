#pragma once

#include <string>
#include <cstdint>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

struct StreamLogMsg {
  char level[8];
  char tag[24];
  char message[192];
  uint32_t timestamp_ms;
};

struct OfflineScanItem {
  char uid[32];
  uint32_t timestamp_ms;
};

class TcpClient {
public:
  static TcpClient &instance();

  esp_err_t start();
  void stop();
  bool is_connected() const { return connected_; }

  bool queue_log(const char *level, const char *tag, const char *message);
  bool queue_offline_scan(const std::string &uid);
  size_t get_pending_offline_count() const;
  bool send_scan_blocking(const std::string &uid, std::string &response_out, uint32_t timeout_ms = 2000);

private:
  TcpClient();
  ~TcpClient();
  TcpClient(const TcpClient &) = delete;
  TcpClient &operator=(const TcpClient &) = delete;

  static void task_entry(void *arg);
  void run();

  bool connect_to_backend();
  bool perform_handshake();
  void disconnect_socket();

  int socket_fd_;
  bool running_;
  bool connected_;
  bool authenticated_;
  std::string session_token_;

  QueueHandle_t log_queue_;
  QueueHandle_t offline_queue_;
  SemaphoreHandle_t socket_mutex_;
  TaskHandle_t task_handle_;
};
