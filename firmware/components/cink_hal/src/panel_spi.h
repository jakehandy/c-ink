#pragma once
#include <stdint.h>

#include "cink_hal/board.h"
#include "cink_hal/hal.h"

// Generic SPI e-paper driver that runs the command scripts in the board
// profile. Internal to cink_hal; the app goes through hal::display_*.
namespace hal {
bool panel_spi_init(const Board& b);
void panel_spi_present(const uint8_t* black, const uint8_t* accent, Refresh mode, int rotation);
void panel_spi_sleep();
PanelReport panel_spi_report();
}  // namespace hal
