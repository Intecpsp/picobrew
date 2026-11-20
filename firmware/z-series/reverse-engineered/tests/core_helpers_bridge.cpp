#include "zseries/core_helpers.hpp"
#include <vector>
using namespace zseries::oem;
namespace {
struct Context { std::uint32_t* words; std::vector<std::uint32_t> effects; std::uint32_t failure; };
std::uint32_t millis(void* p) { return static_cast<Context*>(p)->words[2]; }
bool perform(void* p, const HelperAction& action, std::uint32_t& result) {
    auto& c = *static_cast<Context*>(p);
    c.effects.push_back(static_cast<std::uint32_t>(action.effect)); c.effects.push_back(action.argument);
    if (action.effect == HelperEffect::CanSynchronize) result = c.words[16];
    return c.failure != static_cast<std::uint32_t>(action.effect);
}
bool assertion(void* p, std::uint32_t line) {
    auto& c = *static_cast<Context*>(p);
    c.effects.push_back(11); c.effects.push_back(line);
    if (c.failure == 11) return false;
    c.effects.push_back(10); c.effects.push_back(line);
    ++c.words[3];
    c.effects.push_back(12); c.effects.push_back(200);
    c.words[2] += 200;
    return true;
}
}
extern "C" unsigned core_helpers_case(std::uint32_t* w, std::uint32_t* output,
                                      unsigned* count, unsigned missing, unsigned failure) {
    Context c{w, {}, failure};
    HelperServices services;
    services.context=&c; services.millis=missing==1?nullptr:millis;
    services.perform=missing==2?nullptr:perform; services.assertion=missing==3?nullptr:assertion;
    HelperResult result=HelperResult::Ok;
    if (w[0]==0) {
        RegistrationState state; state.enabled=w[5]!=0;state.pending=w[6]!=0;
        state.deadline=w[7];state.interval_ms=w[8];
        RegistrationObserved observed;observed.backend_ready=w[9]!=0;
        observed.registered=w[10]!=0;observed.expired=w[11]!=0;
        result=tick_registration(state,observed,w[1],services);
        w[5]=state.enabled;w[6]=state.pending;w[7]=state.deadline;
    } else if(w[0]==1) {
        ProgramDownloadState state;state.requested=w[5]!=0;state.active=w[6]!=0;
        ProgramDownloadObserved observed;observed.complete=w[7]!=0;observed.error=w[8]!=0;
        observed.backend_ready=w[9]!=0;observed.uri="https://example.invalid/program";
        result=tick_program_download(state,observed,services);
        w[5]=state.requested;w[6]=state.active;w[7]=observed.complete;w[8]=observed.error;
    } else {
        SessionLogger logger;logger.services=services;logger.state=static_cast<SessionLogState>(w[5]);
        logger.changed_ms=w[6];logger.deadline=w[7];logger.error=w[8]!=0;
        logger.stop_requested=w[9]!=0;logger.resume_pending=w[10]!=0;logger.retained_session_id=w[11];
        logger.observed.backend_state=w[12];logger.observed.created_id=w[13];logger.observed.session_id=w[14];
        logger.observed.resumed_id=w[15];logger.observed.step_name="comparison step";
        if(w[0]==2)result=logger.tick(w[1]);
        else if(w[0]==3)result=logger.transition(static_cast<SessionLogState>(w[17]));
        else result=logger.begin();
        w[5]=static_cast<std::uint32_t>(logger.state);w[6]=logger.changed_ms;w[7]=logger.deadline;
        w[8]=logger.error;w[9]=logger.stop_requested;w[10]=logger.resume_pending;w[11]=logger.retained_session_id;
    }
    w[4]=static_cast<std::uint32_t>(result);*count=static_cast<unsigned>(c.effects.size());
    for(unsigned i=0;i<*count;++i)output[i]=c.effects[i];
    return w[4];
}
