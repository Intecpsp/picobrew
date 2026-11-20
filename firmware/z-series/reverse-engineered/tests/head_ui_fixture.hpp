#pragma once
#include "zseries/head_ui_commands.hpp"
#include "core_event_fixture.hpp"
namespace head_ui_fixture {
using namespace zseries::oem;
using Word=std::uint32_t;
inline Quantity quantity(const Word*& w){Quantity q;const std::uint64_t bits=std::uint64_t(w[0])|(std::uint64_t(w[1])<<32);std::memcpy(&q.value,&bits,8);q.unit=static_cast<Unit>(w[2]);w+=3;return q;}
inline void quantity(std::vector<Word>& out,Quantity q){std::uint64_t bits;std::memcpy(&bits,&q.value,8);out.insert(out.end(),{static_cast<Word>(bits),static_cast<Word>(bits>>32),static_cast<Word>(q.unit)});}
inline void parameters(ProgramParameters& p,const Word* w){p.id=static_cast<ZProgramId>(*w++);std::memcpy(&p.abv,w++,4);std::memcpy(&p.ibu,w++,4);p.time=quantity(w);p.water=quantity(w);p.intensity=quantity(w);p.temperature=quantity(w);p.resumed_session=*w;}
inline void parameters(std::vector<Word>& out,const ProgramParameters& p){out.insert(out.end(),{static_cast<Word>(p.id),static_cast<Word>(p.abv),static_cast<Word>(p.ibu)});quantity(out,p.time);quantity(out,p.water);quantity(out,p.intensity);quantity(out,p.temperature);out.push_back(p.resumed_session);}
inline void manual(ManualStepParameters& p,const Word* w){p.byte_00=*w++;p.location=*w++;p.temperature=quantity(w);p.hold=quantity(w);p.drain=quantity(w);for(auto& b:p.bytes_38)b=*w++;}
inline void manual(std::vector<Word>& out,const ManualStepParameters& p){out.insert(out.end(),{p.byte_00,p.location});quantity(out,p.temperature);quantity(out,p.hold);quantity(out,p.drain);for(auto b:p.bytes_38)out.push_back(b);}
inline void recipe(RecipeReference& value,const unsigned char* bytes){std::vector<std::uint8_t> raw(core_event_fixture::bytes);std::memcpy(raw.data()+16,bytes,176);CoreEvent event;core_event_fixture::load(event,raw.data());value=event.recipes[0];}
inline std::vector<std::uint8_t> recipe(const RecipeReference& value){CoreEvent event;event.recipes[0]=value;const auto raw=core_event_fixture::save(event);return {raw.begin()+16,raw.begin()+192};}
inline std::vector<std::uint8_t> event(const HeadUiEvent& value){std::vector<std::uint8_t> raw(276);core_event_fixture::word(raw.data(),static_cast<Word>(value.command));raw[4]=value.flags;core_event_fixture::word(raw.data()+8,value.index);const auto r=recipe(value.recipe);std::copy(r.begin(),r.end(),raw.begin()+12);core_event_fixture::parameters(raw.data()+188,value.parameters);return raw;}
}
