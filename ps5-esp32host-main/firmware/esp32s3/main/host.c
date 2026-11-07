#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "dhcpserver/dhcpserver.h"
#include "dns.h"

static const char *TAG = "ps5_host";
static const uint8_t HOST_IP[] = {192, 168, 4, 1};
extern const unsigned char cert_start[] asm("_binary_server_crt_start");
extern const unsigned char cert_end[] asm("_binary_server_crt_end");
extern const unsigned char key_start[] asm("_binary_server_key_start");
extern const unsigned char key_end[] asm("_binary_server_key_end");

static void dns_task(void *unused)
{
    (void)unused;
    uint8_t query[512], reply[512];
    for (;;) {
        int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(53),
                                     .sin_addr.s_addr = htonl(INADDR_ANY)};
        if (fd < 0 || bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
            ESP_LOGE(TAG, "DNS socket failed: errno=%d", errno);
            if (fd >= 0) close(fd);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        ESP_LOGI(TAG, "DNS ready on UDP 53");
        for (;;) {
            struct sockaddr_in peer;
            socklen_t length = sizeof(peer);
            int got = recvfrom(fd, query, sizeof(query), 0, (struct sockaddr *)&peer, &length);
            if (got < 0) break;
            size_t count = ps5_dns_reply(query, got, reply, sizeof(reply), HOST_IP);
            if (count) sendto(fd, reply, count, 0, (struct sockaddr *)&peer, length);
        }
        close(fd);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static const char *content_type(const char *path)
{
    const char *extension = strrchr(path, '.');
    if (!extension) return "application/octet-stream";
    if (!strcmp(extension, ".html")) return "text/html; charset=utf-8";
    if (!strcmp(extension, ".js")) return "application/javascript";
    if (!strcmp(extension, ".css")) return "text/css";
    if (!strcmp(extension, ".json")) return "application/json";
    return "application/octet-stream";
}

static esp_err_t serve_file(httpd_req_t *request)
{
    char uri[192];
    size_t length = strcspn(request->uri, "?#");
    if (length == 0 || length >= sizeof(uri)) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid path");
    memcpy(uri, request->uri, length);
    uri[length] = 0;
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (!strcmp(uri, "/document") || !strncmp(uri, "/document/", 10)) {
        httpd_resp_set_status(request, "302 Found");
        httpd_resp_set_hdr(request, "Location", "/");
        return httpd_resp_send(request, NULL, 0);
    }
    if (!strcmp(uri, "/status")) {
        httpd_resp_set_type(request, "application/json");
        return httpd_resp_sendstr(request, "{\"host\":\"PS5-Host\",\"ip\":\"192.168.4.1\",\"dhcp\":true,\"offline\":true}");
    }
    if (strstr(uri, "..") || strchr(uri, '%') || strchr(uri, '\\'))
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid path");
    const char *relative = !strcmp(uri, "/") ? "/index.html" : uri;
    // Only published site assets are exposed; certs are embedded in the app.
    if (strcmp(relative, "/index.html") && strncmp(relative, "/src/", 5) &&
        strncmp(relative, "/offsets/", 9) && strncmp(relative, "/payloads/", 10))
        return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Not found");
    char path[208];
    snprintf(path, sizeof(path), "/web%s", relative);
    FILE *file = fopen(path, "rb");
    if (!file) return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "File not found");
    httpd_resp_set_type(request, content_type(path));
    ESP_LOGI(TAG, "GET %s", uri);
    char *buffer = malloc(8192);
    if (!buffer) {
        fclose(file);
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    esp_err_t result = ESP_OK;
    size_t got;
    while ((got = fread(buffer, 1, 8192, file)) > 0) {
        result = httpd_resp_send_chunk(request, buffer, got);
        if (result != ESP_OK) break;
    }
    if (ferror(file)) result = ESP_FAIL;
    free(buffer);
    fclose(file);
    if (result != ESP_OK) return ESP_FAIL;
    return httpd_resp_send_chunk(request, NULL, 0);
}

static void start_servers(void)
{
    httpd_handle_t http = NULL, https = NULL;
    httpd_config_t plain = HTTPD_DEFAULT_CONFIG();
    plain.server_port = 80;
    plain.ctrl_port = 32768;
    plain.max_open_sockets = 3;
    plain.lru_purge_enable = true;
    plain.uri_match_fn = httpd_uri_match_wildcard;
    plain.stack_size = 6144;
    plain.send_wait_timeout = 20;
    httpd_uri_t route = {.uri = "/*", .method = HTTP_GET, .handler = serve_file};
    ESP_ERROR_CHECK(httpd_start(&http, &plain));
    ESP_ERROR_CHECK(httpd_register_uri_handler(http, &route));

    httpd_ssl_config_t secure = HTTPD_SSL_CONFIG_DEFAULT();
    secure.httpd.ctrl_port = 32769;
    secure.httpd.max_open_sockets = 4;
    secure.httpd.lru_purge_enable = true;
    secure.httpd.uri_match_fn = httpd_uri_match_wildcard;
    secure.httpd.stack_size = 8192;
    secure.httpd.send_wait_timeout = 20;
    secure.servercert = cert_start;
    secure.servercert_len = cert_end - cert_start;
    secure.prvtkey_pem = key_start;
    secure.prvtkey_len = key_end - key_start;
    secure.port_secure = 443;
    ESP_ERROR_CHECK(httpd_ssl_start(&https, &secure));
    ESP_ERROR_CHECK(httpd_register_uri_handler(https, &route));
    ESP_LOGI(TAG, "HTTP :80 and HTTPS :443 ready");
}

static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == IP_EVENT && id == IP_EVENT_AP_STAIPASSIGNED) {
        ip_event_ap_staipassigned_t *event = data;
        ESP_LOGI(TAG, "DHCP assigned " IPSTR, IP2STR(&event->ip));
    }
}

