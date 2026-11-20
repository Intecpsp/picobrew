#include "zseries/head_flow.hpp"
#include <algorithm>
#include <cstring>
#include <vector>
using namespace zseries::oem;
namespace {
struct Context {
    std::uint32_t pending = 0, now = 0, increment = 0, clocks = 0;
    std::vector<std::uint32_t> events;
    void record(std::uint32_t a, std::uint32_t b = 0, std::uint32_t c = 0, std::uint32_t d = 0) {
        events.insert(events.end(), {a, b, c, d});
    }
};
std::uint32_t clock(void* pointer) {
    auto& ctx = *static_cast<Context*>(pointer); const std::uint32_t value = ctx.now + ctx.clocks * ctx.increment;
    ++ctx.clocks; ctx.record(200, value); return value;
}
bool pending(void* pointer, HeadEventKind kind) { return static_cast<Context*>(pointer)->pending & (1u << static_cast<unsigned>(kind)); }
bool event(void* pointer, HeadEventKind kind) {
    auto& ctx = *static_cast<Context*>(pointer); ctx.pending &= ~(1u << static_cast<unsigned>(kind));
    ctx.record(100 + static_cast<unsigned>(kind)); return true;
}
bool effect(void* pointer, const HeadAction& action) {
    static_cast<Context*>(pointer)->record(static_cast<unsigned>(action.effect), action.argument, action.second, action.third); return true;
}
std::int32_t signed_bits(std::uint32_t value) { std::int32_t result; std::memcpy(&result, &value, 4); return result; }
}
extern "C" int head_flow_test(unsigned operation, const std::uint32_t* args,
                               std::uint32_t* values, std::uint32_t* events, std::size_t capacity) {
    HeadFlow head; Context ctx; ctx.pending = args[6]; ctx.now = args[14]; ctx.increment = args[15];
    head.services.context = &ctx; head.services.millis = clock; head.services.pending = pending;
    head.services.handle_event = event; head.services.perform = effect;
    head.observed.core = static_cast<CoreStateId>(args[0]); head.observed.runtime = static_cast<RuntimeStateId>(args[1]);
    head.observed.instruction = static_cast<OpCode>(args[2]); head.observed.downloading = args[3];
    head.observed.downloaded_bytes = signed_bits(args[4]); head.observed.download_bytes = signed_bits(args[5]);
    head.transient_timer.active = args[7]; head.transient_timer.duration = args[8]; head.transient_timer.deadline = args[9];
    head.alarm_timer.active = args[10]; head.alarm_timer.duration = args[11]; head.alarm_timer.deadline = args[12];
    head.alarm_duration_ms = args[13];
    HeadFlowResult result = HeadFlowResult::Ok;
    switch (operation) {
    case 0: result = head.synchronize(); break;
    case 1: result = head.dispatch(static_cast<CoreCommandId>(args[17])); break;
    case 2: result = head.alarm(args[17]); break;
    case 3: result = head.tick(args[16]); break;
    case 4: result = head.start_transient(args[17]); break;
    case 5: result = head.reset_transient(); break;
    default: return -1;
    }
    if (ctx.events.size() > capacity) return -2;
    values[0] = static_cast<unsigned>(result); values[1] = ctx.events.size();
    values[2] = head.transient_timer.active; values[3] = head.transient_timer.duration; values[4] = head.transient_timer.deadline;
    values[5] = head.alarm_timer.active; values[6] = head.alarm_timer.duration; values[7] = head.alarm_timer.deadline;
    values[8] = ctx.pending; values[9] = ctx.clocks;
    std::copy(ctx.events.begin(), ctx.events.end(), events); return 0;
}
