#include "nfc.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <utility>

static const char *TAG = "PN532_I2C";

static const uint8_t PN532_ACK[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};

static void i2c_bus_clear(gpio_num_t sda, gpio_num_t scl) {
  gpio_reset_pin(sda);
  gpio_reset_pin(scl);
  gpio_set_direction(sda, GPIO_MODE_INPUT);
  gpio_set_direction(scl, GPIO_MODE_OUTPUT);
  gpio_set_level(scl, 1);
  for (int i = 0; i < 9; i++) {
    gpio_set_level(scl, 0);
    esp_rom_delay_us(5);
    gpio_set_level(scl, 1);
    esp_rom_delay_us(5);
  }
}

NfcController::NfcController(gpio_num_t sda_pin, gpio_num_t scl_pin,
                             gpio_num_t irq_pin, i2c_port_t i2c_port)
    : sda_pin_(sda_pin), scl_pin_(scl_pin), irq_pin_(irq_pin), i2c_port_(i2c_port) {}

NfcController::~NfcController() {
  i2c_driver_delete(i2c_port_);
}

esp_err_t NfcController::init() {
  i2c_bus_clear(sda_pin_, scl_pin_);

  if (irq_pin_ != GPIO_NUM_NC) {
    gpio_config_t irq_conf = {};
    irq_conf.pin_bit_mask = (1ULL << irq_pin_);
    irq_conf.mode = GPIO_MODE_INPUT;
    irq_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&irq_conf);
    ESP_LOGI(TAG, "Chân ngắt IRQ kích hoạt trên GPIO %d", irq_pin_);
  }

  ESP_LOGI(TAG, "Khởi tạo I2C Master (SDA=GPIO %d, SCL=GPIO %d, tốc độ 100 kHz)...",
           sda_pin_, scl_pin_);

  i2c_config_t conf = {};
  conf.mode = I2C_MODE_MASTER;
  conf.sda_io_num = sda_pin_;
  conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
  conf.scl_io_num = scl_pin_;
  conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
  conf.master.clk_speed = 100000;

  esp_err_t err = i2c_param_config(i2c_port_, &conf);
  if (err != ESP_OK) return err;

  err = i2c_driver_install(i2c_port_, conf.mode, 0, 0, 0);
  if (err != ESP_OK) return err;

  uint8_t dummy = 0x00;
  i2c_master_write_to_device(i2c_port_, PN532_I2C_ADDRESS, &dummy, 1, pdMS_TO_TICKS(50));
  vTaskDelay(pdMS_TO_TICKS(20));

  flush_buffer();

  err = check_communication();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Chưa thấy PN532 trên SDA=%d, SCL=%d. Thử đảo chân SDA=%d, SCL=%d...",
             sda_pin_, scl_pin_, scl_pin_, sda_pin_);
    i2c_driver_delete(i2c_port_);
    std::swap(sda_pin_, scl_pin_);
    conf.sda_io_num = sda_pin_;
    conf.scl_io_num = scl_pin_;
    i2c_param_config(i2c_port_, &conf);
    i2c_driver_install(i2c_port_, conf.mode, 0, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    err = check_communication();
    if (err != ESP_OK) {
      i2c_driver_delete(i2c_port_);
      std::swap(sda_pin_, scl_pin_);
      ESP_LOGE(TAG, "Khong tim thay PN532 tren I2C");
      return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Nhan PN532 sau khi dao chan (SDA=%d, SCL=%d)", sda_pin_, scl_pin_);
  }

  err = configure_sam();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Loi cau hinh SAM qua I2C");
    return err;
  }

  ESP_LOGI(TAG, "Khoi tao PN532 thanh cong");
  return ESP_OK;
}

void NfcController::flush_buffer() {
  uint8_t dump[32];
  for (int i = 0; i < 3; i++) {
    if (!read_raw(dump, sizeof(dump))) {
      break;
    }
  }
}

bool NfcController::read_raw(uint8_t *buf, size_t len) {
  std::vector<uint8_t> rx(len + 1, 0);
  esp_err_t err = i2c_master_read_from_device(i2c_port_, PN532_I2C_ADDRESS,
                                              rx.data(), rx.size(), pdMS_TO_TICKS(50));
  if (err != ESP_OK) return false;

  uint8_t status = rx[0];
  if ((status & 0x01) != 0x01) {
    return false;
  }

  if (buf != nullptr && len > 0) {
    memcpy(buf, &rx[1], len);
  }
  return true;
}

