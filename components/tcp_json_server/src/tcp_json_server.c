#include "tcp_json_server.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "app_config.h"
#include "ethernet_w5500_config.h"
#include "command_handler.h"
#include "ethernet_w5500.h"
#include "status_led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "tcp_json";

static int send_all(int sock, const char *data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        int n = send(sock, data + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static void send_json_line(int sock, const char *json)
{
    if (!json) {
        return;
    }
    (void)send_all(sock, json, strlen(json));
    (void)send_all(sock, "\n", 1);
}

static void process_line(int sock, const char *line)
{
    char *response = command_handler_process(line);
    send_json_line(sock, response);
    free(response);
}

static void configure_client_socket(int sock)
{
    int yes = 1;
    int idle = TCP_KEEPALIVE_IDLE_S;
    int interval = TCP_KEEPALIVE_INTERVAL_S;
    int count = TCP_KEEPALIVE_COUNT;

    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));
}

static void handle_client(int sock)
{
    char rx[256];
    char line[TCP_RX_LINE_MAX];
    size_t line_len = 0;

    while (true) {
        int len = recv(sock, rx, sizeof(rx), 0);
        if (len == 0) {
            ESP_LOGI(TAG, "Client closed connection");
            break;
        }
        if (len < 0) {
            if (errno == EINTR) {
                continue;
            }
            ESP_LOGW(TAG, "recv failed: errno=%d", errno);
            break;
        }

        for (int i = 0; i < len; ++i) {
            char ch = rx[i];
            if (ch == '\r') {
                continue;
            }
            if (ch == '\n') {
                if (line_len > 0) {
                    line[line_len] = '\0';
                    process_line(sock, line);
                    line_len = 0;
                }
                continue;
            }

            if (line_len < sizeof(line) - 1) {
                line[line_len++] = ch;
            } else {
                send_json_line(sock,
                    "{\"ok\":false,\"error\":{\"code\":\"LINE_TOO_LONG\",\"message\":\"JSON line exceeds TCP_RX_LINE_MAX\"}}");
                line_len = 0;
            }
        }
    }
}

static void server_task(void *arg)
{
    (void)arg;

    while (true) {
        int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (listen_sock < 0) {
            ESP_LOGE(TAG, "socket() failed: errno=%d", errno);
            status_led_set_system(STATUS_LED_ERROR);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        int yes = 1;
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(TMS_TCP_SERVER_PORT),
            .sin_addr.s_addr = htonl(INADDR_ANY),
        };

        if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
            ESP_LOGE(TAG, "bind() failed: errno=%d", errno);
            close(listen_sock);
            status_led_set_system(STATUS_LED_ERROR);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (listen(listen_sock, TCP_SERVER_BACKLOG) != 0) {
            ESP_LOGE(TAG, "listen() failed: errno=%d", errno);
            close(listen_sock);
            status_led_set_system(STATUS_LED_ERROR);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        ESP_LOGI(TAG, "TCP JSON server listening on port %d", TMS_TCP_SERVER_PORT);

        while (true) {
            struct sockaddr_in source_addr;
            socklen_t addr_len = sizeof(source_addr);
            int client = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
            if (client < 0) {
                ESP_LOGW(TAG, "accept() failed: errno=%d", errno);
                break;
            }

            char client_ip[20] = {0};
            inet_ntoa_r(source_addr.sin_addr, client_ip, sizeof(client_ip));
            ESP_LOGI(TAG, "Client connected: %s:%u", client_ip, ntohs(source_addr.sin_port));
            configure_client_socket(client);
            status_led_set_system(STATUS_LED_CLIENT);

            handle_client(client);
            shutdown(client, SHUT_RDWR);
            close(client);

            status_led_set_system(ethernet_w5500_is_link_up() ? STATUS_LED_READY : STATUS_LED_NET_WAIT);
        }

        close(listen_sock);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t tcp_json_server_start(void)
{
    BaseType_t ok = xTaskCreate(server_task, "tcp_json_server",
                                TCP_SERVER_TASK_STACK_SIZE, NULL,
                                TCP_SERVER_TASK_PRIORITY, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