static void start_access_point(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    assert(ap);
    // Configure the built-in DHCP server before WIFI_EVENT_AP_START starts it.
    esp_err_t stopped = esp_netif_dhcps_stop(ap);
    if (stopped != ESP_OK && stopped != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) ESP_ERROR_CHECK(stopped);
    esp_netif_ip_info_t ip = {0};
    esp_netif_set_ip4_addr(&ip.ip, 192, 168, 4, 1);
    esp_netif_set_ip4_addr(&ip.gw, 192, 168, 4, 1);
    esp_netif_set_ip4_addr(&ip.netmask, 255, 255, 255, 0);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap, &ip));
    esp_netif_dns_info_t dns = {.ip.type = ESP_IPADDR_TYPE_V4};
    dns.ip.u_addr.ip4.addr = ip.ip.addr;
    dhcps_offer_t offer = OFFER_DNS;
    ESP_ERROR_CHECK(esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer)));
    ESP_ERROR_CHECK(esp_netif_set_dns_info(ap, ESP_NETIF_DNS_MAIN, &dns));
    uint32_t lease_minutes = 120;
    ESP_ERROR_CHECK(esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_IP_ADDRESS_LEASE_TIME, &lease_minutes, sizeof(lease_minutes)));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, network_event, NULL));
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    const char *ssid = CONFIG_PS5_AP_SSID, *password = CONFIG_PS5_AP_PASSWORD;
    if (strlen(ssid) == 0 || strlen(ssid) > 32 || strlen(password) < 8 || strlen(password) > 63) {
        ESP_LOGE(TAG, "Invalid SSID/password length; fix menuconfig");
        abort();
    }
    wifi_config_t wifi = {.ap = {.channel = CONFIG_PS5_AP_CHANNEL, .max_connection = 4,
                                 .authmode = WIFI_AUTH_WPA2_PSK, .pmf_cfg.required = false}};
    memcpy(wifi.ap.ssid, ssid, strlen(ssid));
    wifi.ap.ssid_len = strlen(ssid);
    memcpy(wifi.ap.password, password, strlen(password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi));
    // Re-arm DHCP after stopping it for configuration. While the interface is
    // down this sets INIT, so AP_START starts the server with our DNS options.
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Wi-Fi '%s': DHCP, gateway and DNS at 192.168.4.1 (offline)", ssid);
}

void app_main(void)
{
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);
    esp_vfs_spiffs_conf_t filesystem = {.base_path = "/web", .partition_label = "web",
                                      .max_files = 6, .format_if_mount_failed = false};
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&filesystem));
    size_t total, used;
    ESP_ERROR_CHECK(esp_spiffs_info("web", &total, &used));
    ESP_LOGI(TAG, "Web filesystem: %u / %u bytes", (unsigned)used, (unsigned)total);
    start_access_point();
    if (xTaskCreate(dns_task, "dns", 4096, NULL, 5, NULL) != pdPASS) abort();
    start_servers();
    ESP_LOGI(TAG, "Ready. Connect PS5 with automatic IP and DNS, then open User's Guide.");
}
