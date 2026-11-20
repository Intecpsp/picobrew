// Z119 Head Core-event dispatch and state presentation.
#pragma once
#include "zseries/head_flow.hpp"
#include "zseries/program_list.hpp"
#include "zseries/ui_catalog.hpp"

namespace zseries { namespace oem {

// Actual presentation/helper boundaries called by the recovered Head body.
// Scalar fields are documented by the cases below; text/event pointers are
// borrowed for the duration of perform(). Queued adapters must own their copies.
enum class HeadCoreEffect : std::uint8_t {
    ResetScreen, AppendCommand, AppendCommandText, InstructionMenu,
    DispatchCoreCommand, BootLogo, WaitingMessage, RegistrationScreen,
    NetworkConfiguration, ProgressScreen, ResetUi, ClearDisplay, HomeScreen,
    SessionStartScreen, RunningScreen, SystemErrorScreen, LoadingErrorScreen,
    NetworkErrorScreen, RecipeListScreen, ProgramLoadedScreen, BoilerResultScreen,
    ExpiredRegistrationScreen, AppendLine, ErrorScrollScreen, CheckRinse
};
struct HeadCoreAction {
    HeadCoreEffect effect;
    std::uint32_t first = 0, second = 0, third = 0;
    const char* title = nullptr;
    const char* text = nullptr;
    const CoreEvent* event = nullptr;
    explicit HeadCoreAction(HeadCoreEffect value) noexcept : effect(value) {}
};
struct HeadCoreServices {
    void* context = nullptr;
    // CheckRinse returns the original virtual service result in result.
    bool (*perform)(void*, const HeadCoreAction&, std::uint32_t& result) = nullptr;
    std::uint32_t (*millis)(void*) = nullptr;
    // Must perform the OEM diagnostic counter/log/200-ms delay and return.
    bool (*assertion)(void*, std::uint32_t original_line) = nullptr;
};
struct HeadCoreObserved {
    RuntimeStateId runtime = RuntimeStateId::Off;
    std::int32_t download_received = 0, download_total = 0;
    std::int32_t interrupted_sessions = 0, maintenance_count = 0;
    std::uint8_t maintenance_limit = 0;
    std::uint32_t maintenance_date = 0;
    const char* registration_token = nullptr;
    const char* network_detail = nullptr;
};
struct HeadCoreState {
    HeadTimer transient;
    std::uint32_t power_prompt_duration = 30000;
    bool update_progress_visible = false, display_dirty = false;
    std::uint32_t assertion_line = 0;
};
enum class HeadCoreResult { Ok, MissingService, ServiceFailure, AssertionFailure };
class HeadCoreEvents {
public:
    HeadCoreState state;
    HeadCoreObserved observed;
    HeadCoreServices services;
    LanguageType language = LanguageType::English;
    // 0x401095b4. The original take clears pending and reads retained data even
    // when empty (with a diagnostic); HeadFlow normally calls only if pending.
    HeadCoreResult consume(CoreEventMailbox&);
    HeadCoreResult handle(const CoreEvent&);
    // 0x40106e84: transition event argument=current, second_argument=previous.
    HeadCoreResult transition(const CoreEvent&);
private:
    HeadCoreResult perform(const HeadCoreAction&, std::uint32_t* result = nullptr);
    HeadCoreResult effect(HeadCoreEffect, std::uint32_t first = 0,
                          std::uint32_t second = 0, std::uint32_t third = 0);
    HeadCoreResult assertion(std::uint32_t);
    HeadCoreResult waiting(std::uint32_t title, const char* detail,
                           bool animated, std::uint32_t counter = 0);
};
// 0x400f41ec: the Head consumes Still transitions even when they need no screen
// change. A later HeadFlow check selects the Still screen for WaitStillReady.
CoreStillEvent consume_head_still_event(PendingEvent<CoreStillEvent>&) noexcept;

}} // namespace zseries::oem
