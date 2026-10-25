#pragma once

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include <cstdint>
#include <string>
#include <vector>

#define DEFAULT_NFC_SDA_PIN   GPIO_NUM_21
#define DEFAULT_NFC_SCL_PIN   GPIO_NUM_22
#define DEFAULT_NFC_IRQ_PIN   GPIO_NUM_16
#define PN532_I2C_ADDRESS     0x24

struct NfcCard {
  std::string uid_str;
  std::vector<uint8_t> uid;
  uint8_t card_type;
};

class NfcController {
public:
  NfcController(gpio_num_t sda_pin = DEFAULT_NFC_SDA_PIN,
                gpio_num_t scl_pin = DEFAULT_NFC_SCL_PIN,
                gpio_num_t irq_pin = DEFAULT_NFC_IRQ_PIN,
                i2c_port_t i2c_port = I2C_NUM_0);
  ~NfcController();

  esp_err_t init();
  esp_err_t check_communication();
  bool read_card(NfcCard &card_out, uint32_t timeout_ms = 100);

private:
  gpio_num_t sda_pin_;
  gpio_num_t scl_pin_;
  gpio_num_t irq_pin_;
  i2c_port_t i2c_port_;

  esp_err_t send_command(const uint8_t *cmd, size_t cmd_len);
  bool read_ack(uint32_t timeout_ms = 200);
  int read_data(uint8_t *data_out, size_t max_len, uint32_t timeout_ms = 200);
  bool read_raw(uint8_t *buf, size_t len);
  bool is_ready();
  void flush_buffer();
  esp_err_t configure_sam();
};
