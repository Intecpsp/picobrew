#include "zseries/display.hpp"
#include <algorithm>
#include <cstring>

namespace zseries { namespace oem {
namespace {
std::int32_t signed_bits(std::uint32_t value) noexcept {
    std::int32_t result;
    std::memcpy(&result, &value, sizeof result);
    return result;
}
std::int32_t add_wrapped(std::int32_t a, std::int32_t b) noexcept {
    return signed_bits(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
bool command_io(const DisplayIo& io) noexcept { return io.digital_write && io.spi_byte; }
void command_unchecked(const DisplayIo& io, std::uint8_t value) noexcept {
    // 0x400d1b24 uses literal pins 2/0 even though setup/flush read the
    // object fields. The production constructor selects those same pins.
    io.digital_write(io.context, 2, 0);
    io.digital_write(io.context, 0, 0);
    io.spi_byte(io.context, value);
    io.digital_write(io.context, 0, 1);
}
void pixel(DisplayState& state, std::int32_t x, std::int32_t y, std::uint8_t color) noexcept {
    std::uint8_t& byte = state.pixels[static_cast<std::size_t>(y) * 64 + static_cast<std::size_t>(x) / 2];
    if (x & 1) byte = static_cast<std::uint8_t>((byte & 0xf0) | (color & 0x0f));
    else byte = static_cast<std::uint8_t>((byte & 0x0f) | (color << 4) | (color & 0xf0));
}
} // namespace

std::uint32_t display_rect_width(const DisplayRect& r) noexcept {
    return r.left <= r.right ? static_cast<std::uint32_t>(r.right) + 1u - static_cast<std::uint32_t>(r.left) : 0;
}
std::uint32_t display_rect_height(const DisplayRect& r) noexcept {
    return r.top <= r.bottom ? static_cast<std::uint32_t>(r.bottom) + 1u - static_cast<std::uint32_t>(r.top) : 0;
}
std::int32_t display_rect_center_y(const DisplayRect& r) noexcept {
    return r.top <= r.bottom ? signed_bits(static_cast<std::uint32_t>(r.bottom) + 1u + static_cast<std::uint32_t>(r.top)) / 2 : 0;
}
bool display_rect_valid(const DisplayRect& r) noexcept { return r.left <= r.right && r.top <= r.bottom; }
bool display_rect_intersects(const DisplayRect& a, const DisplayRect& b) noexcept {
    return a.left <= b.right && b.left <= a.right && a.top <= b.bottom && b.top <= a.bottom;
}
bool display_rect_contains(const DisplayRect& a, const DisplayRect& b) noexcept {
    return a.left <= b.left && a.top <= b.top && b.right <= a.right && b.bottom <= a.bottom;
}
bool display_rect_intersection(const DisplayRect& a, const DisplayRect& b, DisplayRect& result) noexcept {
    if (!display_rect_intersects(a, b)) return false;
    DisplayRect clipped{std::max(a.left,b.left),std::max(a.top,b.top),std::min(a.right,b.right),std::min(a.bottom,b.bottom)};
    if (!display_rect_valid(clipped)) return false;
    result = clipped;
    return true;
}
void display_rect_translate(DisplayRect& r, std::int32_t x, std::int32_t y) noexcept {
    r.left=add_wrapped(r.left,x); r.right=add_wrapped(r.right,x);
    r.top=add_wrapped(r.top,y); r.bottom=add_wrapped(r.bottom,y);
}

void display_clear(DisplayState& state) noexcept { state.pixels.fill(0); }
DisplayResult display_command(DisplayState&, const DisplayIo& io, std::uint8_t value) noexcept {
    if (!command_io(io)) return DisplayResult::MissingIo;
    command_unchecked(io,value);
    return DisplayResult::Ok;
}
DisplayResult display_flush(DisplayState& state, const DisplayIo& io) noexcept {
    if (!command_io(io) || !io.spi_bytes) return DisplayResult::MissingIo;
    const std::uint8_t window[]={0x15,0,0x3f,0x75,0,0x3f};
    for (std::uint8_t value:window) command_unchecked(io,value);
    io.digital_write(io.context,state.data_command,1);
    io.digital_write(io.context,state.chip_select,0);
    // Original LOOP at 0x400d1bd2 reverses all 4,096 bytes. Its decompiler
    // output omits the loop back and misleadingly shows a one-byte copy.
    std::reverse_copy(state.pixels.begin(),state.pixels.end(),state.transfer.begin());
    io.spi_bytes(io.context,state.transfer.data(),state.transfer.size());
    io.digital_write(io.context,state.chip_select,1);
    return DisplayResult::Ok;
}
DisplayResult display_clear_and_flush(DisplayState& state,const DisplayIo& io) noexcept {
    if (!command_io(io) || !io.spi_bytes) return DisplayResult::MissingIo;
    display_clear(state);
    return display_flush(state,io);
}
DisplayResult display_invert(DisplayState& state,const DisplayIo& io,bool inverted) noexcept {
    return display_command(state,io,inverted?0xa7:0xa4);
}
DisplayResult display_setup(DisplayState& state,const DisplayIo& io,const DisplaySetupServices& services) noexcept {
    if (!command_io(io) || !io.pin_mode) return DisplayResult::MissingIo;
    if (!services.report_information || !services.register_commands) return DisplayResult::MissingSetupServices;
    io.pin_mode(io.context,state.chip_select,2);
    io.pin_mode(io.context,state.data_command,2);
    io.digital_write(io.context,state.chip_select,1);
    if (state.reset) {
        io.pin_mode(io.context,state.reset,2);
        io.digital_write(io.context,state.reset,1);
    }
    display_clear(state);
    const std::uint8_t setup[]={0xae,0xb3,0x91,0xa8,0x3f,0xa2,0x4c,0xa1,0,0xa0,0x50,0xad,2,
        0x81,0x60,0x86,0xbe,2,0xbf,0x0e,0xa4,0x23,1,0x24,0,0,0x3f,0x3f,0,0xaf};
    for (std::uint8_t value:setup) command_unchecked(io,value);
    services.report_information(services.context);
    services.register_commands(services.context);
    return DisplayResult::Ok;
}

DisplayResult display_rectangle(DisplayState& state,const DisplayRect& rectangle,std::uint8_t color,bool filled) noexcept {
    DisplayRect clipped;
    if (!display_rect_intersects(DisplayBounds,rectangle)) return DisplayResult::Ok;
    if (!display_rect_intersection(DisplayBounds,rectangle,clipped)) return DisplayResult::InvalidRectangle;
    // Border selection occurs after clipping in 0x400e880c. A clipped border
    // therefore draws the new edge, rather than omitting an off-screen edge.
    for (std::int32_t y=clipped.top;y<=clipped.bottom;++y) {
        for (std::int32_t x=clipped.left;x<=clipped.right;++x)
            if (filled || x==clipped.left || x==clipped.right || y==clipped.top || y==clipped.bottom)
                pixel(state,x,y,color);
        // Original 0x400e880c also writes the previous byte's low nibble on
        // interior rows of a one-pixel-wide, even-X outline. At X=0 this is
        // the preceding row's final pixel. Preserve this observable quirk.
        if (!filled && clipped.left==clipped.right && !(clipped.left&1) && y!=clipped.top && y!=clipped.bottom) {
            const std::size_t previous=static_cast<std::size_t>(y)*64+static_cast<std::size_t>(clipped.left)/2-1;
            state.pixels[previous]=static_cast<std::uint8_t>((state.pixels[previous]&0xf0)|(color&15));
        }
    }
    return DisplayResult::Ok;
}

DisplayResult display_bitmap(DisplayState& state,const DisplayPoint& point,const DisplayBitmap& bitmap,
                             const DisplayRect& region,bool invert) noexcept {
    if (!bitmap.pixels || !bitmap.width || !bitmap.height || !bitmap.row_bytes ||
        bitmap.width>static_cast<std::uint32_t>(INT32_MAX) || bitmap.height>static_cast<std::uint32_t>(INT32_MAX) ||
        bitmap.row_bytes<(bitmap.width+1u)/2u ||
        static_cast<std::uint64_t>(bitmap.row_bytes)*bitmap.height>bitmap.bytes)
        return DisplayResult::InvalidBitmap;
    const DisplayRect storage{0,0,static_cast<std::int32_t>(bitmap.width-1),static_cast<std::int32_t>(bitmap.height-1)};
    if (!display_rect_valid(region) || !display_rect_contains(bitmap.bounds,region) || !display_rect_contains(storage,region))
        return DisplayResult::InvalidRectangle;
    const std::int64_t right=static_cast<std::int64_t>(point.x)+display_rect_width(region)-1;
    const std::int64_t bottom=static_cast<std::int64_t>(point.y)+display_rect_height(region)-1;
    if (right>INT32_MAX || bottom>INT32_MAX) return DisplayResult::InvalidRectangle;
    const DisplayRect destination{point.x,point.y,static_cast<std::int32_t>(right),static_cast<std::int32_t>(bottom)};
    DisplayRect clipped;
    if (!display_rect_intersection(DisplayBounds,destination,clipped)) return DisplayResult::Ok;
    const std::uint32_t first_source_x=static_cast<std::uint32_t>(static_cast<std::int64_t>(region.left)+clipped.left-point.x);
    // The aligned, non-inverted OEM copy omits a one-pixel even-X span: its
    // trailing-byte offset is zero, which is also used as the no-tail flag.
    if (!invert && clipped.left==clipped.right && !(clipped.left&1) && !(first_source_x&1))
        return DisplayResult::Ok;
    for (std::int32_t y=clipped.top;y<=clipped.bottom;++y) {
        const std::uint32_t source_y=static_cast<std::uint32_t>(static_cast<std::int64_t>(region.top)+y-point.y);
        for (std::int32_t x=clipped.left;x<=clipped.right;++x) {
            const std::uint32_t source_x=static_cast<std::uint32_t>(static_cast<std::int64_t>(region.left)+x-point.x);
            const std::uint8_t pair=bitmap.pixels[static_cast<std::size_t>(bitmap.height-1-source_y)*bitmap.row_bytes+source_x/2];
            std::uint8_t color=source_x&1 ? pair&15 : pair>>4;
            if (invert) color=15-color;
            pixel(state,x,y,color);
        }
    }
    return DisplayResult::Ok;
}
}} // namespace zseries::oem
