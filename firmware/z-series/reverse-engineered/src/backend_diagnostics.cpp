#include "zseries/backend_diagnostics.hpp"
#include "zseries/http_backend.hpp"
#include "zseries/core_helpers.hpp"
#include <cstdio>

namespace zseries { namespace oem {
namespace {
struct AssertionRow {BackendDiagnosticDomain domain;std::uint32_t line;BackendAssertionDescription value;};
const AssertionRow assertions[]={
    {BackendDiagnosticDomain::Http,0xe7,{"HttpBackend.h","backState.BackendError == ZBackendError::None"}},
    {BackendDiagnosticDomain::Http,0x2e3,{"HttpBackend.h","!CString::isNullOrEmpty(ctl)"}},
    {BackendDiagnosticDomain::Http,0x2e9,{"HttpBackend.h","!_uri.isTruncated()"}},
    {BackendDiagnosticDomain::Http,0x2ee,{"HttpBackend.h","!CString::isNullOrEmpty(type)"}},
    {BackendDiagnosticDomain::Http,0x2f4,{"HttpBackend.h","!_uri.isTruncated()"}},
    {BackendDiagnosticDomain::Http,0x2fe,{"HttpBackend.h","!_uri.isTruncated()"}},
    {BackendDiagnosticDomain::Http,0x303,{"HttpBackend.h","!CString::isNullOrEmpty(uri)"}},
    {BackendDiagnosticDomain::Http,0x306,{"HttpBackend.h","!_uri.isTruncated()"}},
    {BackendDiagnosticDomain::Http,0x3ac,{"HttpBackend.h","_pendingRecipe.kind == RecipeKind::Zymatic"}},
    {BackendDiagnosticDomain::Http,0x3b1,{"HttpBackend.h","_pendingRecipe.kind == RecipeKind::BrewPen"}},
    {BackendDiagnosticDomain::Http,0x3b2,{"HttpBackend.h","_pendingRecipe.id == 0"}},
    {BackendDiagnosticDomain::Http,0x3b6,{"HttpBackend.h","_pendingRecipe.kind == RecipeKind::PicoPak"}},
    {BackendDiagnosticDomain::Session,0x7e,{"ZBackend.h","typelen> 0"}},
    {BackendDiagnosticDomain::Session,0xfc,{"ZBackend.h","canSync()"}},
    {BackendDiagnosticDomain::Session,0xfd,{"ZBackend.h","s.Type != SessionType::Unknown"}},
    {BackendDiagnosticDomain::Session,0x108,{"ZBackend.h","canSync()"}},
    {BackendDiagnosticDomain::Session,0x113,{"ZBackend.h","canSync()"}},
    {BackendDiagnosticDomain::Helpers,0xdc,{"CoreHelpers.h","!_backState.IsRegistered"}},
    {BackendDiagnosticDomain::Helpers,0x14a,{"CoreHelpers.h","_state != newState"}},
    {BackendDiagnosticDomain::Helpers,0x1c1,{"CoreHelpers.h","_cState.sessionState.Id == _sessionId"}},
    {BackendDiagnosticDomain::Http,0x5d,{"Buffer.h","!_isFrozen"}},
    {BackendDiagnosticDomain::Http,0x5e,{"Buffer.h","data != nullptr || datalen == 0"}},
    {BackendDiagnosticDomain::Http,0x68,{"Buffer.h","count == 0"}},
    {BackendDiagnosticDomain::Session,0x5d,{"Buffer.h","!_isFrozen"}},
    {BackendDiagnosticDomain::Session,0x5e,{"Buffer.h","data != nullptr || datalen == 0"}},
    {BackendDiagnosticDomain::Session,0x68,{"Buffer.h","count == 0"}},
    {BackendDiagnosticDomain::Http,0xf,{"Json.h","isEnumValid<EClass>(e)"}},
    {BackendDiagnosticDomain::Session,0x17,{"Json.h","isEnumValid<EClass>(value)"}},
    {BackendDiagnosticDomain::Session,0x7d,{"Quantity.h","areCompatible(_unit, unit)"}},
};
struct DiagnosticRow {BackendDiagnosticDomain domain;std::uint32_t line;BackendDiagnosticDescription value;};
const DiagnosticRow diagnostics[]={
    {BackendDiagnosticDomain::Http,0xaf,{"HttpBackend.h","HBE",ConsoleLevel::Error,BackendDiagnosticKind::Plain,"Useless call to setState"}},
    {BackendDiagnosticDomain::Http,0xe0,{"HttpBackend.h","JSON",ConsoleLevel::Error,BackendDiagnosticKind::JsonError,"deserializeJson error %d %s"}},
    {BackendDiagnosticDomain::Http,0xed,{"HttpBackend.h","JSON",ConsoleLevel::Warning,BackendDiagnosticKind::Plain,"ZBackendError is missing"}},
    {BackendDiagnosticDomain::Http,0xf4,{"HttpBackend.h","HBE",ConsoleLevel::Error,BackendDiagnosticKind::Text,"%s"}},
    {BackendDiagnosticDomain::Http,0x10a,{"HttpBackend.h","JSON",ConsoleLevel::Error,BackendDiagnosticKind::JsonError,"deserializeJson error %d %s"}},
    {BackendDiagnosticDomain::Http,0x11c,{"HttpBackend.h","JSON",ConsoleLevel::Warning,BackendDiagnosticKind::Integer,"Truncating list (retaining only %d recipes)"}},
    {BackendDiagnosticDomain::Http,0x140,{"HttpBackend.h","JSON",ConsoleLevel::Error,BackendDiagnosticKind::JsonError,"deserializeJson error %d %s"}},
    {BackendDiagnosticDomain::Http,0x275,{"HttpBackend.h","JSON",ConsoleLevel::Error,BackendDiagnosticKind::JsonError,"deserializeJson error %d %s"}},
    {BackendDiagnosticDomain::Http,0x28a,{"HttpBackend.h","JSON",ConsoleLevel::Error,BackendDiagnosticKind::JsonError,"deserializeJson error %d %s"}},
    {BackendDiagnosticDomain::Http,0x2a0,{"HttpBackend.h","JSON",ConsoleLevel::Error,BackendDiagnosticKind::JsonError,"deserializeJson error %d %s"}},
    {BackendDiagnosticDomain::Http,0x2b5,{"HttpBackend.h","JSON",ConsoleLevel::Error,BackendDiagnosticKind::JsonError,"deserializeJson error %d %s"}},
    {BackendDiagnosticDomain::Http,0x2c8,{"HttpBackend.h","HBE",ConsoleLevel::Error,BackendDiagnosticKind::Plain,"Unsupported type for ResumableSession"}},
    {BackendDiagnosticDomain::Http,0x31a,{"HttpBackend.h","HBE",ConsoleLevel::Error,BackendDiagnosticKind::Plain,"Unsupported type"}},
    {BackendDiagnosticDomain::Http,0x32a,{"HttpBackend.h","HBE",ConsoleLevel::Error,BackendDiagnosticKind::Plain,"_httpClient.init()"}},
    {BackendDiagnosticDomain::Http,0x350,{"HttpBackend.h","HBE",ConsoleLevel::Error,BackendDiagnosticKind::Text,"_httpClient.%s()"}},
    {BackendDiagnosticDomain::Http,0x3bf,{"HttpBackend.h","HBE",ConsoleLevel::Error,BackendDiagnosticKind::Text,"Unexpected command type %s"}},
    {BackendDiagnosticDomain::Http,0x425,{"HttpBackend.h","HBE",ConsoleLevel::Error,BackendDiagnosticKind::Plain,"Unexpected content type"}},
    {BackendDiagnosticDomain::Session,0xf4,{"ZBackend.h","ZBEND",ConsoleLevel::Error,BackendDiagnosticKind::Plain,"Failed to send command"}},
    {BackendDiagnosticDomain::Session,0x11c,{"ZBackend.h","ZBEND",ConsoleLevel::Warning,BackendDiagnosticKind::Plain,"Missing step name"}},
    {BackendDiagnosticDomain::Session,0x161,{"ZBackend.h","ZBEND",ConsoleLevel::Error,BackendDiagnosticKind::Plain,"Unexpected content type"}},
    {BackendDiagnosticDomain::Session,0x164,{"ZBackend.h","ZBEND",ConsoleLevel::Error,BackendDiagnosticKind::Plain,"Not enough space to receive response"}},
    {BackendDiagnosticDomain::Session,0x178,{"ZBackend.h","ZBEND",ConsoleLevel::Error,BackendDiagnosticKind::JsonError,"deserializeJson error %d %s"}},
    {BackendDiagnosticDomain::Helpers,0x17b,{"CoreHelpers.h","ZSES",ConsoleLevel::Error,BackendDiagnosticKind::Integer,"Invalid newState %d"}},
    {BackendDiagnosticDomain::Helpers,0x1a9,{"CoreHelpers.h","ZSES",ConsoleLevel::Error,BackendDiagnosticKind::Text,"Cannot start session (state %s)"}},
    {BackendDiagnosticDomain::Helpers,0x1ab,{"CoreHelpers.h","ZSES",ConsoleLevel::Error,BackendDiagnosticKind::Plain,"Cannot start session (backend cannot sync)"}},
    {BackendDiagnosticDomain::Helpers,0x1ae,{"CoreHelpers.h","ZSES",ConsoleLevel::Warning,BackendDiagnosticKind::Plain,"start()"}},
    {BackendDiagnosticDomain::Helpers,0x1c3,{"CoreHelpers.h","ZSES",ConsoleLevel::Error,BackendDiagnosticKind::Plain,"Error while creating session"}},
    {BackendDiagnosticDomain::Helpers,0x1ca,{"CoreHelpers.h","ZSES",ConsoleLevel::Error,BackendDiagnosticKind::Plain,"Timeout while creating session"}},
    {BackendDiagnosticDomain::Helpers,0x1d8,{"CoreHelpers.h","ZSES",ConsoleLevel::Error,BackendDiagnosticKind::Plain,"Timeout while logging data point"}},
    {BackendDiagnosticDomain::Helpers,0x1e0,{"CoreHelpers.h","ZSES",ConsoleLevel::Error,BackendDiagnosticKind::Plain,"Timeout while ending session"}},
    {BackendDiagnosticDomain::Http,0x397,{"HttpBackend.h","HBE",ConsoleLevel::Error,BackendDiagnosticKind::Plain,"get() called while not in Idle state"}},
    {BackendDiagnosticDomain::Http,0x399,{"HttpBackend.h","HBE",ConsoleLevel::Warning,BackendDiagnosticKind::Plain,"Missing network"}},
    {BackendDiagnosticDomain::Http,0x3cb,{"HttpBackend.h","HBE",ConsoleLevel::Error,BackendDiagnosticKind::Plain,"beginSync() called while not in Idle state"}},
    {BackendDiagnosticDomain::Http,0x3cd,{"HttpBackend.h","HBE",ConsoleLevel::Warning,BackendDiagnosticKind::Plain,"Missing network"}},
    {BackendDiagnosticDomain::Http,0x429,{"HttpBackend.h","HBE",ConsoleLevel::Error,BackendDiagnosticKind::Plain,"Not enough space to receive response"}},
    {BackendDiagnosticDomain::Helpers,0x167,{"CoreHelpers.h","ZSES",ConsoleLevel::Error,BackendDiagnosticKind::Plain,"Session cannot send log data point"}},
    {BackendDiagnosticDomain::Helpers,0x172,{"CoreHelpers.h","ZSES",ConsoleLevel::Error,BackendDiagnosticKind::Plain,"Session cannot end session on backend"}},
    {BackendDiagnosticDomain::Http,0x66,{"Buffer.h","BUF",ConsoleLevel::Warning,BackendDiagnosticKind::Plain,"Buffer truncated"}},
    {BackendDiagnosticDomain::Http,0x6a,{"Buffer.h","BUF",ConsoleLevel::Warning,BackendDiagnosticKind::Integer,"Dropping %d bytes"}},
    {BackendDiagnosticDomain::Session,0x66,{"Buffer.h","BUF",ConsoleLevel::Warning,BackendDiagnosticKind::Plain,"Buffer truncated"}},
    {BackendDiagnosticDomain::Session,0x6a,{"Buffer.h","BUF",ConsoleLevel::Warning,BackendDiagnosticKind::Integer,"Dropping %d bytes"}},
    {BackendDiagnosticDomain::Helpers,0xca,{"CoreHelpers.h","REG",ConsoleLevel::Debug,BackendDiagnosticKind::Scope,"stop"}},
};
const char* error_name(RecipeJsonError error){static const char* names[]={"Ok","IncompleteInput","InvalidInput","NoMemory","NotSupported","TooDeep"};const auto n=static_cast<unsigned>(error);return n<6?names[n]:nullptr;}
bool render(const ConsoleServices& services,const BackendDiagnosticDescription* description,std::uint32_t line,const char* text,std::int32_t number,RecipeJsonError error){
    if(!description||!services.log)return false;
    char message[256];const char* rendered=description->format;
    switch(description->kind){
    case BackendDiagnosticKind::Plain:break;
    case BackendDiagnosticKind::Text:if(!text)return false;::snprintf(message,sizeof(message),description->format,text);rendered=message;break;
    case BackendDiagnosticKind::Integer: ::snprintf(message,sizeof(message),description->format,static_cast<int>(number));rendered=message;break;
    case BackendDiagnosticKind::JsonError:{const auto name=error_name(error);if(!name)return false;::snprintf(message,sizeof(message),description->format,static_cast<int>(error),name);rendered=message;break;}
    case BackendDiagnosticKind::Scope:return false;
    }
    return services.log(services.context,description->level,description->tag,description->file,line,rendered);
}
}
const BackendAssertionDescription* backend_assertion_description(BackendDiagnosticDomain domain,std::uint32_t line) noexcept {for(const auto& row:assertions)if(row.domain==domain&&row.line==line)return &row.value;return nullptr;}
const BackendDiagnosticDescription* backend_diagnostic_description(BackendDiagnosticDomain domain,std::uint32_t line) noexcept {for(const auto& row:diagnostics)if(row.domain==domain&&row.line==line)return &row.value;return nullptr;}
bool report_backend_assertion(const ConsoleServices& services,BackendDiagnosticDomain domain,std::uint32_t line){const auto entry=backend_assertion_description(domain,line);return entry&&services.assertion&&services.assertion(services.context,entry->file,line,entry->expression);}
bool report_http_backend_diagnostic(const ConsoleServices& services,const HttpBackend& backend,std::uint32_t line){
    const char* text=backend.diagnostic_text;std::int32_t number=static_cast<std::int32_t>(backend.diagnostic_argument);
    if(line==0xf4)text=to_string(backend.backend.backend_error);
    if(line==0x11c)number=static_cast<std::int32_t>(backend.event.count);
    return render(services,backend_diagnostic_description(BackendDiagnosticDomain::Http,line),line,text,number,backend.json_error);
}
bool report_z_backend_diagnostic(const ConsoleServices& services,const ZBackend& backend,std::uint32_t line){return render(services,backend_diagnostic_description(BackendDiagnosticDomain::Session,line),line,nullptr,static_cast<std::int32_t>(backend.diagnostic_argument),backend.json_error);}
bool report_core_helper_diagnostic(const ConsoleServices& services,const SessionLogger& logger,std::uint32_t line,const BackendScopeTrace& scope){
    const auto description=backend_diagnostic_description(BackendDiagnosticDomain::Helpers,line);if(!description)return false;
    if(description->kind==BackendDiagnosticKind::Scope)return scope.emit&&scope.emit(scope.context,description->tag,description->file,line,description->format);
    const char* names[]={"Idle","Creating","Waiting","Logging","Ending"};const auto state=static_cast<std::uint32_t>(logger.state);
    return render(services,description,line,state<5?names[state]:"<State::INVALID>",static_cast<std::int32_t>(state),RecipeJsonError::Ok);
}
}} // namespace zseries::oem
