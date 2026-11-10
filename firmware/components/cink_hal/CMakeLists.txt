set(srcs src/buttons.cpp src/power.cpp)
if(CONFIG_CINK_DISPLAY_TICKEY)
  list(APPEND srcs src/display_tickey.cpp)
else()
  list(APPEND srcs src/display_headless.cpp)
endif()
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS include
                       PRIV_REQUIRES esp_adc esp_driver_gpio esp_driver_usb_serial_jtag esp_hw_support log)
