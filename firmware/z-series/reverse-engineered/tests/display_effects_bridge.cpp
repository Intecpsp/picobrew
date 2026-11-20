#include "zseries/display_effects.hpp"
#include <algorithm>
#include <cstring>
#include <vector>
using namespace zseries::oem;
namespace {
struct Trace {
    std::vector<std::uint8_t> bytes;
    std::uint32_t flushes = 0, delays = 0;
    void word(std::uint32_t v) { for (unsigned i = 0; i < 4; ++i) bytes.push_back(v >> (i * 8)); }
};
void gpio(void* p, std::uint8_t pin, std::uint8_t value) {
    auto& t = *static_cast<Trace*>(p); t.bytes.insert(t.bytes.end(), {1, pin, value});
}
void byte(void* p, std::uint8_t value) {
    auto& t = *static_cast<Trace*>(p); t.bytes.insert(t.bytes.end(), {2, value});
}
void bytes(void* p, const std::uint8_t* data, std::size_t count) {
    auto& t = *static_cast<Trace*>(p); t.bytes.push_back(3); t.word(count);
    t.bytes.insert(t.bytes.end(), data, data + count); ++t.flushes;
}
void wait(void* p, std::uint32_t ms) {
    auto& t = *static_cast<Trace*>(p); t.bytes.push_back(4); t.word(ms); ++t.delays;
}
void pattern(std::vector<std::uint8_t>& data, unsigned seed) {
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = ((i * 37 + seed) ^ (i >> 3)) & 255;
}
}

extern "C" int display_effect_test(unsigned operation, const std::uint32_t* args,
    const std::uint32_t* times, std::size_t time_count, std::uint8_t* pixels,
    std::uint8_t* transfer, std::uint32_t* values, std::uint8_t* events, std::size_t capacity) {
    DisplayState state; std::copy(pixels, pixels + 4096, state.pixels.begin());
    std::copy(transfer, transfer + 4096, state.transfer.begin());
    Trace trace; DisplayIo io; io.context = &trace; io.digital_write = gpio; io.spi_byte = byte; io.spi_bytes = bytes;
    DisplayDelay delay; delay.context = &trace; delay.milliseconds = wait;
    DisplayEffectResult result = DisplayEffectResult::Ok;
    const auto s = [args](unsigned n) { std::int32_t v; std::memcpy(&v, &args[n], 4); return v; };
    DisplayAnimationState motion;
    std::vector<std::uint8_t> data[3]; DisplayBitmap bitmaps[3]; const DisplayBitmap* frames[3];
    const unsigned width = args[16], height = args[17], stride = args[18], seed = args[19];
    for (unsigned i = 0; i < 3; ++i) {
        if (!width || !height || stride * height > 65536) return -1;
        data[i].resize(stride * height); pattern(data[i], seed + 11 + i * 37);
        bitmaps[i] = {width, height, stride, {0, 0, static_cast<std::int32_t>(width - 1), static_cast<std::int32_t>(height - 1)}, data[i].data(), data[i].size()};
        frames[i] = args[20] & (1u << i) ? nullptr : &bitmaps[i];
    }
    DisplayAnimation animation{args[0], args[1], args[2], frames};
    switch (operation) {
    case 0: result = display_vertical_gradient(state, {s(0), s(1), s(2), s(3)}, args[5]); break;
    case 1: result = display_horizontal_gradient(state, {s(0), s(1), s(2), s(3)}, args[4], args[5]); break;
    case 2: result = display_caret(state, {s(0), s(1)}, s(2), args[3], args[4]); break;
    case 3:
        if (args[2] > 3) return -2;
        motion.animation = args[6] ? &animation : nullptr; motion.cycle = args[3]; motion.frame = args[4];
        motion.deadline = args[5]; motion.dirty = args[7]; motion.point = {s(8), s(9)};
        for (std::size_t i = 0; i < time_count; ++i) {
            result = display_animation_tick(state, motion, io, times[i]); if (result != DisplayEffectResult::Ok) break;
        }
        break;
    case 4: result = display_slide(state, io, delay, bitmaps[0], args[3], {s(4), s(5)}, s(6), args[7]); break;
    case 5: result = display_slide_aligned(state, io, delay, bitmaps[0], args[3], args[8], args[9], s(6), args[7]); break;
    default: return -3;
    }
    if (trace.bytes.size() > capacity) return -4;
    std::copy(state.pixels.begin(), state.pixels.end(), pixels); std::copy(state.transfer.begin(), state.transfer.end(), transfer);
    std::copy(trace.bytes.begin(), trace.bytes.end(), events);
    values[0] = trace.bytes.size(); values[1] = motion.animation ? 1 : 0; values[2] = motion.cycle;
    values[3] = motion.frame; values[4] = motion.deadline; values[5] = motion.dirty;
    values[6] = trace.flushes; values[7] = trace.delays; values[8] = static_cast<unsigned>(result);
    return 0;
}
