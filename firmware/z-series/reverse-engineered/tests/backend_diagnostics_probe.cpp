#include "zseries/backend_diagnostics.hpp"
#include "zseries/http_backend.hpp"
#include "zseries/core_helpers.hpp"
#include <ArduinoJson.h>
#include <iostream>
#include <string>
using namespace zseries::oem;
struct Capture {JsonArray events;};
int main() {
    std::string line;
    while (std::getline(std::cin,line)) {
        DynamicJsonDocument in(4096),out(8192);
        if(deserializeJson(in,line))return 2;
        Capture capture{out.createNestedArray("events")};
        ConsoleServices services;services.context=&capture;
        services.assertion=[](void* context,const char* file,std::uint32_t source,const char* expression){
            auto row=static_cast<Capture*>(context)->events.createNestedObject();
            row["file"]=file;row["line"]=source;row["assertion"]=expression;return true;};
        services.log=[](void* context,ConsoleLevel level,const char* tag,const char* file,std::uint32_t source,const char* message){
            auto row=static_cast<Capture*>(context)->events.createNestedObject();
            row["file"]=file;row["line"]=source;row["level"]=static_cast<unsigned>(level);row["tag"]=tag;row["message"]=std::string(message);return true;};
        if(in["missing"].as<bool>())services=ConsoleServices{};
        const auto domain=static_cast<BackendDiagnosticDomain>(in["domain"].as<unsigned>());
        const auto source=in["line"].as<std::uint32_t>();
        const std::string op=in["op"]|"diagnostic";
        bool ok=false;
        if(op=="assertion")ok=report_backend_assertion(services,domain,source);
        else if(op=="description") {
            const auto value=backend_diagnostic_description(domain,source);
            if(value){out["file"]=value->file;out["tag"]=value->tag;out["level"]=static_cast<unsigned>(value->level);
                out["kind"]=static_cast<unsigned>(value->kind);out["format"]=value->format;ok=true;}
        } else if(domain==BackendDiagnosticDomain::Http) {
            HttpClient client;BackendState state;ProgramBuffer program;CoreEvent event;CoreEventMailbox mailbox;
            HttpBackend backend(client,state,program,event,mailbox);
            state.backend_error=static_cast<ZBackendError>(in["backend_error"]|0);
            event.count=in["count"].as<std::uint32_t>();backend.diagnostic_argument=in["argument"].as<std::uint32_t>();
            backend.diagnostic_text=in["text"].as<const char*>();backend.json_error=static_cast<RecipeJsonError>(in["error"]|0);
            ok=report_http_backend_diagnostic(services,backend,source);
        } else if(domain==BackendDiagnosticDomain::Session) {
            HttpClient client;ZBackend backend(client);backend.diagnostic_argument=in["argument"].as<std::uint32_t>();
            backend.json_error=static_cast<RecipeJsonError>(in["error"]|0);
            ok=report_z_backend_diagnostic(services,backend,source);
        } else {
            SessionLogger logger;logger.state=static_cast<SessionLogState>(in["state"].as<std::uint32_t>());
            BackendScopeTrace scope;scope.context=&capture;
            if(in["scope"].as<bool>())scope.emit=[](void* context,const char* tag,const char* file,std::uint32_t source,const char* name){
                auto row=static_cast<Capture*>(context)->events.createNestedObject();row["file"]=file;row["line"]=source;
                row["tag"]=tag;row["scope"]=name;return true;};
            ok=report_core_helper_diagnostic(services,logger,source,scope);
        }
        out["ok"]=ok;serializeJson(out,std::cout);std::cout<<'\n';
    }
}
