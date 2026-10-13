// Shared I2C0 master bus on SDA=GPIO8 / SCL=GPIO9, used by all M5Stack
// PORT.A peripherals on the lock (ByteButton, OLED, future units).
//
// Lazy-init on first call. All consumers add their own device handle
// via i2c_master_bus_add_device() — they don't share device handles,
// only the bus.

#pragma once

#include <driver/i2c_master.h>

namespace aliro::i2c {

// Returns a singleton I2C0 master bus handle, initializing it on first
// call. Returns nullptr on failure (consumer should disable itself).
i2c_master_bus_handle_t shared_bus();

}  // namespace aliro::i2c
