#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*clock_ethernet_rx_callback_t)(
    const uint8_t *rx,
    int rx_len,
    uint8_t *tx,
    int tx_max
);

/*
 * Initialize W5500 Ethernet using static IP.
 */
esp_err_t clock_ethernet_init_static(void);

/*
 * Initialize W5500 Ethernet using DHCP.
 * You can use this later when connected to a router.
 */
esp_err_t clock_ethernet_init_dhcp(void);

/*
 * Start TCP server.
 * The callback is called whenever TCP data is received.
 */
esp_err_t clock_ethernet_start_tcp_server(clock_ethernet_rx_callback_t rx_callback);

#ifdef __cplusplus
}
#endif