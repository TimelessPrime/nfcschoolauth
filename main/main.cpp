#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "led.h"
#include "speaker.h"
#include "nfc.h"
#include "wifi_manager.h"
#include "logger.h"
#include "tcp_client.h"
#include "config_loader.h"

static const char *TAG = "APP_MAIN";

extern "C" void app_main(void) {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  ESP_LOGI(TAG, "NFC School Auth khoi dong (Brownout Detector disabled)");

  SystemLogger::instance().init(60);
  SystemLogger::instance().log(LogType::SYSTEM, TAG, "He thong bat dau khoi dong.");

  ConfigLoader::instance().load();
  const auto &cfg = ConfigLoader::instance().get_config();

  LedController led((gpio_num_t)cfg.gpio.led_idle,
                    (gpio_num_t)cfg.gpio.led_success,
                    (gpio_num_t)cfg.gpio.led_error);
  if (led.init() != ESP_OK) {
    ESP_LOGE(TAG, "Loi khoi tao LED");
    return;
  }

  BuzzerController buzzer((gpio_num_t)cfg.gpio.buzzer, true);
  if (buzzer.init() != ESP_OK) {
    ESP_LOGE(TAG, "Loi khoi tao buzzer");
    led.indicate_error(1000);
    return;
  }

  static WifiManager wifi;
  led.set_wifi_connecting();
  wifi.start();

  TcpClient::instance().start();

  gpio_num_t irq_pin = (cfg.gpio.nfc_irq < 0) ? GPIO_NUM_NC : (gpio_num_t)cfg.gpio.nfc_irq;
  NfcController nfc((gpio_num_t)cfg.gpio.nfc_sda, (gpio_num_t)cfg.gpio.nfc_scl, irq_pin);
  ESP_LOGI(TAG, "Ket noi PN532 (SDA=%d, SCL=%d, IRQ=%d)...", cfg.gpio.nfc_sda, cfg.gpio.nfc_scl, cfg.gpio.nfc_irq);

  while (nfc.init() != ESP_OK) {
    ESP_LOGW(TAG, "Chua tim thay PN532, thu lai sau 2s (kiem tra DIP switch CH1=1, CH2=0)...");
    led.set_hardware_error();
    buzzer.beep_error();
    vTaskDelay(pdMS_TO_TICKS(2000));
  }

  buzzer.beep_boot();
  led.update_status(wifi.is_connected(), TcpClient::instance().is_connected(), wifi.get_state() == WifiState::AP_PORTAL_ACTIVE);
  SystemLogger::instance().log(LogType::SYSTEM, TAG, "PN532 san sang.");
  ESP_LOGI(TAG, "He thong san sang (cooldown=%lu ms, release=%lu ms, feedback=%lu ms)",
           (unsigned long)cfg.scan.cooldown_ms,
           (unsigned long)cfg.scan.release_timeout_ms,
           (unsigned long)cfg.scan.feedback_duration_ms);

  NfcCard card;
  bool is_card_present = false;
  uint32_t last_seen_time = 0;
  uint32_t last_scan_time = 0;
  uint32_t last_status_check = 0;

  while (true) {
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);

    if (now - last_status_check >= 300) {
      last_status_check = now;
      bool is_ap = (wifi.get_state() == WifiState::AP_PORTAL_ACTIVE);
      bool is_wifi = wifi.is_connected();
      bool is_srv = TcpClient::instance().is_connected();
      bool is_sync = (TcpClient::instance().get_pending_offline_count() > 0 && is_srv);
      if (is_sync) {
        led.set_syncing(true);
      } else {
        led.update_status(is_wifi, is_srv, is_ap);
      }
    }

    bool detected = nfc.read_card(card, 60);

    if (!detected) {
      if (is_card_present && (now - last_seen_time > cfg.scan.release_timeout_ms)) {
        is_card_present = false;
      }
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    last_seen_time = now;
    if (is_card_present) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    is_card_present = true;

    if (now - last_scan_time < cfg.scan.cooldown_ms) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    last_scan_time = now;

    ESP_LOGI(TAG, "Quet the: UID [%s]", card.uid_str.c_str());

    std::string resp;
    bool sent = TcpClient::instance().send_scan_blocking(card.uid_str, resp, 800);

    if (sent) {
      bool is_valid = (resp.find("\"SUCCESS\"") != std::string::npos);
      if (is_valid) {
        ESP_LOGI(TAG, "Diem danh hop le tu server: %s", resp.c_str());
        SystemLogger::instance().log_attendance(card.uid_str, true, "Diem danh thanh cong tu server");
        buzzer.beep_success();
        led.indicate_success_online(cfg.scan.feedback_duration_ms);
      } else {
        ESP_LOGW(TAG, "The khong hop le / chua dang ky: %s", resp.c_str());
        SystemLogger::instance().log_attendance(card.uid_str, false, "The chua dang ky");
        buzzer.beep_error();
        led.indicate_unknown_card(cfg.scan.feedback_duration_ms);
      }
    } else {
      ESP_LOGW(TAG, "Server offline, luu vao bo dem tam (offline queue)");
      TcpClient::instance().queue_offline_scan(card.uid_str);
      SystemLogger::instance().log_attendance(card.uid_str, true, "Ghi nhan bo dem offline (cho dong bo)");
      buzzer.beep_success();
      led.indicate_success_offline(cfg.scan.feedback_duration_ms);
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
