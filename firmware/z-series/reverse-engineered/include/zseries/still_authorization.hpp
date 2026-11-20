// Concrete Z119 PicoStill backend request, receiver and authorization record.
#pragma once
#include "zseries/console.hpp"
#include "zseries/http_client.hpp"
#include "zseries/program_recipe_json.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace zseries { namespace oem {
struct PicoStillObserved;
struct StillAuthorizationRecord {
    std::array<char,17> uid{};
    std::int32_t profile_id = -1;
    std::array<char,33> cleaned_ack_date{}, reserved_date{};
    bool uid_truncated = false, cleaned_truncated = false;
    bool authorized() const noexcept { return uid[0] != 0; }
    bool cleaned() const noexcept { return cleaned_ack_date[0] != 0; }
};
enum class StillAuthorizationResult { Ok, MissingService, ServiceFailure, InvalidSpan };
struct StillAuthorizationServices {
    void* context = nullptr;
    // Select the actual WiFiClientSecure owned at original object+0x14.
    // This service must bind that socket to the shared HTTP transport.
    bool (*select_wifi_tls)(void*) = nullptr;
    ConsoleServices diagnostics;
};
struct StillAuthorizationJson {
    const char* uid = nullptr;
    const char* cleaned_ack_date = nullptr;
    std::int32_t profile_id = 0;
};
std::size_t serialize_still_request_json(char*,std::size_t,const char* machine_uid,
                                        const char* still_uid,bool cleaned_ack);
RecipeJsonError parse_still_authorization_json(char*,std::size_t,StillAuthorizationJson&);
class StillAuthorization {
public:
    explicit StillAuthorization(HttpClient& client) noexcept : client(client) {}
    HttpClient& client;
    StillAuthorizationServices services;
    StillAuthorizationRecord record;
    const char* authorization = nullptr;
    bool pending = false;
    std::array<char,513> buffer{};
    std::size_t length = 0, remaining = 512;
    bool truncated = false, frozen = false;
    RecipeJsonError json_error = RecipeJsonError::Ok;
    StillAuthorizationResult callback_result = StillAuthorizationResult::Ok;

    // 0x4011a228. Caller token and selected still UID must be NUL-terminated.
    StillAuthorizationResult request(const char* machine_uid,const char* still_uid,bool cleaned_ack);
    // 0x401164ac follows the owner's HTTP tick; it does not tick HTTP itself.
    StillAuthorizationResult tick();
    StillAuthorizationResult parse();
    StillAuthorizationResult prepare_body(const char*,const char*,bool);
    StillAuthorizationResult receive_headers(const HttpResponse&,bool& accepted);
    StillAuthorizationResult receive_body(const std::uint8_t*,std::size_t);
    HttpReceiver receiver() noexcept;
    void observe(PicoStillObserved&) const noexcept;
    void reset_buffer() noexcept;
private:
    StillAuthorizationResult diagnose(const char* file,std::uint32_t line,const char* message);
    StillAuthorizationResult assertion(const char* file,std::uint32_t line,const char* expression);
    StillAuthorizationResult overflow(std::size_t dropped);
    StillAuthorizationResult copy_record(const StillAuthorizationJson&);
};
}} // namespace zseries::oem