esp_err_t NfcController::send_command(const uint8_t *cmd, size_t cmd_len) {
  uint8_t len = cmd_len + 1;
  uint8_t dcs = 0xD4;

  std::vector<uint8_t> frame;
  frame.reserve(cmd_len + 8);
  frame.push_back(0x00);
  frame.push_back(0x00);
  frame.push_back(0xFF);
  frame.push_back(len);
  frame.push_back((uint8_t)(~len + 1));
  frame.push_back(0xD4);
  for (size_t i = 0; i < cmd_len; i++) {
    frame.push_back(cmd[i]);
    dcs += cmd[i];
  }
  frame.push_back((uint8_t)(~dcs + 1));
  frame.push_back(0x00);

  flush_buffer();

  for (int attempt = 0; attempt < 3; attempt++) {
    esp_err_t err = i2c_master_write_to_device(i2c_port_, PN532_I2C_ADDRESS,
                                               frame.data(), frame.size(),
                                               pdMS_TO_TICKS(100));
    if (err == ESP_OK) {
      if (read_ack(200)) {
        return ESP_OK;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  return ESP_FAIL;
}

bool NfcController::read_ack(uint32_t timeout_ms) {
  uint32_t waited = 0;
  uint8_t ack_buf[6] = {0};

  while (waited < timeout_ms) {
    if (read_raw(ack_buf, 6)) {
      if (memcmp(ack_buf, PN532_ACK, 6) == 0) {
        return true;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
    waited += 5;
  }

  return false;
}

int NfcController::read_data(uint8_t *data_out, size_t max_len, uint32_t timeout_ms) {
  uint32_t waited = 0;
  uint8_t frame[40] = {0};

  while (waited < timeout_ms) {
    if (read_raw(frame, sizeof(frame))) {
      for (int i = 0; i < (int)sizeof(frame) - 5; i++) {
        if (frame[i] == 0x00 && frame[i + 1] == 0x00 && frame[i + 2] == 0xFF) {
          uint8_t len = frame[i + 3];
          uint8_t lcs = frame[i + 4];
          if ((uint8_t)(len + lcs) == 0 && frame[i + 5] == 0xD5) {
            int payload_len = len - 2;
            if (payload_len < 0) return -1;
            if ((size_t)payload_len > max_len) payload_len = max_len;
            if (i + 7 + payload_len <= (int)sizeof(frame) && data_out != nullptr) {
              memcpy(data_out, &frame[i + 7], payload_len);
            }
            return payload_len;
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
    waited += 5;
  }

  return -1;
}

esp_err_t NfcController::check_communication() {
  const uint8_t cmd_ver[] = {0x02};
  uint8_t resp[8] = {0};

  for (int attempt = 1; attempt <= 3; attempt++) {
    if (send_command(cmd_ver, sizeof(cmd_ver)) == ESP_OK) {
      int len = read_data(resp, sizeof(resp), 300);
      if (len >= 3) {
        ESP_LOGI(TAG, "PN532 firmware v%d.%d, IC: 0x%02X",
                 resp[1], resp[2], resp[0]);
        return ESP_OK;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  return ESP_FAIL;
}

esp_err_t NfcController::configure_sam() {
  uint8_t resp[8] = {0};

  const uint8_t sam_cmd[] = {0x14, 0x01, 0x14, 0x01};
  if (send_command(sam_cmd, sizeof(sam_cmd)) != ESP_OK ||
      read_data(resp, sizeof(resp), 200) < 0) {
    return ESP_FAIL;
  }

  const uint8_t retry_cmd[] = {0x32, 0x05, 0xFF, 0x01, 0x03};
  if (send_command(retry_cmd, sizeof(retry_cmd)) != ESP_OK ||
      read_data(resp, sizeof(resp), 200) < 0) {
    return ESP_FAIL;
  }

  return ESP_OK;
}

bool NfcController::read_card(NfcCard &card_out, uint32_t timeout_ms) {
  const uint8_t scan_cmd[] = {0x4A, 0x01, 0x00};
  if (send_command(scan_cmd, sizeof(scan_cmd)) != ESP_OK) {
    return false;
  }

  uint8_t data[32] = {0};
  int n = read_data(data, sizeof(data), timeout_ms);
  if (n < 7) {
    return false;
  }

  if (data[0] != 1) {
    return false;
  }

  uint8_t uid_len = data[5];
  if (uid_len == 0 || uid_len > 10 || (n < 6 + uid_len)) {
    return false;
  }

  card_out.uid.clear();
  card_out.uid_str.clear();

  char hex_buf[4];
  for (int i = 0; i < uid_len; i++) {
    uint8_t b = data[6 + i];
    card_out.uid.push_back(b);
    snprintf(hex_buf, sizeof(hex_buf), (i == uid_len - 1) ? "%02X" : "%02X ", b);
    card_out.uid_str += hex_buf;
  }

  card_out.card_type = data[4];
  return true;
}
