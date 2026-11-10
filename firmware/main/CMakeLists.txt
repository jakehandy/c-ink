idf_component_register(
  SRCS app.cpp config.cpp cache.cpp crypto.cpp net.cpp sync.cpp usb_link.cpp
  INCLUDE_DIRS .
  PRIV_REQUIRES cink_core cink_hal nvs_flash esp_wifi esp_netif esp_event esp_http_client
                esp_partition mbedtls json esp_driver_usb_serial_jtag esp_timer esp_app_format
                spi_flash)
