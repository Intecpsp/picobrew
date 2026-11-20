// Physical Button/Encoder producers recovered from the preserved Z119 image.
#pragma once
#include "zseries/core_flow.hpp"
#include <cstdint>

#if defined(__XTENSA__)
#define ZSERIES_INPUT_IRAM __attribute__((section(".iram1")))
#else
#define ZSERIES_INPUT_IRAM
#endif

namespace zseries { namespace oem {

enum class InputInterrupt : std::uint8_t { EncoderChange, ButtonFalling };
struct PhysicalInputIo {
    void* context = nullptr;
    std::uint32_t (*millis)(void*) = nullptr;
    bool (*pin_mode)(void*, std::uint8_t pin, std::uint8_t mode) = nullptr;
    // This callback is used in the encoder ISR and must be safe there.
    bool (*read_pin)(void*, std::uint8_t pin, std::int32_t& value) = nullptr;
    bool (*attach_interrupt)(void*, std::uint8_t pin, InputInterrupt, std::uint8_t mode) = nullptr;
};
enum class PhysicalInputResult { Ok, MissingService, ServiceFailure, InvalidRingIndex, InvalidPhase };

struct PhysicalButtonState {
    std::int32_t pin = 32;
    std::uint32_t id = 0;
    bool pressed = false;
    std::uint32_t press_count = 0, state = 0, state_date = 0, assertion_line = 0;
};

class PhysicalButton {
public:
    explicit PhysicalButton(PendingEvent<CoreButtonEvent>& destination) noexcept : events(destination) {}
    PhysicalButtonState state;
    PhysicalInputIo io;
    PendingEvent<CoreButtonEvent>& events;

    PhysicalInputResult setup();                       // 0x400d0364
    PhysicalInputResult tick(std::uint32_t now);        // 0x400f06a4
    PhysicalInputResult edge(bool pressed);            // 0x400ee2fc
    PhysicalInputResult transition(std::uint32_t next); // 0x400e5f08
    // 0x400f0670 / 0x400f0568: retain the earlier event if the slot is full.
    void emit(bool long_press, std::uint32_t count) noexcept;
};

class PhysicalEncoder;
struct PhysicalEncoderServices {
    void* context = nullptr;
    // Registration must install the original console actions. It is a required
    // application service; successful no-op registration is not supplied.
    bool (*register_commands)(void*, PhysicalEncoder&) = nullptr;
    bool (*button_event)(void*, const CoreButtonEvent&) = nullptr;
    bool (*rotation_event)(void*, std::int32_t delta) = nullptr;
};

class PhysicalEncoder {
public:
    PhysicalEncoder() noexcept;
    PhysicalEncoder(const PhysicalEncoder&) = delete;
    PhysicalEncoder& operator=(const PhysicalEncoder&) = delete;
    PhysicalInputIo io;
    PhysicalEncoderServices services;
    PendingEvent<CoreButtonEvent> button_events;
    PhysicalButton button;
    std::uint8_t pin_a = 34, pin_b = 35, pin_button = 39;
    std::uint32_t activity_date = 0, total = 0, assertion_line = 0;
    std::uint8_t phase = 0, previous_phase = 0;

    // Original single-producer ISR ring: there is no full flag or overflow
    // prevention. A producer lap makes write == read and loses pending input.
    // Volatile accesses and the target MEMW preserve the original ISR boundary;
    // they do not establish a portable multi-thread synchronization contract.
    volatile std::uint8_t ring[256] = {};
    volatile std::uint16_t write_index = 0;
    std::uint16_t read_index = 0;

    PhysicalInputResult setup();                         // 0x400fb8a4
    ZSERIES_INPUT_IRAM PhysicalInputResult sample(std::uint8_t& value); // 0x40080808
    ZSERIES_INPUT_IRAM PhysicalInputResult interrupt();   // 0x40080828
    PhysicalInputResult tick(std::uint32_t now);          // 0x400f0804
    PhysicalInputResult emit_rotation(std::int32_t delta); // 0x400d198c
    // Console wrappers call these operations without changing total/phase.
    void console_press() noexcept;
    PhysicalInputResult console_rotate(std::int32_t delta);
};

}} // namespace zseries::oem
