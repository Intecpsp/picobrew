// Recovered Z119 ZBackend session HTTP service (Core +0xfff0).
#pragma once
#include "zseries/http_client.hpp"
#include "zseries/program_repository.hpp"
#include "zseries/program_recipe_json.hpp"

namespace zseries { namespace oem {
struct BackendHooks {
    void* context=nullptr;
    bool (*network_ready)(void*,bool&)=nullptr;
    bool (*diagnostic)(void*,std::uint32_t)=nullptr;
    bool (*assertion)(void*,std::uint32_t)=nullptr;
};
enum class BackendResult { Ok, MissingService, ServiceFailure, InvalidSpan };
struct SessionSnapshot {
    SessionType type=SessionType::Unknown;
    std::uint32_t session_id=0,recipe_id=0xffffffffu;
    ZProgramId program=ZProgramId::None;
    const char* name=nullptr;
    ProgramStats stats;
    ProgramParameters parameters;
    bool group_session=false;
    const char* recipe_guid=nullptr;
    const char* still_uid=nullptr;
    const char* still_version=nullptr;
};
struct SessionLogSnapshot {
    std::uint32_t session_id=0;
    double thermoblock=0,wort=0,ambient=0,drain=0,target=0;
    const char* step_name=nullptr;
    std::uint8_t valve_position=0;
    bool keg_pump=false,drain_pump=false;
    std::uint32_t pause_reason=0,seconds_remaining=0;
    std::int32_t rssi=0;
};
// Concrete pinned ArduinoJson adapters. Serialization reproduces the OEM
// bounded output behavior, including truncated output rather than a fabricated
// successful server result. These functions perform no network I/O.
struct BackendSerialization {std::size_t bytes=0;BackendResult result=BackendResult::Ok;};
BackendSerialization serialize_session_json(char*,std::size_t,const SessionSnapshot&,const BackendHooks&);
std::size_t serialize_session_log_json(char*,std::size_t,const SessionLogSnapshot&,const HttpTiming&,const BackendHooks&);
RecipeJsonError parse_created_session_json(char*,std::size_t,std::uint32_t&);

class ZBackend {
public:
    explicit ZBackend(HttpClient& value) noexcept:client(value){}
    HttpClient& client;
    BackendHooks hooks;
    const char* token=nullptr;
    // The historical shared OEM credential is deliberately not embedded in
    // reconstructed source. Runtime composition supplies authorization.
    const char* authorization=nullptr;
    std::uint32_t state=0,created_session_id=0;
    bool completed=false,uri_truncated=false,response_truncated=false,response_frozen=false;
    std::array<char,129> uri{};
    std::array<char,1025> response{};
    std::array<char,1024> payload{};
    std::size_t response_length=0;
    BackendResult callback_result=BackendResult::Ok;
    RecipeJsonError json_error=RecipeJsonError::Ok;
    std::uint32_t diagnostic_argument=0;

    BackendResult can_sync(bool&);
    BackendResult endpoint(const char* type,const char* request_token,std::int32_t id);
    BackendResult create(const SessionSnapshot&);
    BackendResult end(const SessionSnapshot&);
    BackendResult log(const SessionLogSnapshot&);
    BackendResult tick(std::uint32_t now);
    BackendResult cancel();
    BackendResult receive_headers(const HttpResponse&,bool& accepted);
    BackendResult receive_body(const std::uint8_t*,std::size_t);
    BackendResult receive_complete();
    HttpReceiver receiver() noexcept;
private:
    void clear_request() noexcept;
    void append_uri(const char*) noexcept;
    BackendResult diagnose(std::uint32_t);
    BackendResult check(std::uint32_t);
    BackendResult configure();
    BackendResult session(const SessionSnapshot&,bool ending);
};
}} // namespace zseries::oem
