#include "zseries/network_console_tests.hpp"
#include <cstdio>
#include <cstring>

namespace zseries { namespace oem {
namespace {
struct Scope {
    const ApplicationConsoleHooks& hooks;const char* name;std::uint32_t line;bool active=false;
    Scope(const ApplicationConsoleHooks& h,const char* n,std::uint32_t l):hooks(h),name(n),line(l){}
    bool begin(){active=hooks.test_scope&&hooks.test_scope(hooks.context,true,"HBE","HttpBackend.h",line,name);return active;}
    bool end(){if(!active)return false;active=false;return hooks.test_scope(hooks.context,false,"HBE","HttpBackend.h",line,name);}
    ~Scope(){if(active)(void)end();}
};
struct Identifier {
    HttpBackend& backend;std::array<char,13>& shared;std::array<char,13> saved;const char* token;bool active=true;
    Identifier(HttpBackend& b,std::array<char,13>& id,const char* test):backend(b),shared(id),saved(id),token(b.token){
        std::memcpy(shared.data(),test,12);shared[12]=0;backend.token=shared.data();
    }
    void restore(){if(active){shared=saved;backend.token=token;active=false;}}
    ~Identifier(){restore();}
};
bool require(const ConsoleServices& s,bool value,std::uint32_t line,const char* expression){
    return value||(s.assertion&&s.assertion(s.context,"HttpBackend.h",line,expression));
}
bool log(const ConsoleServices& s,std::uint32_t line,const char* label,const char* value){
    char text[80];const int count=::snprintf(text,sizeof text,"%s = %s",label,value);
    return count>=0&&static_cast<std::size_t>(count)<sizeof text&&
        s.log(s.context,ConsoleLevel::Warning,"HBE","HttpBackend.h",line,text);
}
bool request(HttpBackend& backend,std::uint32_t state,const ApplicationConsoleHooks& hooks){
    if(backend.start_request(state)!=BackendResult::Ok)return false;
    while(backend.client.state!=HttpState::Idle){
        if(!hooks.yield(hooks.context)||backend.client.tick(hooks.millis(hooks.context))!=HttpResult::Ok||
           backend.callback_result!=BackendResult::Ok)return false;
    }
    return true;
}
#define TEST(value) do{if(!(value))return false;}while(false)
#define CHECK(value,line,expression) TEST(require(services,(value),(line),(expression)))
}
bool http_backend_network_console_test(HttpBackend& backend,std::array<char,13>& shared_identifier,
        const ConsoleServices& services,const ApplicationConsoleHooks& hooks,
        const HttpClientTestResources& resources){
    if(!services.assertion||!services.log||!hooks.test_scope||!hooks.millis||!hooks.yield||!resources.text)return false;
    const char* test_identifier=resources.text(resources.context,0x3f418735);
    if(!test_identifier)return false;
    std::size_t length=0;while(length<13&&test_identifier[length])++length;if(length!=12)return false;
    Identifier identifier(backend,shared_identifier,test_identifier);
    TEST(log(services,0x552,"temp",identifier.saved.data()));TEST(log(services,0x553,"backend._uid",shared_identifier.data()));
    bool accepted=false;
    {Scope scope(hooks,"getType(State::GetBackendStateDownloading)",0x559);TEST(scope.begin());TEST(request(backend,2,hooks));
        TEST(backend.parse_response_as(2,accepted)==BackendResult::Ok);CHECK(accepted,0x55c,"backend.parseBackendState()");
        CHECK(backend.backend.registered,0x55d,"backend._backState.IsRegistered");
        CHECK(!backend.backend.token_expired,0x55e,"!backend._backState.IsCodeExpired");
        CHECK(backend.backend.backend_error==ZBackendError::None,0x55f,"backend._backState.BackendError == ZBackendError::None");
        CHECK(backend.backend.resumable_session_id==-1,0x560,"backend._backState.ResumableSessionID == -1");TEST(scope.end());}
    const char* names[]={"getType(State::GetMyRecipesDownloading)","getType(State::GetPaksShippedListDownloading)","getType(State::GetCurrentZKitsListDownloading)"};
    const std::uint32_t states[]={4,12,18},scope_lines[]={0x563,0x56d,0x577},lines[]={0x566,0x570,0x57a};
    for(unsigned i=0;i<3;++i){Scope scope(hooks,names[i],scope_lines[i]);TEST(scope.begin());TEST(request(backend,states[i],hooks));
        TEST(backend.parse_response_as(4,accepted)==BackendResult::Ok);CHECK(accepted,lines[i],"backend.parseRecipesList()");
        if(i==2){CHECK(backend.event.count>0&&backend.event.count<=20,0x57b,"backend._ev.count > 0 && backend._ev.count <= backend._ev.list.max_size()");
            CHECK(backend.event.offset==0,0x57c,"backend._ev.offset == 0");CHECK(!backend.event.byte_ddc,0x57d,"!backend._ev.truncated");}
        else{CHECK(backend.event.count==(i?0u:11u),lines[i]+1,i?"backend._ev.count == 0":"backend._ev.count == 11");
            CHECK(backend.event.offset==0,lines[i]+2,"backend._ev.offset == 0");
            CHECK(backend.event.total==(i?0u:11u),lines[i]+3,i?"backend._ev.totalResults == 0":"backend._ev.totalResults == 11");
            CHECK(!backend.event.byte_ddc,lines[i]+4,"!backend._ev.truncated");}TEST(scope.end());}
    {Scope scope(hooks,"getType(State::GetPicoPakRecipeRefDownloading)",0x580);TEST(scope.begin());
        backend.pending_recipe.kind=RecipeKind::PicoPak;backend.reference_id=1;TEST(request(backend,16,hooks));
        TEST(backend.parse_response_as(16,accepted)==BackendResult::Ok);CHECK(accepted,0x585,"backend.parseRecipeRef()");
        CHECK(backend.event.count==1,0x586,"backend._ev.count == 1");const auto reference=backend.event.recipes[0];
        CHECK(std::strcmp(reference.name.data(),"Not Yet-ie Imperial Stout")==0,0x588,"CString::areEquals(ref.name.c_ptr(), \"Not Yet-ie Imperial Stout\")");
        CHECK(reference.id==1,0x589,"ref.id == testPicoRecipeid");CHECK(reference.kind==RecipeKind::PicoPak,0x58a,"ref.kind == RecipeKind::PicoPak");TEST(scope.end());}
    {Scope scope(hooks,"getType(State::GetPicoPakRecipeDownloading)",0x58d);TEST(scope.begin());backend.pending_recipe.id=1;
        TEST(request(backend,14,hooks));TEST(backend.parse_response_as(14,accepted)==BackendResult::Ok);
        CHECK(accepted,0x591,"backend.parsePicoPakRecipe()");TEST(scope.end());}
    {Scope scope(hooks,"getType(State::GetZymaticRecipeRefDownloading)",0x594);TEST(scope.begin());
        backend.pending_recipe.kind=RecipeKind::Zymatic;backend.reference_id=146530;TEST(request(backend,8,hooks));
        TEST(backend.parse_response_as(8,accepted)==BackendResult::Ok);CHECK(accepted,0x599,"backend.parseRecipeRef()");
        CHECK(backend.event.count==1,0x59a,"backend._ev.count == 1");const auto reference=backend.event.recipes[0];
        CHECK(std::strcmp(reference.name.data(),"Atomic Amber")==0,0x59c,"CString::areEquals(ref.name.c_ptr(), \"Atomic Amber\")");
        CHECK(reference.id==146530,0x59d,"ref.id == testZymaticRecipeid");CHECK(reference.kind==RecipeKind::Zymatic,0x59e,"ref.kind == RecipeKind::Zymatic");TEST(scope.end());}
    {Scope scope(hooks,"getType(State::GetZymaticRecipeDownloading)",0x5a1);TEST(scope.begin());backend.pending_recipe.id=146530;
        TEST(request(backend,6,hooks));
        // Actual 0x4011b6cc invokes the PicoPak parser 0x401166ec here.
        // Preserve that diagnostic quirk without changing backend.state.
        TEST(backend.parse_response_as(14,accepted)==BackendResult::Ok);CHECK(accepted,0x5a5,"backend.parsePicoPakRecipe()");TEST(scope.end());}
    {Scope scope(hooks,"getType(State::GetResumableSessionDownloading)",0x5a8);TEST(scope.begin());backend.backend.resumable_session_id=12345;
        TEST(request(backend,22,hooks));TEST(backend.parse_response_as(22,accepted)==BackendResult::Ok);CHECK(accepted,0x5ac,"backend.parseResumableSession()");TEST(scope.end());}
    identifier.restore();TEST(log(services,0x5af,"backend._uid",shared_identifier.data()));TEST(log(services,0x5b0,"testUid",test_identifier));
    CHECK(shared_identifier[0]!=0,0x5b1,"!CString::isNullOrEmpty(backend._uid.c_ptr())");
    CHECK(std::strcmp(shared_identifier.data(),test_identifier)!=0,0x5b2,"!CString::areEquals(backend._uid.c_ptr(), testZUid.c_ptr())");
    return backend.transition(0)==BackendResult::Ok;
}
#undef TEST
#undef CHECK
}} // namespace zseries::oem
