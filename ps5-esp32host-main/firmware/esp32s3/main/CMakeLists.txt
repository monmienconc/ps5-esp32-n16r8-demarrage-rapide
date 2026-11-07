idf_component_register(SRCS "host.c" "dns.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_server esp_https_server spiffs lwip
    EMBED_TXTFILES "../certs/server.crt" "../certs/server.key")
