#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

enum class LogType {
  SYSTEM,
  NFC_SCAN,
  WIFI,
  ERROR
};

struct LogEntry {
  uint32_t timestamp_ms;
  LogType type;
  std::string tag;
  std::string uid;
  std::string message;
  bool success;
};

class SystemLogger {
public:
  static SystemLogger &instance();

  esp_err_t init(size_t max_entries = 60);

  void log(LogType type, const std::string &tag, const std::string &message);
  void log_attendance(const std::string &uid, bool success, const std::string &message = "");

  std::vector<LogEntry> get_logs();
  std::string get_logs_json(size_t limit = 30);
  void clear();

  uint32_t get_total_scans() const { return total_scans_; }
  uint32_t get_successful_scans() const { return successful_scans_; }

private:
  SystemLogger();
  ~SystemLogger();
  SystemLogger(const SystemLogger &) = delete;
  SystemLogger &operator=(const SystemLogger &) = delete;

  std::vector<LogEntry> entries_;
  size_t max_entries_;
  SemaphoreHandle_t mutex_;
  uint32_t total_scans_;
  uint32_t successful_scans_;
  bool initialized_;
};
