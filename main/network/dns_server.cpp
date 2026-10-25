#include "dns_server.h"
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DNS_SERVER";

#define DNS_PORT 53
#define DNS_MAX_LEN 512

static TaskHandle_t s_dns_task_handle = NULL;
static int s_dns_sock = -1;
static uint32_t s_redirect_ip = 0;

static void dns_server_task(void *pvParameters) {
  uint8_t rx_buffer[DNS_MAX_LEN];
  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);

  ESP_LOGI(TAG, "DNS server khoi dong tren cong 53 UDP");

  while (s_dns_sock >= 0) {
    int len = recvfrom(s_dns_sock, rx_buffer, sizeof(rx_buffer), 0,
                       (struct sockaddr *)&client_addr, &client_addr_len);

    if (len < 12) {
      if (len < 0) {
        break;
      }
      continue;
    }

    rx_buffer[2] = 0x81;
    rx_buffer[3] = 0x80;

    rx_buffer[6] = 0x00;
    rx_buffer[7] = 0x01;

    rx_buffer[8] = 0x00;
    rx_buffer[9] = 0x00;

    rx_buffer[10] = 0x00;
    rx_buffer[11] = 0x00;

    int idx = 12;
    while (idx < len && rx_buffer[idx] != 0) {
      idx += rx_buffer[idx] + 1;
    }

    if (idx + 5 > len) {
      continue;
    }

    idx += 5;

    if (idx + 16 > DNS_MAX_LEN) {
      continue;
    }

    rx_buffer[idx++] = 0xc0;
    rx_buffer[idx++] = 0x0c;

    rx_buffer[idx++] = 0x00;
    rx_buffer[idx++] = 0x01;

    rx_buffer[idx++] = 0x00;
    rx_buffer[idx++] = 0x01;

    rx_buffer[idx++] = 0x00;
    rx_buffer[idx++] = 0x00;
    rx_buffer[idx++] = 0x00;
    rx_buffer[idx++] = 0x3c;

    rx_buffer[idx++] = 0x00;
    rx_buffer[idx++] = 0x04;

    memcpy(&rx_buffer[idx], &s_redirect_ip, 4);
    idx += 4;

    sendto(s_dns_sock, rx_buffer, idx, 0,
           (struct sockaddr *)&client_addr, client_addr_len);
  }

  ESP_LOGI(TAG, "DNS server dung");
  vTaskDelete(NULL);
}

esp_err_t dns_server_start(const char *ip_address) {
  if (s_dns_sock >= 0 || s_dns_task_handle != NULL) {
    ESP_LOGW(TAG, "DNS Server da dang chay!");
    return ESP_OK;
  }

  if (ip_address == NULL) {
    ip_address = "192.168.4.1";
  }

  struct in_addr addr;
  if (inet_aton(ip_address, &addr) == 0) {
    ESP_LOGE(TAG, "Dia chi IP DNS khong hop le: %s", ip_address);
    return ESP_ERR_INVALID_ARG;
  }
  s_redirect_ip = addr.s_addr;

  s_dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (s_dns_sock < 0) {
    ESP_LOGE(TAG, "Khong the tao socket DNS (errno %d)", errno);
    return ESP_FAIL;
  }

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(DNS_PORT);

  if (bind(s_dns_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    ESP_LOGE(TAG, "Khong the bind port 53 DNS (errno %d)", errno);
    close(s_dns_sock);
    s_dns_sock = -1;
    return ESP_FAIL;
  }

  BaseType_t ret = xTaskCreate(dns_server_task, "dns_task", 4096, NULL, 5, &s_dns_task_handle);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Khong the tao DNS task!");
    close(s_dns_sock);
    s_dns_sock = -1;
    return ESP_FAIL;
  }

  return ESP_OK;
}

void dns_server_stop(void) {
  if (s_dns_sock >= 0) {
    int sock = s_dns_sock;
    s_dns_sock = -1;
    close(sock);
  }
  s_dns_task_handle = NULL;
}
