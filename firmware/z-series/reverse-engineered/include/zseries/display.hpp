// Z119 DisplayTypes / SSD1325 graphics, recovered from the Z application.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace zseries { namespace oem {

struct DisplayPoint { std::int32_t x, y; };
struct DisplayRect { std::int32_t left, top, right, bottom; };
constexpr std::size_t DisplayWidth = 128;
constexpr std::size_t DisplayHeight = 64;
constexpr std::size_t DisplayBufferBytes = DisplayWidth * DisplayHeight / 2;
constexpr DisplayRect DisplayBounds{0, 0, 127, 63};

std::uint32_t display_rect_width(const DisplayRect&) noexcept;
std::uint32_t display_rect_height(const DisplayRect&) noexcept;
std::int32_t display_rect_center_y(const DisplayRect&) noexcept;
bool display_rect_valid(const DisplayRect&) noexcept;
bool display_rect_intersects(const DisplayRect&, const DisplayRect&) noexcept;
bool display_rect_contains(const DisplayRect&, const DisplayRect&) noexcept;
bool display_rect_intersection(const DisplayRect&, const DisplayRect&, DisplayRect&) noexcept;
void display_rect_translate(DisplayRect&, std::int32_t x, std::int32_t y) noexcept;

// Original descriptors have width, height, row stride, inclusive bounds,
// a packed 4-bit pixel pointer and its byte count. Source rows run from the
// rectangle's high Y to low Y; the framebuffer uses the opposite row order.
struct DisplayBitmap {
    std::uint32_t width, height, row_bytes;
    DisplayRect bounds;
    const std::uint8_t* pixels;
    std::size_t bytes;
};

struct DisplayIo {
    void* context = nullptr;
    void (*pin_mode)(void*, std::uint8_t pin, std::uint8_t mode) = nullptr;
    void (*digital_write)(void*, std::uint8_t pin, std::uint8_t value) = nullptr;
    void (*spi_byte)(void*, std::uint8_t value) = nullptr;
    void (*spi_bytes)(void*, const std::uint8_t*, std::size_t) = nullptr;
};

// The original setup ends by reporting information and installing its console
// commands. These are required integration effects, not successful defaults.
struct DisplaySetupServices {
    void* context = nullptr;
    void (*report_information)(void*) = nullptr;
    void (*register_commands)(void*) = nullptr;
};

enum class DisplayResult { Ok, MissingIo, MissingSetupServices, InvalidRectangle, InvalidBitmap };
struct DisplayState {
    std::array<std::uint8_t, DisplayBufferBytes> pixels{};
    std::array<std::uint8_t, DisplayBufferBytes> transfer{};
    std::uint8_t chip_select = 0;
    std::uint8_t data_command = 2;
    std::uint8_t reset = 4;
};

void display_clear(DisplayState&) noexcept;
DisplayResult display_command(DisplayState&, const DisplayIo&, std::uint8_t) noexcept;
DisplayResult display_flush(DisplayState&, const DisplayIo&) noexcept;
DisplayResult display_clear_and_flush(DisplayState&, const DisplayIo&) noexcept;
DisplayResult display_invert(DisplayState&, const DisplayIo&, bool) noexcept;
DisplayResult display_setup(DisplayState&, const DisplayIo&, const DisplaySetupServices&) noexcept;
DisplayResult display_rectangle(DisplayState&, const DisplayRect&, std::uint8_t color, bool filled) noexcept;
DisplayResult display_bitmap(DisplayState&, const DisplayPoint&, const DisplayBitmap&,
                             const DisplayRect& region, bool invert = false) noexcept;

}} // namespace zseries::oem
