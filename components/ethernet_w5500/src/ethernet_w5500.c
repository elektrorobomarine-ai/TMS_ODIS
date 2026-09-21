#include "ethernet_w5500.h"

#include <string.h>
#include "board_config.h"
#include "ethernet_w5500_config.h"
#include "status_led.h"
#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"
#include "esp_eth_netif_glue.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_check.h"

static const char *TAG = "eth_w5500";
static esp_eth_handle_t s_eth_handle = NULL;
static esp_netif_t *s_eth_netif = NULL;
static esp_eth_netif_glue_handle_t s_glue = NULL;
static volatile bool s_link_up = false;

static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        s_link_up = true;
        status_led_set_system(STATUS_LED_READY);
        ESP_LOGI(TAG, "Ethernet link up");
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        s_link_up = false;
        status_led_set_system(STATUS_LED_NET_WAIT);
        ESP_LOGW(TAG, "Ethernet link down");
        break;
    case ETHERNET_EVENT_START:
        status_led_set_system(STATUS_LED_NET_WAIT);
        ESP_LOGI(TAG, "Ethernet started");
        break;
    case ETHERNET_EVENT_STOP:
        s_link_up = false;
        status_led_set_system(STATUS_LED_NET_WAIT);
        ESP_LOGI(TAG, "Ethernet stopped");
        break;
    default:
        break;
    }
}

static void got_ip_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_id;
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "IPv4: " IPSTR, IP2STR(&event->ip_info.ip));
}

static esp_err_t configure_static_ip(void)
{
#if TMS_NET_USE_STATIC_IP
    esp_netif_ip_info_t ip_info = {0};
    ESP_RETURN_ON_ERROR(esp_netif_str_to_ip4(TMS_NET_IPV4_ADDR, &ip_info.ip), TAG, "invalid IP");
    ESP_RETURN_ON_ERROR(esp_netif_str_to_ip4(TMS_NET_IPV4_NETMASK, &ip_info.netmask), TAG, "invalid netmask");
    ESP_RETURN_ON_ERROR(esp_netif_str_to_ip4(TMS_NET_IPV4_GATEWAY, &ip_info.gw), TAG, "invalid gateway");

    /* Ignore the return value: depending on netif lifecycle, DHCP can already be stopped. */
    (void)esp_netif_dhcpc_stop(s_eth_netif);
    ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(s_eth_netif, &ip_info), TAG, "set static IPv4 failed");
    ESP_LOGI(TAG, "Static IPv4 configured: %s / %s, gw %s",
             TMS_NET_IPV4_ADDR, TMS_NET_IPV4_NETMASK, TMS_NET_IPV4_GATEWAY);
#endif
    return ESP_OK;
}

esp_err_t ethernet_w5500_init(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");

    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num = W5500_MOSI_GPIO,
        .miso_io_num = W5500_MISO_GPIO,
        .sclk_io_num = W5500_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(W5500_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "SPI init failed");

    spi_device_interface_config_t devcfg = {
        .mode = 0,
        .clock_speed_hz = W5500_SPI_CLOCK_HZ,
        .spics_io_num = W5500_CS_GPIO,
        .queue_size = 16,
    };

    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(W5500_SPI_HOST, &devcfg);
    w5500_config.base.int_gpio_num = W5500_INT_GPIO;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac_config.rx_task_stack_size = 4096;

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.reset_gpio_num = W5500_RST_GPIO;

    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
    if (!mac || !phy) {
        ESP_LOGE(TAG, "Failed to create W5500 MAC/PHY");
        return ESP_FAIL;
    }

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_RETURN_ON_ERROR(esp_eth_driver_install(&eth_config, &s_eth_handle), TAG, "eth driver install failed");

    uint8_t mac_addr[6];
    ESP_RETURN_ON_ERROR(esp_read_mac(mac_addr, ESP_MAC_ETH), TAG, "read MAC failed");
    ESP_RETURN_ON_ERROR(esp_eth_ioctl(s_eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr), TAG, "set MAC failed");

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&netif_cfg);
    if (!s_eth_netif) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(configure_static_ip(), TAG, "IPv4 configuration failed");

    s_glue = esp_eth_new_netif_glue(s_eth_handle);
    if (!s_glue) {
        return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(esp_netif_attach(s_eth_netif, s_glue), TAG, "netif attach failed");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL), TAG, "eth event register failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_handler, NULL), TAG, "ip event register failed");

    ESP_RETURN_ON_ERROR(esp_eth_start(s_eth_handle), TAG, "eth start failed");
    return ESP_OK;
}

bool ethernet_w5500_is_link_up(void)
{
    return s_link_up;
}

esp_err_t ethernet_w5500_get_ip(char *buf, size_t buf_len)
{
    if (!buf || buf_len < 16 || !s_eth_netif) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_netif_ip_info_t ip_info;
    ESP_RETURN_ON_ERROR(esp_netif_get_ip_info(s_eth_netif, &ip_info), TAG, "get IP info failed");
    if (!esp_ip4addr_ntoa(&ip_info.ip, buf, (int)buf_len)) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
