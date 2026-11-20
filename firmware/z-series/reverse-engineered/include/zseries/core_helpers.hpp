// Original Z119 registration, recipe-download and session-log controllers.
#pragma once
#include "zseries/enums.hpp"
#include <cstdint>

namespace zseries { namespace oem {

enum class HelperEffect : std::uint32_t {
    RegistrationRequest = 1, EmitCoreEvent, ConfigureProgramDownload,
    StartProgramDownload, CanSynchronize, CreateSession, LogSession,
    EndSession, CancelSessionClient, Diagnostic
};
struct HelperAction {
    HelperEffect effect;
    std::uint32_t argument = 0;
    const char* text = nullptr;
    explicit HelperAction(HelperEffect value) noexcept : effect(value) {}
};
struct HelperServices {
    void* context = nullptr;
    std::uint32_t (*millis)(void*) = nullptr;
    // Adapter completion is distinct from the OEM boolean returned by
    // CanSynchronize. Other call results are ignored by the original helpers.
    bool (*perform)(void*, const HelperAction&, std::uint32_t& result) = nullptr;
    // Assert::fail logs, increments the diagnostic counter, delays 200 ms,
    // and RETURNS. Its caller continues when this required binding succeeds.
    bool (*assertion)(void*, std::uint32_t source_line) = nullptr;
};
enum class HelperResult { Ok, MissingService, ServiceFailure };

struct RegistrationState {
    std::uint32_t interval_ms = 0, deadline = 0;
    bool enabled = false, pending = false;
};
struct RegistrationObserved {
    bool backend_ready = false, registered = false, expired = false;
};
// Original CoreHelpers.h 0x400fb100. The backend state must be refreshed by
// the application before each tick. Deadlines use the OEM absolute comparison.
HelperResult tick_registration(RegistrationState&, const RegistrationObserved&,
                               std::uint32_t now, const HelperServices&);

struct ProgramDownloadState { bool requested = false, active = false; };
struct ProgramDownloadObserved {
    bool backend_ready = false, complete = false, error = false;
    const char* uri = nullptr;
};
// Original 0x4010ca58. The application's ConfigureProgramDownload binding
// owns the real HTTP receiver; its lifetime extends through all HTTP ticks.
HelperResult tick_program_download(ProgramDownloadState&, ProgramDownloadObserved&,
                                  const HelperServices&);

enum class SessionLogState : std::uint32_t { Idle, Creating, Waiting, Logging, Ending };
struct SessionLogObserved {
    std::uint32_t backend_state = 0, created_id = 0, session_id = 0, resumed_id = 0;
    const char* step_name = nullptr;
};
class SessionLogger {
public:
    SessionLogState state = SessionLogState::Idle;
    std::uint32_t changed_ms = 0, deadline = 0, retained_session_id = 0;
    bool error = false, stop_requested = false, resume_pending = false;
    SessionLogObserved observed;
    HelperServices services;
    // Original 0x4011c7c8, 0x4011cadc and 0x4011c928, respectively.
    HelperResult transition(SessionLogState);
    HelperResult begin();
    HelperResult tick(std::uint32_t now);
private:
    HelperResult effect(HelperEffect, std::uint32_t = 0, const char* = nullptr,
                        std::uint32_t* result = nullptr);
    HelperResult diagnostic(std::uint32_t line);
    bool available() const noexcept;
};

}} // namespace zseries::oem
