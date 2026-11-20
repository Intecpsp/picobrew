#include "recipe_document.hpp"
#include "zseries/http_backend.hpp"
#include "zseries/program_diagnostics.hpp"
#include <cstdio>
#include <cstdarg>
#include <memory>
#include <new>

namespace zseries { namespace oem {
namespace {
using Variant=ArduinoJson::JsonVariantConst;
const ConsoleServices* diagnostic_sink(HttpBackend& self){return self.diagnostics.log||self.diagnostics.assertion?&self.diagnostics:nullptr;}
bool output(HttpBackend& self,ConsoleLevel level,const char* tag,const char* file,std::uint32_t line,const char* format,...){
    const auto* service=diagnostic_sink(self);if(!service)return true;if(!service->log)return false;
    char message[512];va_list args,copy;va_start(args,format);va_copy(copy,args);const int count=::vsnprintf(message,sizeof message,format,args);va_end(args);
    if(count<0){va_end(copy);return false;}
    if(static_cast<std::size_t>(count)<sizeof message){va_end(copy);return service->log(service->context,level,tag,file,line,message);}
    // Response logging can exceed the small formatting buffer. Bound it to
    // the owned 4096-byte response and never truncate an apparent success.
    if(count>4608){va_end(copy);return false;}
    std::unique_ptr<char[]> extended(new(std::nothrow)char[static_cast<std::size_t>(count)+1]);if(!extended){va_end(copy);return false;}
    const int written=::vsnprintf(extended.get(),static_cast<std::size_t>(count)+1,format,copy);va_end(copy);
    return written==count&&service->log(service->context,level,tag,file,line,extended.get());
}
template<std::size_t N> bool text(HttpBackend& self,BackendText<N>& destination,const char* value){
    destination.set(value);const auto length=value?std::strlen(value):0;
    return length<=N||(output(self,ConsoleLevel::Warning,"BUF","Buffer.h",0x66,"Buffer truncated")&&
        output(self,ConsoleLevel::Warning,"BUF","Buffer.h",0x6a,"Dropping %u bytes",static_cast<unsigned>(length-N)));
}
bool step(void* context,std::size_t index,HttpRecipeStep& out){
    const auto value=(*static_cast<Variant*>(context))[index];out.name=value["Name"].as<const char*>();
    out.action=value["Action"].as<std::int32_t>();out.temperature_fahrenheit=value["Temp"].as<std::int32_t>();
    out.temperature_scan_fahrenheit=json_adapter::number(value["Temp"]);out.location=value["Location"].as<std::int32_t>();
    out.minutes=value["Time"].as<std::int32_t>();out.drain_minutes=value["Drain"].as<std::int32_t>();
    out.minimum_minutes=value["MinTime"].is<std::int32_t>()?value["MinTime"].as<std::int32_t>():-1;
    out.maximum_minutes=value["MaxTime"].is<std::int32_t>()?value["MaxTime"].as<std::int32_t>():-1;return true;
}
BackendResult reference(HttpBackend& self,Variant value,RecipeReference& result,std::uint32_t line){
    const auto kind=static_cast<std::uint32_t>(value["Kind"].as<std::int32_t>());if(kind>3){const auto r=self.assertion(0xf);if(r!=BackendResult::Ok)return r;}
    const auto id=value["ID"].as<std::int32_t>(),abv=value["Abv"].as<std::int32_t>(),ibu=value["Ibu"].as<std::int32_t>();
    const char* name=value["Name"].as<const char*>();const auto length=name?std::strlen(name):0;
    if(!output(self,ConsoleLevel::Info,"JSON","HttpBackend.h",line,"%7d %d %s [Abv %d] [Ibu %d]",
        id,static_cast<std::int32_t>(kind),name?name:"(null)",abv,ibu))return BackendResult::ServiceFailure;
    result.id=static_cast<std::uint32_t>(id);result.kind=static_cast<RecipeKind>(kind);
    result.name_length=static_cast<std::uint32_t>(std::min(length,result.name.size()-1));result.name.fill(0);
    if(result.name_length)std::memcpy(result.name.data(),name,result.name_length);
    result.name_overflow=length>result.name_length;result.name_frozen=false;
    if(result.name_overflow&&(!output(self,ConsoleLevel::Warning,"BUF","Buffer.h",0x66,"Buffer truncated")||
        !output(self,ConsoleLevel::Warning,"BUF","Buffer.h",0x6a,"Dropping %u bytes",static_cast<unsigned>(length-result.name_length))))return BackendResult::ServiceFailure;
    result.parameters.abv=abv;result.parameters.ibu=ibu;
    return BackendResult::Ok;
}
BackendResult session_type(HttpBackend& self,Variant value,SessionType& type){
    const auto parsed=static_cast<std::uint32_t>(value.as<std::int32_t>());if(parsed>16){auto r=self.assertion(0xf);if(r!=BackendResult::Ok)return r;}
    type=static_cast<SessionType>(parsed);return BackendResult::Ok;
}
ProgramBuildResult build(HttpBackend& self,Variant value,SessionType type,std::uint32_t id){
    auto steps=value["Steps"];HttpRecipe recipe;recipe.name=value["Name"].as<const char*>();
    const char* code=value["TypeCode"].as<const char*>();recipe.type_code=code;recipe.is_beer=!code||!*code||std::strcmp(code,"Beer")==0;
    if(value["StartWater"].is<double>())recipe.start_water_liters=json_adapter::number(value["StartWater"]);
    recipe.pico_abv=value["Abv"].as<std::int32_t>();recipe.pico_ibu=value["Ibu"].as<std::int32_t>();
    recipe.context=&steps;recipe.step=step;recipe.step_count=steps.size();
    return build_http_recipe(self.program,recipe,type==SessionType::BrewPicoPak?HttpRecipeFormat::PicoPak:HttpRecipeFormat::Zymatic,
        type,id,self.parameters,self.environment,diagnostic_sink(self));
}
}
std::size_t serialize_backend_request(char* output,std::size_t capacity,std::uint32_t state,
    const char* firmware,BoilerType boiler,std::int32_t offset,const char* search,std::int32_t reference_id){
    if(!output||!capacity)return 0;
    ArduinoJson::StaticJsonDocument<JSON_ARRAY_SIZE(8)> document;
    auto root=document.to<ArduinoJson::JsonObject>();
    if(state==2){root["CurrentFirmware"]=firmware;root["BoilerType"]=static_cast<std::int32_t>(boiler);}
    else if(state==8||state==16){root["ID"]=reference_id;root["Kind"]=state==8?0:1;}
    else{const int kind=state==4?1:state==12?2:state==18?3:state==20?4:0;
        if(kind){root["Kind"]=kind;root["Offset"]=offset;root["MaxCount"]=20u;
            if(kind==3&&search&&*search)root["SearchString"]=search;}}
    output[0]=0;const auto count=ArduinoJson::serializeJson(document,output,capacity-1);output[count<capacity?count:capacity-1]=0;return count;
}
BackendResult HttpBackend::parse_response(bool& accepted){
    return parse_response_as(state,accepted);
}
BackendResult HttpBackend::parse_response_as(std::uint32_t state,bool& accepted){
    accepted=false;
    if(state!=2&&state!=4&&state!=6&&state!=8&&state!=10&&state!=12&&state!=14&&state!=16&&state!=18&&state!=20&&state!=22)return BackendResult::Ok;
    const bool list=state==4||state==12||state==18||state==20;
    if(list){local_recipe.uri.clear();local_recipe.guid.clear();}
    if(state==2&&!output(*this,ConsoleLevel::Info,"BEND",nullptr,0,"Response = %s",response.data()))return BackendResult::ServiceFailure;
    response_frozen=true;ArduinoJson::StaticJsonDocument<json_adapter::http_capacity> document;
    json_error=json_adapter::decode(document,response.data(),response_length);
    if(json_error!=RecipeJsonError::Ok){
        if(state==2)backend.error_text.set("JSON decoding");
        return diagnostic(state==2?0xe0:list?0x10a:state==8||state==16?0x140:state==6?0x275:state==10?0x28a:state==14?0x2a0:0x2b5);
    }
    const auto root=document.as<Variant>();const auto source=event.source;
    if(state==2){
        if(backend.backend_error!=ZBackendError::None){auto r=assertion(0xe7);if(r!=BackendResult::Ok)return r;}
        backend.registered=root["IsRegistered"].as<bool>();backend.token_expired=root["TokenExpired"].as<bool>();
        if(!root["ZBackendError"].isNull()){
            const auto value=static_cast<std::uint32_t>(root["ZBackendError"].as<std::int32_t>());if(value>3){auto r=assertion(0xf);if(r!=BackendResult::Ok)return r;}
            backend.backend_error=static_cast<ZBackendError>(value);
        }else{const auto r=diagnostic(0xed);if(r!=BackendResult::Ok)return r;}
        if(backend.backend_error!=ZBackendError::None){backend.error_text.set(to_string(backend.backend_error));return diagnostic(0xf4);}
        if(!text(*this,backend.registration_token,root["RegistrationToken"].as<const char*>())||
           !text(*this,backend.update_address,root["UpdateAddress"].as<const char*>())||
           !text(*this,backend.program_uri,root["ProgramUri"].as<const char*>())||
           !text(*this,backend.alias,root["Alias"].as<const char*>()))return BackendResult::ServiceFailure;
        backend.dirty_sessions=root["SessionStats"]["DirtySessionsSinceClean"].as<std::int32_t>();
        auto result=session_type(*this,root["SessionStats"]["LastSessionType"],backend.last_session);if(result!=BackendResult::Ok)return result;
        backend.resumable_session_id=root["SessionStats"]["ResumableSessionID"].as<std::int32_t>();accepted=true;return BackendResult::Ok;
    }
    if(list){
        auto recipes=root["Recipes"];
        if(!output(*this,ConsoleLevel::Info,"JSON",nullptr,0,"source = %s",to_string(source))||
           !output(*this,ConsoleLevel::Info,"JSON",nullptr,0,"count = %u",static_cast<unsigned>(recipes.size())))return BackendResult::ServiceFailure;
        event.reset(CoreEventType::RecipeRefListLoaded);event.source=source;
        event.count=static_cast<std::uint32_t>(std::min<std::size_t>(20,recipes.size()));
        event.offset=static_cast<std::uint32_t>(root["Offset"].as<std::int32_t>());event.total=static_cast<std::uint32_t>(root["TotalResults"].as<std::int32_t>());event.byte_ddc=event.count<recipes.size();
        if(event.byte_ddc){auto r=diagnostic(0x11c);if(r!=BackendResult::Ok)return r;}
        if(!output(*this,ConsoleLevel::Warning,"JSON","HttpBackend.h",0x11e,"offset       = %d",static_cast<std::int32_t>(event.offset))||
           !output(*this,ConsoleLevel::Warning,"JSON","HttpBackend.h",0x11f,"totalresults = %d",static_cast<std::int32_t>(event.total)))return BackendResult::ServiceFailure;
        for(std::uint32_t i=0;i<event.count;++i){auto value=recipes[i];auto r=reference(*this,value,event.recipes[i],0x128);if(r!=BackendResult::Ok)return r;
            const auto& item=event.recipes[i];
            if(item.kind==RecipeKind::BrewPen){if(!text(*this,local_recipe.uri,value["Uri"].as<const char*>()))return BackendResult::ServiceFailure;local_recipe.guid.clear();}}
        accepted=true;return BackendResult::Ok;
    }
    if(state==8||state==16){event.reset(CoreEventType::RecipeRefListLoaded);event.count=1;event.source=source;
        auto result=reference(*this,root,event.recipes[0],0x14f);accepted=result==BackendResult::Ok;return result;}
    auto value=root;SessionType type=state==6?SessionType::BrewZymatic:state==10?SessionType::BrewZRecipe:SessionType::BrewPicoPak;
    std::uint32_t id=0;
    if(state==22){program.reset();const auto result=session_type(*this,root["SessionType"],type);if(result!=BackendResult::Ok)return result;
        if(type==SessionType::BrewZymatic)value=root["Recipe"];
        else if(type==SessionType::BrewPicoPak)value=root["ZPicoRecipe"];
        else return diagnostic(0x2c8);}
    if(type==SessionType::BrewPicoPak)id=value["PicoRecipeID"].as<std::int32_t>();
    else if(type==SessionType::BrewZRecipe){id=pending_recipe.id;local_recipe.guid.append(value["Guid"].as<const char*>());}
    else id=value["ID"].as<std::int32_t>();
    if(!output(*this,ConsoleLevel::Info,"JSON",nullptr,0,"recipeId = %d",static_cast<std::int32_t>(id)))return BackendResult::ServiceFailure;
    program_error=build(*this,value,type,id);
    // Existing bounded reconstruction rejects invalid program inputs. Preserve
    // that explicit failure; do not publish an OEM-success claim on this path.
    if(program_error==ProgramBuildResult::DiagnosticFailure)return BackendResult::ServiceFailure;
    if(program_error!=ProgramBuildResult::Ok)return BackendResult::Ok;
    if(diagnostic_sink(*this)&&!report_program_buffer(*diagnostic_sink(*this),program))return BackendResult::ServiceFailure;
    event.reset(state==22?CoreEventType::ResumedProgramLoaded:CoreEventType::ProgramLoaded);event.source=source;accepted=true;return BackendResult::Ok;
}
}} // namespace zseries::oem
