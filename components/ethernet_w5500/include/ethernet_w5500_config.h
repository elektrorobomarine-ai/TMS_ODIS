#pragma once

/* ============================================================
 * TMS CONTROLLER - W5500 / TCP SERVER CONFIGURATION
 * Edit this file for device IP and TCP listening port.
 * ============================================================ */
#define TMS_NET_USE_STATIC_IP       1

#define TMS_NET_IPV4_ADDR           "192.168.3.200"
#define TMS_NET_IPV4_NETMASK        "255.255.255.0"
#define TMS_NET_IPV4_GATEWAY        "192.168.3.1"

#define TMS_TCP_SERVER_PORT         5000
