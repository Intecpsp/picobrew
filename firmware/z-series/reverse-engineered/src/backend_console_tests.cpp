#include "zseries/backend_console_tests.hpp"
#include "zseries/backend_diagnostics.hpp"
#include "zseries/http_backend.hpp"
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
namespace zseries { namespace oem {
namespace {
#include "backend_console_documents.inc"
struct Scope {
    const ApplicationConsoleHooks& hooks;const char* name;std::uint32_t line;bool active=false;
    Scope(const ApplicationConsoleHooks& h,const char* n,std::uint32_t l):hooks(h),name(n),line(l){}
    bool begin(){active=hooks.test_scope&&hooks.test_scope(hooks.context,true,"HBE","HttpBackend.h",line,name);return active;}
    bool end(){if(!active)return false;active=false;return hooks.test_scope(hooks.context,false,"HBE","HttpBackend.h",line,name);}
    ~Scope(){if(active)(void)end();}
};
bool require(const ConsoleServices& s,bool value,std::uint32_t line,const char* expression){
    return value||(s.assertion&&s.assertion(s.context,"HttpBackend.h",line,expression));
}
struct Workspace {
    HttpClient client;BackendState state;ProgramBuffer program;CoreEvent event;CoreEventMailbox mailbox;
    HttpBackend backend;const ConsoleServices& diagnostics;
    explicit Workspace(const ConsoleServices& s):backend(client,state,program,event,mailbox),diagnostics(s){
        backend.diagnostics=s;backend.environment=RecipeEnvironment(0,96);backend.hooks.context=this;
        backend.hooks.assertion=[](void* p,std::uint32_t line){auto& w=*static_cast<Workspace*>(p);return report_backend_assertion(w.diagnostics,BackendDiagnosticDomain::Http,line);};
        backend.hooks.diagnostic=[](void* p,std::uint32_t line){auto& w=*static_cast<Workspace*>(p);return report_http_backend_diagnostic(w.diagnostics,w.backend,line);};
    }
    bool parse(std::uint32_t state,const char* text,bool& accepted){
        backend.state=state;backend.response.fill(0);backend.response_length=std::strlen(text);
        if(backend.response_length>=backend.response.size())return false;
        std::memcpy(backend.response.data(),text,backend.response_length+1);
        backend.response_frozen=false;backend.response_truncated=false;
        return backend.parse_response(accepted)==BackendResult::Ok;
    }
};
#define CHECK(value,line,expression) do{if(!require(services,(value),(line),(expression)))return false;}while(false)
#define TEST(value) do{if(!(value))return false;}while(false)
}
bool http_backend_console_self_test(const ConsoleServices& services,const ApplicationConsoleHooks& hooks){
    if(!services.assertion||!services.log||!hooks.test_scope)return false;
    // A temporary owned workspace keeps the larger portable objects off the
    // Core task's stack. Allocation failure is an explicit test failure.
    std::unique_ptr<Workspace> storage(new(std::nothrow) Workspace(services));if(!storage)return false;
    auto& w=*storage;bool accepted=false;
    {Scope scope(hooks,"HttpBackend::parseRecipesList() SimpleCase",0x459);TEST(scope.begin());
        TEST(w.parse(4,backend_test_document_0,accepted));CHECK(accepted,0x462,"parseRecipesList(bb, source, ev, bpr)");CHECK(w.event.count==4,0x463,"ev.count == 4");
        const std::uint32_t ids[]={61511,121731,121732,121733};const char* names[]={"Pico Pale Ale","Party Porter","Mantis TobogganFest","Viva Cascadia"};const std::uint32_t kinds[]={0,1,2,0};
        for(unsigned i=0;i<4;++i){const auto& recipe=w.event.recipes[i];CHECK(recipe.id==ids[i],0x442,"id == a.id");CHECK(std::strcmp(names[i],recipe.name.data())==0,0x443,"CString::areEquals(name, a.name.c_ptr())");CHECK(static_cast<unsigned>(recipe.kind)==kinds[i],0x444,"kind == a.kind");}
        CHECK(std::strcmp(w.backend.local_recipe.uri.text.data(),"Some URI")==0,0x469,"CString::areEquals(\"Some URI\", bpr.recipes[0].RecipeUri.c_ptr())");TEST(scope.end());}
    {Scope scope(hooks,"HttpBackend::parseRecipesList() WorstCase",0x46d);TEST(scope.begin());
        TEST(w.parse(4,backend_test_document_1,accepted));CHECK(accepted,0x493,"parseRecipesList(bb, source, ev, bpr)");CHECK(w.event.byte_ddc,0x494,"ev.truncated");CHECK(w.event.count==20,0x495,"ev.count == ev.list.max_size()");TEST(scope.end());}
    const char* names[]={"HttpBackend::parseZymaticRecipe() SimpleCase","HttpBackend::parseZymaticRecipe() NoTypeCode","HttpBackend::parseZymaticRecipe() Coffee","HttpBackend::parseZymaticRecipe() WorstCase"};
    const std::uint32_t scopes[]={0x499,0x4ae,0x4bb,0x4c8},lines[]={0x4a8,0x4b5,0x4c2,0x4e4};
    const double volumes[]={1.23456,11.36,12.3456,123.456};const char* documents[]={backend_test_document_2,backend_test_document_3,backend_test_document_4,backend_test_document_5};
    const char* expressions[]={"Quantity::areNearEquals(pgm.params.water.getLiters(), 1.23456)","pgm.params.water.getLiters() == Program::DefaultZymaticRecipeWaterL","Quantity::areNearEquals(pgm.params.water.getLiters(), 12.3456)","Quantity::areNearEquals(pgm.params.water.getLiters(), 123.456)"};
    for(unsigned i=0;i<4;++i){Scope scope(hooks,names[i],scopes[i]);TEST(scope.begin());TEST(w.parse(6,documents[i],accepted));
        CHECK(accepted,lines[i],"parseZymaticRecipe(bb, source, ev, pgm, sensorsState, p)");CHECK(program_validation_error(w.program.record().view)==nullptr,lines[i]+1,"pgm.pgm.isValid()");
        double water=0;TEST(quantity_in(w.program.parameters.water,Unit::Liter,water));CHECK(i==1?water==volumes[i]:std::fabs(water-volumes[i])<=1e-5,lines[i]+2,expressions[i]);TEST(scope.end());}
    for(unsigned i=0;i<2;++i){Scope scope(hooks,i?"HttpBackend::parsePicoPakRecipe() Tweaking":"HttpBackend::parsePicoPakRecipe() SimpleCase",i?0x4ff:0x4f8);TEST(scope.begin());
        if(i)w.backend.parameters.abv=w.backend.parameters.ibu=10;
        TEST(w.parse(14,backend_test_document_6,accepted));CHECK(accepted,i?0x503:0x4fa,"parsePicoPakRecipe(bb, source, ev, pgm, sensorsState, p)");
        CHECK(program_validation_error(w.program.record().view)==nullptr,i?0x504:0x4fb,"pgm.pgm.isValid()");TEST(scope.end());}
    {Scope scope(hooks,"HttpBackend::parseResumableSession() SimpleCase",0x516);TEST(scope.begin());TEST(w.parse(22,backend_test_document_7,accepted));
        CHECK(accepted,0x518,"parseResumableSession(bb, source, ev, pgm, sensorsState, p)");CHECK(program_validation_error(w.program.record().view)==nullptr,0x519,"pgm.pgm.isValid()");TEST(scope.end());}
    {Scope scope(hooks,"HttpBackend::parseBackendState() SimpleCase",0x51d);TEST(scope.begin());TEST(w.parse(2,backend_test_document_8,accepted));
        CHECK(accepted,0x527,"parseBackendState(bb, source, ev, backendState)");CHECK(!w.state.needs_update(),0x528,"!backendState.needsUpdate()");CHECK(w.state.dirty_sessions<3,0x529,"!backendState.needsClean()");
        CHECK(w.state.last_session==SessionType::BrewZRecipe,0x52a,"SessionType::BrewZRecipe == backendState.LastSessionType");CHECK(w.state.registered,0x52b,"backendState.IsRegistered");
        CHECK(std::strcmp(w.state.registration_token.text.data(),"123456789")==0,0x52c,"CString::areEquals(\"123456789\", backendState.RegistrationCode.c_ptr())");
        CHECK(std::strcmp(w.state.alias.text.data(),"MyAlias")==0,0x52d,"CString::areEquals(\"MyAlias\", backendState.Alias.c_ptr())");TEST(scope.end());}
    {Scope scope(hooks,"HttpBackend::parseBackendState() WorstCase",0x531);TEST(scope.begin());TEST(w.parse(2,backend_test_document_9,accepted));
        CHECK(accepted,0x53d,"parseBackendState(bb, source, ev, backendState)");CHECK(w.state.needs_update(),0x53e,"backendState.needsUpdate()");
        CHECK(std::strcmp(w.state.update_address.text.data(),"https://picodevblob.blob.core.windows.net/firmware/zseries/zseries_0_0_1.bin")==0,0x53f,"CString::areEquals(\"https://picodevblob.blob.core.windows.net/firmware/zseries/zseries_0_0_1.bin\", backendState.UpdateAddress.c_ptr())");
        CHECK(!w.state.registered,0x540,"!backendState.IsRegistered");CHECK(w.state.dirty_sessions>=3,0x541,"backendState.needsClean()");TEST(scope.end());}
    return true;
}
bool backend_console_test(std::uint32_t callback,CoreApplication&,
        const ConsoleServices& services,const ApplicationConsoleHooks& hooks){
    switch(callback){
    case 0x401188f0:case 0x40119378:return http_backend_console_self_test(services,hooks);
    case 0x4011dc20:case 0x4011ec38:return z_backend_console_self_test(services,hooks);
    default:return false;
    }
}
#undef CHECK
#undef TEST
}} // namespace zseries::oem
