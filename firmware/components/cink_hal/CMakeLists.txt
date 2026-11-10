# Board layer. Pins and the panel driver come from the board profile in NVS
# (src/board.cpp); src/display.cpp picks the panel backend at boot.
idf_component_register(SRCS src/board.cpp src/buttons.cpp src/display.cpp src/panel_spi.cpp src/power.cpp
                       INCLUDE_DIRS include
                       REQUIRES cink_core
                       PRIV_REQUIRES esp_adc esp_driver_gpio esp_driver_spi esp_driver_usb_serial_jtag
                                     esp_hw_support esp_timer json log nvs_flash)
