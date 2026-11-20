#include "zseries/display_text.hpp"
#include <cstring>

namespace zseries { namespace oem {
namespace {
std::int32_t signed_bits(std::uint32_t bits) noexcept {
    std::int32_t result;std::memcpy(&result,&bits,sizeof result);return result;
}
DisplayTextResult text_size(const char* text,std::size_t capacity,std::size_t& bytes) noexcept {
    if (!text) return DisplayTextResult::MissingText;
    for (bytes=0;bytes<capacity;++bytes) {
        if (!text[bytes]) return bytes<=UINT16_MAX?DisplayTextResult::Ok:DisplayTextResult::TextTooLong;
        if (bytes==UINT16_MAX) return DisplayTextResult::TextTooLong;
    }
    return DisplayTextResult::UnterminatedText;
}
bool font_valid(const DisplayFont& font) noexcept {
    return font.glyph_width && font.glyph_height && font.glyph_width<=128 && font.glyph_height<=64 &&
        font.bitmap.width==font.glyph_width*225 && font.bitmap.height==font.glyph_height;
}
DisplayTextResult layout(DisplayTextContext& context) noexcept {
    if (!context.font || !font_valid(*context.font)) return DisplayTextResult::InvalidFont;
    const std::uint64_t line_height=static_cast<std::uint64_t>(context.font->glyph_height)+context.line_spacing;
    if (line_height>UINT32_MAX) return DisplayTextResult::InvalidFont;
    context.width=display_rect_width(context.bounds);context.height=display_rect_height(context.bounds);
    context.cell_width=context.font->glyph_width;context.line_height=static_cast<std::uint32_t>(line_height);
    context.rows=context.height/context.line_height;context.columns=context.width/context.cell_width;
    return DisplayTextResult::Ok;
}
DisplayTextResult next_byte(DisplayUtf8& decoder,std::uint8_t& byte) noexcept {
    if (decoder.cursor>=decoder.length) return DisplayTextResult::TruncatedUtf8;
    byte=decoder.bytes[decoder.cursor++];return DisplayTextResult::Ok;
}
DisplayTextResult continuation(DisplayUtf8& decoder,std::uint32_t& bits) noexcept {
    std::uint8_t byte;const DisplayTextResult status=next_byte(decoder,byte);
    if (status!=DisplayTextResult::Ok) return status;
    // The OEM logs a malformed continuation, consumes it, and supplies zero.
    bits=(byte&0xc0)==0x80?byte&0x3f:0;return DisplayTextResult::Ok;
}
} // namespace

DisplayTextResult display_utf8_length(const char* text,std::size_t capacity,std::uint32_t& count) noexcept {
    std::size_t bytes;const DisplayTextResult status=text_size(text,capacity,bytes);
    if (status!=DisplayTextResult::Ok) return status;
    count=0;for(std::size_t i=0;i<bytes;++i)count+=(static_cast<std::uint8_t>(text[i])&0xc0)!=0x80;
    return DisplayTextResult::Ok;
}
DisplayTextResult display_utf8_initialize(DisplayUtf8& decoder,const char* text,std::size_t capacity) noexcept {
    std::size_t bytes;const DisplayTextResult status=text_size(text,capacity,bytes);
    if (status!=DisplayTextResult::Ok) return status;
    decoder.bytes=reinterpret_cast<const std::uint8_t*>(text);decoder.cursor=0;decoder.length=static_cast<std::uint16_t>(bytes);
    decoder.characters=0;for(std::size_t i=0;i<bytes;++i)decoder.characters+=(decoder.bytes[i]&0xc0)!=0x80;
    return DisplayTextResult::Ok;
}
DisplayTextResult display_utf8_next(DisplayUtf8& decoder,std::uint32_t& codepoint) noexcept {
    codepoint=DisplayInvalidCodepoint;
    if (decoder.cursor>=decoder.length) return DisplayTextResult::Ok;
    if (!decoder.bytes) return DisplayTextResult::MissingText;
    std::uint8_t first;DisplayTextResult status=next_byte(decoder,first);
    if (status!=DisplayTextResult::Ok) return status;
    if (!(first&0x80)) {codepoint=first;return DisplayTextResult::Ok;}
    unsigned extra=0;std::uint32_t minimum=0,value=0;
    if ((first&0xe0)==0xc0) {extra=1;minimum=127;value=first&0x1f;}
    else if ((first&0xf0)==0xe0) {extra=2;minimum=2047;value=first&15;}
    else if ((first&0xf8)==0xf0) {extra=3;minimum=65535;value=first&7;}
    else {
        // Invalid leading bytes reset the original decoder to its empty
        // string, then return the sentinel that draws the fallback glyph.
        decoder=DisplayUtf8{};decoder.bytes=reinterpret_cast<const std::uint8_t*>("");return DisplayTextResult::Ok;
    }
    for(unsigned i=0;i<extra;++i){
        std::uint32_t bits;status=continuation(decoder,bits);if(status!=DisplayTextResult::Ok)return status;
        value=(value<<6)|bits;
    }
    codepoint=value;
    return value>minimum?DisplayTextResult::Ok:DisplayTextResult::OverlongUtf8;
}

DisplayTextResult display_text_font(DisplayTextContext& context,const DisplayFont& font,std::uint32_t spacing) noexcept {
    if (!font_valid(font) || static_cast<std::uint64_t>(font.glyph_height)+spacing>UINT32_MAX) return DisplayTextResult::InvalidFont;
    context.font=&font;context.line_spacing=spacing;return layout(context);
}
DisplayTextResult display_text_bounds(DisplayTextContext& context,const DisplayRect& bounds) noexcept {
    if (!display_rect_valid(bounds) || !display_rect_width(bounds) || !display_rect_height(bounds))return DisplayTextResult::InvalidRegion;
    if (!context.font || !font_valid(*context.font))return DisplayTextResult::InvalidFont;
    context.bounds=bounds;return layout(context);
}

DisplayTextResult display_glyphs(DisplayState& state,const DisplayFont& font,const char* text,std::size_t capacity,
                               std::uint32_t count,DisplayPoint point,bool invert) noexcept {
    if (!font_valid(font))return DisplayTextResult::InvalidFont;
    DisplayUtf8 decoder;DisplayTextResult status=display_utf8_initialize(decoder,text,capacity);
    if (status!=DisplayTextResult::Ok)return status;
    for(std::uint32_t i=0;i<count && decoder.cursor<decoder.length;++i){
        std::uint32_t codepoint;status=display_utf8_next(decoder,codepoint);if(status!=DisplayTextResult::Ok)return status;
        if(codepoint==9 || codepoint==10)codepoint=32;
        if(codepoint-32u>223u)codepoint=256;
        const std::int32_t left=static_cast<std::int32_t>((codepoint-32)*font.glyph_width);
        const DisplayRect region{left,0,static_cast<std::int32_t>(left+font.glyph_width-1),static_cast<std::int32_t>(font.glyph_height-1)};
        if(display_bitmap(state,point,font.bitmap,region,invert)!=DisplayResult::Ok)return DisplayTextResult::BitmapFailure;
        point.x=signed_bits(static_cast<std::uint32_t>(point.x)+font.glyph_width);
    }
    return DisplayTextResult::Ok;
}

DisplayTextResult display_text(DisplayState& state,const DisplayTextContext& context,const char* text,std::size_t capacity,
                              const DisplayTextOptions& options) noexcept {
    if(!context.font || !font_valid(*context.font) || !context.cell_width || !context.line_height)return DisplayTextResult::InvalidFont;
    std::size_t bytes;DisplayTextResult status=text_size(text,capacity,bytes);if(status!=DisplayTextResult::Ok)return status;
    std::uint32_t length=options.characters;
    if(!length){status=display_utf8_length(text,capacity,length);if(status!=DisplayTextResult::Ok)return status;}
    if(!length || !bytes)return DisplayTextResult::EmptyText;
    DisplayPoint point{context.bounds.left,signed_bits(static_cast<std::uint32_t>(options.row)*context.line_height+
        static_cast<std::uint32_t>(context.bounds.top)+static_cast<std::uint32_t>(options.offset_y))};
    std::uint32_t draw_count=length;std::size_t skip=0;
    if(options.offset_x){point.x=signed_bits(static_cast<std::uint32_t>(point.x)+static_cast<std::uint32_t>(options.offset_x));}
    else {
        if(draw_count>context.columns){
            draw_count=context.columns;
            if(options.alignment==DisplayTextAlignment::Center)skip=(length-draw_count)/2;
            else if(options.alignment==DisplayTextAlignment::Right)skip=length-draw_count;
        }
        const std::uint64_t wide=static_cast<std::uint64_t>(draw_count)*context.cell_width;
        if(wide>context.width)return DisplayTextResult::InvalidPosition;
        const std::uint32_t width=static_cast<std::uint32_t>(wide);
        if(options.alignment==DisplayTextAlignment::Center)point.x=signed_bits(static_cast<std::uint32_t>(point.x)+(context.width-width)/2);
        else if(options.alignment==DisplayTextAlignment::Right)point.x=signed_bits(static_cast<std::uint32_t>(point.x)+context.width-width);
    }
    // Original clipping advances the byte pointer by a character-count
    // difference, including for UTF-8. Preserve it within the source buffer;
    // reject a pointer beyond its terminator instead of reading outside it.
    if(skip>bytes)return DisplayTextResult::InvalidPosition;
    return display_glyphs(state,*context.font,text+skip,capacity-skip,draw_count,point,options.invert);
}
}} // namespace zseries::oem
