// Z119 SerialLogger and concrete Core CSV formatters (CoreHelpers.h / Core.h).
#pragma once
#include "zseries/auxiliary_sensors.hpp"
#include "zseries/console.hpp"
#include "zseries/pico_still_sensors.hpp"
#include "zseries/program_runtime.hpp"
#include <array>

namespace zseries { namespace oem {
enum class SerialLogResult { Ok, MissingService, ServiceFailure, InvalidInput };
enum class SerialLogMessage {
    Header, Status, Enabled, Interval, BufferTruncated, DroppedBytes, IntervalBelowMinimum
};
struct SerialLogServices {
    void* context=nullptr;
    bool (*assertion)(void*,const char* file,std::uint32_t line,const char* expression)=nullptr;
    // Header/Status borrow text for the call; Enabled supplies True/False and
    // value 0/1; Interval carries milliseconds. BufferTruncated carries its
    // original source line; DroppedBytes carries its count; minimum uses 0x75.
    bool (*emit)(void*,SerialLogMessage,const char* text,std::uint32_t value)=nullptr;
};
struct CsvBuffer {
    static constexpr std::size_t Capacity=384;
    std::array<char,Capacity+1> text{};
    std::size_t length=0;
    bool truncated=false,frozen=false;
    void clear() noexcept;
    SerialLogResult append(const char*,const SerialLogServices&);
    SerialLogResult append(const void*,std::size_t,const SerialLogServices&);
    SerialLogResult format(const SerialLogServices&,const char* format,...);
};
struct CsvSource {
    void* context=nullptr;
    SerialLogResult (*header)(void*,CsvBuffer&,const SerialLogServices&)=nullptr;
    SerialLogResult (*data)(void*,CsvBuffer&,const SerialLogServices&)=nullptr;
};
// The referenced production states remain owned by application composition.
struct CoreCsvSources {
    const HeaterState* heater=nullptr;
    const HeaterSensors* temperatures=nullptr;
    const AuxiliarySensors* auxiliary=nullptr;
    const z_msp_state* msp=nullptr;
    const ProgramRuntimeState* runtime=nullptr;
    const PicoStillSamples* still=nullptr;
    float reference_pressure_pa=101325.0f;
};
SerialLogResult core_csv_header(void*,CsvBuffer&,const SerialLogServices&); // 0x400f5d08
SerialLogResult core_csv_data(void*,CsvBuffer&,const SerialLogServices&); // 0x400f6064
SerialLogResult still_csv_data(const PicoStillSamples&,CsvBuffer&,const SerialLogServices&); // 0x400f5fcc
CsvSource core_csv_source(CoreCsvSources&) noexcept;

class SerialLogger {
public:
    SerialLogServices services;
    CsvSource source;
    CsvBuffer buffer;
    std::uint32_t minimum_interval_ms=200,deadline=0,interval_ms=1000;
    bool enabled=false,ever_truncated=false;
    SerialLogResult header(); // 0x400efa4c
    SerialLogResult regenerate(); // 0x400efb04
    SerialLogResult out(); // 0x400efb34
    SerialLogResult enable(bool); // 0x400efaac
    SerialLogResult tick(std::uint32_t now); // 0x400efcfc; strict deadline < now
    SerialLogResult information(); // 0x400efb94
    SerialLogResult set_interval(const char*); // 0x400d76ec; signed atoi -> uint32
};
constexpr const char* SerialLoggerConsoleName="Serial Logger";
ConsoleResult register_serial_logger_console(ConsoleShell&,SerialLogger&); // 0x400fae40
}} // namespace zseries::oem
