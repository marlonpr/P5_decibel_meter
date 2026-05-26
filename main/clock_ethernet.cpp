#include "clock_ethernet.h"

#include <string.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_eth.h"
#include "esp_mac.h"

#include "driver/spi_master.h"

#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"

#include "driver/gpio.h"

static const char *TAG = "CLOCK_ETHERNET";

// ================= W5500 PINS =================

#define ETH_SPI_HOST       SPI2_HOST

#define ETH_MOSI_GPIO      GPIO_NUM_11
#define ETH_MISO_GPIO      GPIO_NUM_12
#define ETH_SCLK_GPIO      GPIO_NUM_13
#define ETH_CS_GPIO        GPIO_NUM_14
#define ETH_INT_GPIO       GPIO_NUM_10
#define ETH_RST_GPIO       GPIO_NUM_9

#define ETH_SPI_CLOCK_MHZ  20

// ================= TCP SERVER =================

#define TCP_SERVER_PORT    5000

static esp_eth_handle_t s_eth_handle = NULL;
static esp_netif_t *s_eth_netif = NULL;

static bool s_use_static_ip = true;
static clock_ethernet_rx_callback_t s_rx_callback = NULL;

// ================= EVENTS =================

static void eth_event_handler(void *arg,
                              esp_event_base_t event_base,
                              int32_t event_id,
                              void *event_data)
{
    switch (event_id) {
        case ETHERNET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Ethernet Link Up");
            break;

        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Ethernet Link Down");
            break;

        case ETHERNET_EVENT_START:
            ESP_LOGI(TAG, "Ethernet Started");
            break;

        case ETHERNET_EVENT_STOP:
            ESP_LOGI(TAG, "Ethernet Stopped");
            break;

        default:
            break;
    }
}

static void got_ip_event_handler(void *arg,
                                 esp_event_base_t event_base,
                                 int32_t event_id,
                                 void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "ETHIP: " IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK: " IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW: " IPSTR, IP2STR(&ip_info->gw));
}

static void log_tcp_packet(const uint8_t *data, int len)
{
    if (data == NULL || len <= 0) {
        return;
    }

    char hex_line[128];
    char ascii_line[32];

    for (int offset = 0; offset < len; offset += 16) {
        int chunk_len = len - offset;

        if (chunk_len > 16) {
            chunk_len = 16;
        }

        int hex_pos = 0;

        for (int i = 0; i < chunk_len; i++) {
            hex_pos += snprintf(&hex_line[hex_pos],
                                sizeof(hex_line) - hex_pos,
                                "%02X ",
                                data[offset + i]);
        }

        for (int i = chunk_len; i < 16; i++) {
            hex_pos += snprintf(&hex_line[hex_pos],
                                sizeof(hex_line) - hex_pos,
                                "   ");
        }

        for (int i = 0; i < chunk_len; i++) {
            uint8_t c = data[offset + i];

            if (c >= 32 && c <= 126) {
                ascii_line[i] = (char)c;
            } else {
                ascii_line[i] = '.';
            }
        }

        ascii_line[chunk_len] = '\0';

        ESP_LOGI(TAG, "%04X  %-48s  |%s|", offset, hex_line, ascii_line);
    }
}

static bool send_protocol_ack(int client_sock, const uint8_t *rx, int len)
{
    if (rx == NULL || len < 6) {
        return false;
    }

    /*
     * Expected command format:
     * /TA <ID> <CMD1> <CMD2> ... \
     */
    if (rx[0] != '/' || rx[1] != 'T' || rx[2] != 'A') {
        return false;
    }

    uint8_t id = rx[3];
    uint8_t cmd1 = rx[4];
    uint8_t cmd2 = rx[5];

    uint8_t ack[7];

    ack[0] = '/';
    ack[1] = 't';
    ack[2] = 'a';
    ack[3] = id;
    ack[4] = (uint8_t)(cmd1 + 0x20);  // uppercase to lowercase
    ack[5] = (uint8_t)(cmd2 + 0x20);  // uppercase to lowercase
    ack[6] = '\\';

    ESP_LOGI(TAG,
             "Sending ACK: /ta ID=0x%02X %c%c \\",
             id,
             ack[4],
             ack[5]);

    int sent = send(client_sock, ack, sizeof(ack), 0);

    return sent == sizeof(ack);
}

// ================= TCP SERVER TASK =================

// ================= TCP SERVER TASK =================

