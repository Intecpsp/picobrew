// Test-only field/ABI translator; the application never uses the OEM layout.
#pragma once
#include "zseries/program_list.hpp"
#include <cstring>
#include <vector>
namespace core_event_fixture {
using namespace zseries::oem;
constexpr std::size_t bytes = 0xdf0;
inline std::uint32_t word(const std::uint8_t* p) {
    return p[0] | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
inline void word(std::uint8_t* p, std::uint32_t value) { for(unsigned i=0;i<4;++i)p[i]=static_cast<std::uint8_t>(value>>(8*i)); }
inline void parameters(ProgramParameters& out,const std::uint8_t* p) {
    out.id=static_cast<ZProgramId>(word(p));out.abv=static_cast<std::int32_t>(word(p+4));out.ibu=static_cast<std::int32_t>(word(p+8));
    Quantity* values[]={&out.time,&out.water,&out.intensity,&out.temperature};
    for(unsigned i=0;i<4;++i){std::memcpy(&values[i]->value,p+16+16*i,8);values[i]->unit=static_cast<Unit>(word(p+24+16*i));}
    out.resumed_session=p[80]!=0;
}
inline void parameters(std::uint8_t* p,const ProgramParameters& value) {
    word(p,static_cast<std::uint32_t>(value.id));word(p+4,static_cast<std::uint32_t>(value.abv));word(p+8,static_cast<std::uint32_t>(value.ibu));
    const Quantity values[]={value.time,value.water,value.intensity,value.temperature};
    for(unsigned i=0;i<4;++i){std::memcpy(p+16+16*i,&values[i].value,8);word(p+24+16*i,static_cast<std::uint32_t>(values[i].unit));}
    p[80]=value.resumed_session;
}
inline void load(CoreEvent& out,const std::uint8_t* p) {
    out.type=static_cast<CoreEventType>(word(p+4));out.argument=word(p+8);out.second_argument=word(p+12);
    for(unsigned i=0;i<20;++i){
        const auto* entry=p+16+176*i;auto& ref=out.recipes[i];ref.id=word(entry);std::memcpy(ref.name.data(),entry+4,65);
        ref.name_length=word(entry+72);ref.name_overflow=entry[80];ref.name_frozen=entry[81];
        ref.kind=static_cast<RecipeKind>(word(entry+84));parameters(ref.parameters,entry+88);
    }
    out.count=word(p+0xdd0);out.offset=word(p+0xdd4);out.total=word(p+0xdd8);out.byte_ddc=p[0xddc];
    out.detection_error=static_cast<HexDetectError>(word(p+0xde0));out.boiler=static_cast<BoilerType>(word(p+0xde4));
    out.error=static_cast<CoreErrorId>(word(p+0xde8));out.source=static_cast<CoreCommandId>(word(p+0xdec));
}
inline std::vector<std::uint8_t> save(const CoreEvent& value) {
    std::vector<std::uint8_t> result(bytes);auto* p=result.data();
    word(p+4,static_cast<std::uint32_t>(value.type));word(p+8,value.argument);word(p+12,value.second_argument);
    for(unsigned i=0;i<20;++i){
        auto* entry=p+16+176*i;const auto& ref=value.recipes[i];word(entry,ref.id);std::memcpy(entry+4,ref.name.data(),65);
        word(entry+72,ref.name_length);word(entry+76,64-ref.name_length);entry[80]=ref.name_overflow;entry[81]=ref.name_frozen;
        word(entry+84,static_cast<std::uint32_t>(ref.kind));parameters(entry+88,ref.parameters);
    }
    word(p+0xdd0,value.count);word(p+0xdd4,value.offset);word(p+0xdd8,value.total);p[0xddc]=value.byte_ddc;
    word(p+0xde0,static_cast<std::uint32_t>(value.detection_error));word(p+0xde4,static_cast<std::uint32_t>(value.boiler));
    word(p+0xde8,static_cast<std::uint32_t>(value.error));word(p+0xdec,static_cast<std::uint32_t>(value.source));return result;
}
}
