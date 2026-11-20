// Target adapter for the evidenced Arduino ESP32 1.0.1 SDK interface.
// This adapter supplies real SDK operations; it is not a complete application.
#pragma once
#include "zseries/core_startup.hpp"
#include "zseries/msp_protocol.h"
#include "zseries/safeties.hpp"
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <array>

namespace zseries { namespace oem { namespace esp32 {

struct BusPresence {
    std::array<bool, 128> found{};
    std::array<std::uint8_t, 3> missing{};
    std::size_t missing_count = 0;
};

struct System {
    SPIClass spi;
    TwoWire& sensor_bus;
    TwoWire& controller_bus;
    const esp_partition_t* factory_partition = nullptr;
    const esp_partition_t* running_partition = nullptr;
    const esp_partition_t* boot_partition = nullptr;
    BusPresence sensors;
    BusPresence controller;

    System();
    // Original 0x40112f04 scans both buses. Full setup is assembled by
    // system_setup_platform.hpp, including returning assertions and commands.
    void scan_buses();
};

HeaterIo gpio() noexcept;
SafetyIo safety_input() noexcept;
I2cTransport i2c(TwoWire&) noexcept;
AcquisitionClock acquisition_clock() noexcept;
z_msp_transport msp_transport(TwoWire&) noexcept;
int initialize_msp(z_msp_client&, z_msp_state&, System&) noexcept;

// Core task settings from original app_main 0x400ee978. No entry point is
// installed here: the application must first supply all required Core parts.
bool start_core_task(TaskFunction_t entry, void* context) noexcept;
void initialize_console();

struct TimerCallbacks {
    TimerCallbackFunction_t buzzer;
    void (*heater_second)();
    void (*heater_fast)();
};

// Typed callback implementations for CoreStartupServices. Its context must
// point to TimerCallbacks for these methods; a composed application can
// forward from its own context. Callback targets remain caller-owned.
bool start_periodic_software_timer(void*, std::uint32_t, std::uintptr_t,
                                    CoreTimerCallback, std::uint32_t, PlatformHandle&);
PlatformHandle begin_hardware_timer(void*, std::uint8_t, std::uint16_t, bool);
void attach_timer_interrupt(void*, PlatformHandle, CoreTimerCallback, bool);
void write_timer_alarm(void*, PlatformHandle, std::uint64_t, bool);
void enable_timer_alarm(void*, PlatformHandle, bool);

}}} // namespace zseries::oem::esp32