static void tcp_server_task(void *pvParameters)
{
    char rx_buffer[128];

    struct sockaddr_in server_addr = {};
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_SERVER_PORT);

    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create TCP socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int err = bind(listen_sock,
                   (struct sockaddr *)&server_addr,
                   sizeof(server_addr));

    if (err != 0) {
        ESP_LOGE(TAG, "Socket bind failed: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Socket listen failed: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "TCP server listening on port %d", TCP_SERVER_PORT);

    while (true) {
        struct sockaddr_in client_addr = {};
        socklen_t client_addr_len = sizeof(client_addr);

        ESP_LOGI(TAG, "Waiting for TCP client...");

        int client_sock = accept(listen_sock,
                                 (struct sockaddr *)&client_addr,
                                 &client_addr_len);

        if (client_sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            continue;
        }

        ESP_LOGI(TAG,
                 "TCP client connected from %s:%d",
                 inet_ntoa(client_addr.sin_addr),
                 ntohs(client_addr.sin_port));

        /*
         * Do not send welcome text to the Zeit software.
         * It expects binary protocol only.
         */

        while (true) {
            int len = recv(client_sock,
                           rx_buffer,
                           sizeof(rx_buffer),
                           0);

            if (len < 0) {
                ESP_LOGE(TAG, "recv failed: errno %d", errno);
                break;
            }

            if (len == 0) {
                ESP_LOGI(TAG, "TCP client disconnected");
                break;
            }

            ESP_LOGI(TAG, "TCP RX %d bytes", len);
            log_tcp_packet((const uint8_t *)rx_buffer, len);

            uint8_t tx_buffer[64];
            int tx_len = 0;

            if (s_rx_callback != NULL) {
                tx_len = s_rx_callback((const uint8_t *)rx_buffer,
                                       len,
                                       tx_buffer,
                                       sizeof(tx_buffer));
            }

            if (tx_len > 0) {
                ESP_LOGI(TAG, "Sending custom response: %d bytes", tx_len);
                log_tcp_packet(tx_buffer, tx_len);
                send(client_sock, tx_buffer, tx_len, 0);
            } else if (tx_len == 0) {
                send_protocol_ack(client_sock,
                                  (const uint8_t *)rx_buffer,
                                  len);
            } else {
                ESP_LOGI(TAG, "No response sent");
            }
        }

        shutdown(client_sock, 0);
        close(client_sock);
    }
}

// ================= INIT =================

static esp_err_t clock_ethernet_init_common(bool use_static_ip)
{
    s_use_static_ip = use_static_ip;

	ESP_LOGI(TAG, "Initializing W5500 Ethernet");

	esp_err_t isr_ret = gpio_install_isr_service(0);

	if (isr_ret != ESP_OK && isr_ret != ESP_ERR_INVALID_STATE) {
	    ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(isr_ret));
	    return isr_ret;
	}

    ESP_ERROR_CHECK(esp_netif_init());

    esp_err_t event_ret = esp_event_loop_create_default();
    if (event_ret != ESP_OK && event_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s",
                 esp_err_to_name(event_ret));
        return event_ret;
    }

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&netif_cfg);
    if (s_eth_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create Ethernet netif");
        return ESP_FAIL;
    }

    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = ETH_MOSI_GPIO;
    buscfg.miso_io_num = ETH_MISO_GPIO;
    buscfg.sclk_io_num = ETH_SCLK_GPIO;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;

    ESP_ERROR_CHECK(spi_bus_initialize(ETH_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {};
    devcfg.command_bits = 16;
    devcfg.address_bits = 8;
    devcfg.mode = 0;
    devcfg.clock_speed_hz = ETH_SPI_CLOCK_MHZ * 1000 * 1000;
    devcfg.spics_io_num = ETH_CS_GPIO;
    devcfg.queue_size = 20;

    eth_w5500_config_t w5500_config =
        ETH_W5500_DEFAULT_CONFIG(ETH_SPI_HOST, &devcfg);

    w5500_config.int_gpio_num = ETH_INT_GPIO;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    phy_config.phy_addr = 1;
    phy_config.reset_gpio_num = ETH_RST_GPIO;

    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);

    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &s_eth_handle));

    uint8_t mac_addr[6] = {0};

    ESP_ERROR_CHECK(esp_read_mac(mac_addr, ESP_MAC_WIFI_STA));

    /*
     * Local administered unicast MAC for Ethernet.
     */
    mac_addr[0] = (mac_addr[0] & 0xFE) | 0x02;

    ESP_ERROR_CHECK(esp_eth_ioctl(s_eth_handle,
                                  ETH_CMD_S_MAC_ADDR,
                                  mac_addr));

    ESP_LOGI(TAG,
             "W5500 MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac_addr[0],
             mac_addr[1],
             mac_addr[2],
             mac_addr[3],
             mac_addr[4],
             mac_addr[5]);

    ESP_ERROR_CHECK(esp_netif_attach(s_eth_netif,
                                     esp_eth_new_netif_glue(s_eth_handle)));

    if (s_use_static_ip) {
        ESP_ERROR_CHECK(esp_netif_dhcpc_stop(s_eth_netif));

        esp_netif_ip_info_t ip_info = {};

        ip_info.ip.addr      = ESP_IP4TOADDR(192, 168, 10, 51);
        ip_info.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);
        ip_info.gw.addr      = ESP_IP4TOADDR(192, 168, 10, 1);

        ESP_ERROR_CHECK(esp_netif_set_ip_info(s_eth_netif, &ip_info));

        ESP_LOGI(TAG, "Static Ethernet IP configured: 192.168.10.50");
    }

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT,
                                               ESP_EVENT_ANY_ID,
                                               &eth_event_handler,
                                               NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                               IP_EVENT_ETH_GOT_IP,
                                               &got_ip_event_handler,
                                               NULL));

    ESP_ERROR_CHECK(esp_eth_start(s_eth_handle));

    ESP_LOGI(TAG, "W5500 Ethernet init done");

    return ESP_OK;
}

esp_err_t clock_ethernet_init_static(void)
{
    return clock_ethernet_init_common(true);
}

esp_err_t clock_ethernet_init_dhcp(void)
{
    return clock_ethernet_init_common(false);
}

esp_err_t clock_ethernet_start_tcp_server(clock_ethernet_rx_callback_t rx_callback)
{
    s_rx_callback = rx_callback;

    BaseType_t ret = xTaskCreatePinnedToCore(
        tcp_server_task,
        "TcpServerTask",
        4096,
        NULL,
        4,
        NULL,
        0
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TCP server task");
        return ESP_FAIL;
    }

    return ESP_OK;
}