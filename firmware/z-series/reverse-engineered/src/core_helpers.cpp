#include "zseries/core_helpers.hpp"

namespace zseries { namespace oem {
namespace {
HelperResult perform(const HelperServices& services, HelperEffect effect,
                     std::uint32_t argument = 0, const char* text = nullptr) {
    if (!services.perform) return HelperResult::MissingService;
    HelperAction action(effect); action.argument = argument; action.text = text;
    std::uint32_t ignored = 0;
    return services.perform(services.context, action, ignored)
        ? HelperResult::Ok : HelperResult::ServiceFailure;
}
}

HelperResult tick_registration(RegistrationState& state, const RegistrationObserved& observed,
                               std::uint32_t now, const HelperServices& services) {
    if (!state.enabled) return HelperResult::Ok;
    if (!services.perform || !services.assertion) return HelperResult::MissingService;
    if (observed.backend_ready && state.deadline < now) {
        if (observed.registered && !services.assertion(services.context, 0xdc))
            return HelperResult::ServiceFailure;
        const auto result = perform(services, HelperEffect::RegistrationRequest);
        if (result != HelperResult::Ok) return result;
        state.pending = true;
        state.deadline = state.interval_ms + now;
        return HelperResult::Ok;
    }
    if (!state.pending || !observed.backend_ready) return HelperResult::Ok;
    state.pending = false;
    if (observed.registered || observed.expired) {
        const auto result = perform(services, HelperEffect::Diagnostic, 0xca);
        if (result != HelperResult::Ok) return result;
        state.enabled = false;
    }
    if (observed.registered) return HelperResult::Ok;
    return perform(services, HelperEffect::EmitCoreEvent, static_cast<std::uint32_t>(
        observed.expired ? CoreEventType::RegistrationTokenExpired : CoreEventType::RegistrationNewToken));
}

HelperResult tick_program_download(ProgramDownloadState& state, ProgramDownloadObserved& observed,
                                  const HelperServices& services) {
    if (!observed.backend_ready || !state.requested) {
        if (state.active && observed.complete) state.active = false;
        return HelperResult::Ok;
    }
    if (!services.perform) return HelperResult::MissingService;
    observed.complete = false;
    observed.error = false;
    auto result = perform(services, HelperEffect::ConfigureProgramDownload, 0, observed.uri);
    if (result != HelperResult::Ok) return result;
    result = perform(services, HelperEffect::StartProgramDownload);
    if (result != HelperResult::Ok) return result;
    state.requested = false;
    state.active = true;
    return HelperResult::Ok;
}

bool SessionLogger::available() const noexcept {
    return services.millis && services.perform && services.assertion;
}
HelperResult SessionLogger::effect(HelperEffect value, std::uint32_t argument,
                                   const char* text, std::uint32_t* result) {
    if (!services.perform) return HelperResult::MissingService;
    HelperAction action(value); action.argument = argument; action.text = text;
    std::uint32_t returned = 0;
    if (!services.perform(services.context, action, returned)) return HelperResult::ServiceFailure;
    if (result) *result = returned;
    return HelperResult::Ok;
}
HelperResult SessionLogger::diagnostic(std::uint32_t line) {
    return effect(HelperEffect::Diagnostic, line);
}

HelperResult SessionLogger::transition(SessionLogState next) {
    if (!available()) return HelperResult::MissingService;
    if (state == next) {
        if (!services.assertion(services.context, 0x14a)) return HelperResult::ServiceFailure;
        // The original rechecks state after its returning assertion.
        if (state == next) return HelperResult::Ok;
    }
    const auto transition_ms = services.millis(services.context);
    changed_ms = transition_ms;
    state = next;
    std::uint32_t duration = 20000, ready = 0;
    HelperResult result = HelperResult::Ok;
    switch (next) {
    case SessionLogState::Creating:
        error = false; stop_requested = false;
        result = effect(HelperEffect::CreateSession);
        break;
    case SessionLogState::Waiting:
        duration = 30000;
        break;
    case SessionLogState::Logging:
        result = effect(HelperEffect::CanSynchronize, 0, nullptr, &ready);
        if (result != HelperResult::Ok) return result;
        if (!ready) return diagnostic(0x167);
        error = false;
        result = effect(HelperEffect::LogSession, 0, observed.step_name);
        break;
    case SessionLogState::Ending:
        result = effect(HelperEffect::CanSynchronize, 0, nullptr, &ready);
        if (result != HelperResult::Ok) return result;
        if (!ready) return diagnostic(0x172);
        error = false;
        result = effect(HelperEffect::EndSession);
        break;
    default:
        return static_cast<std::uint32_t>(next) == 5 ? diagnostic(0x17b) : HelperResult::Ok;
    }
    if (result != HelperResult::Ok) return result;
    deadline = transition_ms + duration;
    return HelperResult::Ok;
}

HelperResult SessionLogger::begin() {
    if (!available()) return HelperResult::MissingService;
    if (state != SessionLogState::Idle) return diagnostic(0x1a9);
    std::uint32_t ready = 0;
    auto result = effect(HelperEffect::CanSynchronize, 0, nullptr, &ready);
    if (result != HelperResult::Ok) return result;
    if (!ready) {
        result = diagnostic(0x1ab);
        if (result != HelperResult::Ok) return result;
        error = true;
        return HelperResult::Ok;
    }
    auto next = SessionLogState::Creating;
    if (resume_pending) {
        result = diagnostic(0x1ae);
        if (result != HelperResult::Ok) return result;
        retained_session_id = observed.resumed_id;
        resume_pending = false;
        next = SessionLogState::Waiting;
    }
    return transition(next);
}

HelperResult SessionLogger::tick(std::uint32_t now) {
    if (!available()) return HelperResult::MissingService;
    switch (state) {
    case SessionLogState::Creating:
        if (observed.backend_state == 0) {
            retained_session_id = observed.created_id;
            if (observed.session_id != retained_session_id &&
                !services.assertion(services.context, 0x1c1)) return HelperResult::ServiceFailure;
            if (observed.session_id != 0) return transition(SessionLogState::Waiting);
            const auto result = diagnostic(0x1c3);
            if (result != HelperResult::Ok) return result;
        } else {
            if (now <= deadline) return HelperResult::Ok;
            const auto result = diagnostic(0x1ca);
            if (result != HelperResult::Ok) return result;
        }
        error = true;
        return transition(SessionLogState::Idle);
    case SessionLogState::Waiting:
        if (stop_requested) return transition(SessionLogState::Ending);
        return now <= deadline ? HelperResult::Ok : transition(SessionLogState::Logging);
    case SessionLogState::Logging:
        if (observed.backend_state != 0) {
            if (now <= deadline) return HelperResult::Ok;
            auto result = diagnostic(0x1d8);
            if (result != HelperResult::Ok) return result;
            result = effect(HelperEffect::CancelSessionClient);
            if (result != HelperResult::Ok) return result;
        }
        return transition(stop_requested ? SessionLogState::Ending : SessionLogState::Waiting);
    case SessionLogState::Ending:
        if (observed.backend_state != 0) {
            if (now <= deadline) return HelperResult::Ok;
            const auto result = diagnostic(0x1e0);
            if (result != HelperResult::Ok) return result;
            error = true;
        }
        return transition(SessionLogState::Idle);
    default:
        return HelperResult::Ok;
    }
}

}} // namespace zseries::oem
