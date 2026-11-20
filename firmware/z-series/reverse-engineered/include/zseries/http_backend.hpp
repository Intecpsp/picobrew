// Recovered Z119 HttpBackend request machine, registration and recipe results.
#pragma once
#include "zseries/z_backend.hpp"
#include "zseries/program_list.hpp"
#include <algorithm>
#include <cstring>

namespace zseries { namespace oem {
template<std::size_t N> struct BackendText {
    std::array<char,N+1> text{};
    bool truncated=false,frozen=false;
    std::size_t length=0;
    void clear() noexcept {text.fill(0);truncated=false;frozen=false;length=0;}
    void append(const char* value) noexcept {if(!value||frozen)return;const auto n=std::strlen(value);
        const auto count=std::min(n,N-length);std::memcpy(text.data()+length,value,count);length+=count;text[length]=0;truncated=truncated||count!=n;}
    void set(const char* value) noexcept {clear();append(value);}
};
struct BackendState {
    bool ready=false,error=false,token_expired=false,registered=false,byte_146=false;
    BackendText<128> update_address,program_uri;
    BackendText<16> registration_token,alias;
    BackendText<32> error_text;
    ZBackendError backend_error=ZBackendError::None;
    std::int32_t dirty_sessions=-1,resumable_session_id=-1;
    SessionType last_session=SessionType::Unknown;
    void clear() noexcept;
    bool needs_update() const noexcept {return update_address.text[0]&&std::strcmp(update_address.text.data(),"-1")!=0;}
};
struct BackendRecipeLocation { BackendText<128> uri,guid; };
struct BackendCommand {
    CoreCommandId id=CoreCommandId::None;
    std::int32_t offset=0,reference_id=0;
    const char* search=nullptr;
    RecipeReference recipe;
    ProgramParameters parameters;
};
class HttpBackend {
public:
    HttpBackend(HttpClient& transport,BackendState& registered_state,ProgramBuffer& buffer,
                CoreEvent& output,CoreEventMailbox& queue) noexcept:
        client(transport),backend(registered_state),program(buffer),event(output),mailbox(queue){}
    HttpClient& client;BackendState& backend;ProgramBuffer& program;CoreEvent& event;CoreEventMailbox& mailbox;
    BackendHooks hooks;
    // Optional legacy value-only mode leaves both callbacks empty. The full
    // application binds this sink for original parser/program information.
    ConsoleServices diagnostics;
    const char* token=nullptr;const char* authorization=nullptr;
    const char* firmware_version="0.0.119";
    BoilerType boiler=BoilerType::Unknown;
    RecipeEnvironment environment{0,0};
    std::uint32_t state=0;
    // Arguments retained for the original line-based diagnostic callback.
    std::uint32_t diagnostic_argument=0;
    const char* diagnostic_text=nullptr;
    std::int32_t offset=0,reference_id=0;
    const char* search=nullptr;
    RecipeReference pending_recipe;
    ProgramParameters parameters;
    // The recovered OEM parser writes the same single location slot for every
    // BrewPen result; command handling forces its index to zero.
    BackendRecipeLocation local_recipe;
    BackendText<128> uri;
    std::array<char,4097> response{};
    std::size_t response_length=0;
    bool response_truncated=false,response_frozen=false;
    std::array<char,128> payload{};
    RecipeJsonError json_error=RecipeJsonError::Ok;
    ProgramBuildResult program_error=ProgramBuildResult::Ok;
    BackendResult callback_result=BackendResult::Ok;
    BackendResult registration();
    BackendResult request(const BackendCommand&);
    BackendResult transition(std::uint32_t);
    BackendResult tick(std::uint32_t now);
    BackendResult start_request(std::uint32_t running_state);
    BackendResult finish_response();
    BackendResult receive_headers(const HttpResponse&,bool&);
    BackendResult receive_body(const std::uint8_t*,std::size_t);
    HttpReceiver receiver() noexcept;
    // Concrete pinned JSON adapter (0x40117dac/0x40118060/0x40118218 and
    // recipe/resume wrappers). accepted is separate from service completion.
    BackendResult parse_response(bool& accepted);
    // The original explicit parser wrappers do not infer their format from
    // the current request state (including the network diagnostic's quirks).
    BackendResult parse_response_as(std::uint32_t running_state, bool& accepted);
    BackendResult assertion(std::uint32_t);
    BackendResult diagnostic(std::uint32_t);
private:
    BackendResult can_connect(bool&);
    BackendResult set_error(std::uint32_t,const char*);
    BackendResult endpoint(const char*,bool controller=false,bool include_id=false,std::int32_t id=0);
};
std::size_t serialize_backend_request(char*,std::size_t,std::uint32_t state,
    const char* firmware,BoilerType,std::int32_t offset,const char* search,std::int32_t reference_id);
}} // namespace zseries::oem
