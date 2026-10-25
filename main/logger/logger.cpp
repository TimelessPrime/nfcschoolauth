#include "logger.h"
#include "tcp_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>

static const char *TAG = "LOGGER";

SystemLogger &SystemLogger::instance() {
  static SystemLogger instance;
  return instance;
}

SystemLogger::SystemLogger()
    : max_entries_(60),
      mutex_(NULL),
      total_scans_(0),
      successful_scans_(0),
      initialized_(false) {
  mutex_ = xSemaphoreCreateMutex();
}

SystemLogger::~SystemLogger() {
  if (mutex_) {
    vSemaphoreDelete(mutex_);
    mutex_ = NULL;
  }
}

esp_err_t SystemLogger::init(size_t max_entries) {
  if (mutex_) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    max_entries_ = max_entries;
    entries_.reserve(max_entries_);
    initialized_ = true;
    xSemaphoreGive(mutex_);
  }
  ESP_LOGI(TAG, "Logger khoi tao (%d ban ghi)", (int)max_entries);
  return ESP_OK;
}

void SystemLogger::log(LogType type, const std::string &tag, const std::string &message) {
  if (!mutex_) return;

  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
  LogEntry entry;
  entry.timestamp_ms = now_ms;
  entry.type = type;
  entry.tag = tag;
  entry.uid = "";
  entry.message = message;
  entry.success = (type != LogType::ERROR);

  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (entries_.size() >= max_entries_) {
      entries_.erase(entries_.begin());
    }
    entries_.push_back(entry);
    xSemaphoreGive(mutex_);
  }

  const char *level_str = "INFO";
  switch (type) {
    case LogType::ERROR:
      level_str = "ERROR";
      ESP_LOGE(tag.c_str(), "%s", message.c_str());
      break;
    case LogType::WIFI:
    case LogType::SYSTEM:
    default:
      ESP_LOGI(tag.c_str(), "%s", message.c_str());
      break;
  }

  TcpClient::instance().queue_log(level_str, tag.c_str(), message.c_str());
}

void SystemLogger::log_attendance(const std::string &uid, bool success, const std::string &message) {
  if (!mutex_) return;

  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
  LogEntry entry;
  entry.timestamp_ms = now_ms;
  entry.type = LogType::NFC_SCAN;
  entry.tag = "NFC_ATTENDANCE";
  entry.uid = uid;
  entry.message = message.empty() ? (success ? "Diem danh thanh cong" : "The khong hop le / loi") : message;
  entry.success = success;

  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
    total_scans_++;
    if (success) {
      successful_scans_++;
    }

    if (entries_.size() >= max_entries_) {
      entries_.erase(entries_.begin());
    }
    entries_.push_back(entry);
    xSemaphoreGive(mutex_);
  }

  if (success) {
    ESP_LOGI(TAG, "The hop le: UID [%s]", uid.c_str());
  } else {
    ESP_LOGW(TAG, "The khong hop le: UID [%s]", uid.c_str());
  }

  std::string att_detail = "UID [" + uid + "]: " + entry.message;
  TcpClient::instance().queue_log(success ? "INFO" : "WARN", "ATTENDANCE", att_detail.c_str());
}

std::vector<LogEntry> SystemLogger::get_logs() {
  std::vector<LogEntry> copy;
  if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(200)) == pdTRUE) {
    copy = entries_;
    xSemaphoreGive(mutex_);
  }
  return copy;
}

std::string SystemLogger::get_logs_json(size_t limit) {
  std::string json = "{\"total_scans\":";
  json += std::to_string(total_scans_);
  json += ",\"success_scans\":";
  json += std::to_string(successful_scans_);
  json += ",\"logs\":[";

  if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(200)) == pdTRUE) {
    int start = 0;
    int count = (int)entries_.size();
    if ((size_t)count > limit) {
      start = count - (int)limit;
    }

    bool first = true;
    for (int i = count - 1; i >= start; i--) {
      if (!first) json += ",";
      first = false;

      const auto &e = entries_[i];
      const char *t_str = "system";
      if (e.type == LogType::NFC_SCAN) t_str = "nfc";
      else if (e.type == LogType::WIFI) t_str = "wifi";
      else if (e.type == LogType::ERROR) t_str = "error";

      json += "{\"time_ms\":";
      json += std::to_string(e.timestamp_ms);
      json += ",\"type\":\"";
      json += t_str;
      json += "\",\"tag\":\"";
      json += e.tag;
      json += "\",\"uid\":\"";
      json += e.uid;
      json += "\",\"success\":";
      json += (e.success ? "true" : "false");
      json += ",\"msg\":\"";
      for (char c : e.message) {
        if (c == '"' || c == '\\') json += '\\';
        json += c;
      }
      json += "\"}";
    }

    xSemaphoreGive(mutex_);
  }

  json += "]}";
  return json;
}

void SystemLogger::clear() {
  if (mutex_ && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
    entries_.clear();
    xSemaphoreGive(mutex_);
  }
  ESP_LOGI(TAG, "Da xoa nhat ky.");
}
