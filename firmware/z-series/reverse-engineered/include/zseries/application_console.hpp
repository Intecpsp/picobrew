// Persistent Core console ownership; platform-only commands remain explicit.
#pragma once
#include "zseries/console.hpp"
#include "zseries/core_startup.hpp"
#include "zseries/buzzer.hpp"
#include "zseries/eeprom.hpp"
#include <cstdint>
#include <memory>

namespace zseries { namespace oem {
class CoreApplication;
struct HttpServices;
struct ApplicationConsoleHooks {
    void* context = nullptr;
    // Platform/Head callbacks and diagnostic self-test clusters reach this
    // binding. The analysis manifest records which owner implements each.
    // The original callback address is a stable, source-evidenced identifier.
    // Unknown callbacks and failures must return false; they cannot be no-ops.
    bool (*invoke)(void*, std::uint32_t original_callback, const char* argument) = nullptr;
    // Arduino Print output without a diagnostic prefix (MSP boot response).
    bool (*write)(void*, const char* bytes, std::size_t length) = nullptr;
    // Paired ScopeLogger around the Runtime instruction-stack dump.
    bool (*scope)(void*, bool begin, const char* tag, const char* file,
                  std::uint32_t line, const char* name) = nullptr;
    // Original diagnostic TestScope has distinct prefixes, assertion-count
    // accounting and stack-water checks; an ordinary trace is insufficient.
    bool (*test_scope)(void*, bool begin, const char* tag, const char* file,
                       std::uint32_t line, const char* name) = nullptr;
    // Concrete environment for the original diagnostic suites. Values are
    // read at their original call sites; tests must not invent clock, stack
    // or assertion-counter defaults when a platform binding is absent.
    std::uint32_t (*millis)(void*) = nullptr;
    std::uint32_t (*assertion_count)(void*) = nullptr;
    std::uint32_t (*stack_high_water)(void*) = nullptr;
    bool (*yield)(void*) = nullptr;
    bool (*delay_ms)(void*, std::uint32_t) = nullptr;
    // Original 0x40136e8c is esp_task_wdt_reset, also used between tests.
    bool (*watchdog_reset)(void*) = nullptr;
    // Original 0x400ee634 renders the test category/result on the Head display.
    bool (*test_message)(void*, const char*) = nullptr;
    // Dedicated connection for diagnostic requests; production services keep
    // their owned clients and receiver lifetimes during a console test.
    const HttpServices* http_test_services = nullptr;
};
class ApplicationConsole {
public:
    ApplicationConsole(CoreApplication&, const ConsoleServices&) noexcept;
    ~ApplicationConsole();
    ApplicationConsole(const ApplicationConsole&) = delete;
    ApplicationConsole& operator=(const ApplicationConsole&) = delete;
    ApplicationConsoleHooks hooks;
    ConsoleResult register_component(CoreSetupStage);
    ConsoleResult register_display();
    ConsoleResult register_heater();
    ConsoleResult register_max11613();
    ConsoleResult dispatch(const char*);
    // Internal aggregate callbacks can reach the original body directly.
    // This does not reinterpret a callback address as executable memory.
    ConsoleResult invoke_original(std::uint32_t original_callback, const char* argument = "");
    ConsoleShell* root() noexcept;
    // These preserve the component's actual registration order during setup.
    bool begin_buzzer_console(const char* name);
    bool add_buzzer_command(Buzzer&, const BuzzerCommandDefinition&);
    EepromServices eeprom_services() noexcept;
private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};
}} // namespace zseries::oem
