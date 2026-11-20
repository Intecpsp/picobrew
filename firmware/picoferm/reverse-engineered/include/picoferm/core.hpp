// Reconstructed from PicoFerm 0.2.7, not original vendor source.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace picoferm { namespace oem {

// Application diagnostics cross this explicit boundary. The two original
// assertion helpers only call the Arduino yield function twice and return.
// They do not abort, increment a counter or delay for a fixed interval.
struct Ports {
    virtual ~Ports() = default;
    virtual void yield() = 0;
    virtual void text(const char* scope, const char* key, const char* value) = 0;
    virtual void masked_text(const char* scope,const char* key,const char* value);
    virtual void integer(const char* scope, const char* key, std::uint32_t value) = 0;
    virtual void number(const char* scope, const char* key, float value) = 0;
    virtual void boolean(const char* scope, const char* key, bool value) = 0;
    virtual void digital_write(unsigned pin, bool value) = 0;
    virtual std::uint32_t digital_read(unsigned pin) = 0;
    // The SDK EEPROM dirty-buffer/flash commit is a platform boundary. The
    // caller supplies the exact 236-byte packed application record.
    virtual void persist(const std::uint8_t* bytes, std::size_t size) = 0;
};

void diagnostic_assertion(Ports& ports); // 0x40202d74
void diagnostic_warning(Ports& ports);   // 0x40202d8c

enum class SessionState : std::uint32_t { Undefined = 0, Idle = 1, Fermenting = 2 };
struct Session {
    SessionState state = SessionState::Undefined;
    std::uint32_t error_flags = 0;
    bool has_fermentation_error() const noexcept { return error_flags != 0; }
};
// 0x40204140. Non-null NUL-terminated inputs are the original returning domain.
// A null input is explicitly rejected here after the returning assertion;
// the original proceeds to an invalid dereference and is not replayed as a
// successful result. Extra commas and invalid suffixes retain OEM semantics.
bool parse_session(Session& session, const char* response, Ports& ports);

struct CalibrationStatus {
    bool error = false;
    bool can_calibrate = false;
    const char* message = "";
};
struct Measurement { float temperature; float pressure; };

class PersistentData {
public:
    static constexpr std::size_t size = 236;
    static constexpr std::size_t log_capacity = 12;
    static constexpr std::size_t pressure_offset_position = 0x65;
    static constexpr std::size_t cookie_position = 0x69;
    static constexpr std::size_t has_offset_position = 0x6d;
    static constexpr std::size_t slope_position = 0x6e;
    static constexpr std::size_t version_position = 0x72;
    static constexpr std::size_t log_start_position = 0x76;
    static constexpr std::size_t log_count_position = 0x8b;
    static constexpr std::size_t logs_position = 0x8c;
    std::array<std::uint8_t, size> bytes = {{0}};

    // Explicit little-endian accesses also support the unaligned OEM fields.
    std::uint32_t word(std::size_t offset) const noexcept;
    void word(std::size_t offset, std::uint32_t value) noexcept;
    void reset(Ports& ports);            // 0x4020381c
    void migrate(Ports& ports);          // 0x40203700
    void sanitize(Ports& ports);         // 0x402039fc
    CalibrationStatus calibration(Ports& ports) const; // 0x40203914
    void append(Measurement measurement, Ports& ports); // 0x40203d3c
    // Owned bounds check: callers must not use an invalid physical index.
    bool measurement(std::size_t physical_index, Measurement& result) const noexcept;
};

struct LogIterator {
    const PersistentData& data;
    std::uint32_t processed = 0;
    std::uint32_t index = 0;
    explicit LogIterator(const PersistentData& record) noexcept
        : data(record), index(record.bytes[PersistentData::log_start_position]) {}
    bool done() const noexcept; // 0x4020da98
    void advance() noexcept;    // 0x40203b60
};

std::uint8_t read_button(Ports& ports); // 0x40202e30: GPIO15 latch around GPIO13

// Recovered ADC event and persistence slice of the application, with the OEM
// numeric state IDs retained until the full controller is reconstructed.
// State 8 calibrates; state 4 appends measurements; other states only report.
struct Measurements {
    PersistentData data;
    std::uint32_t machine_state = 0;
    SessionState session_state = SessionState::Undefined;
    void persist(Ports& ports); // 0x402050b4
    void on_adc(float temperature, std::uint32_t raw_pressure, Ports& ports); // 0x40205114
};

}} // namespace picoferm::oem
