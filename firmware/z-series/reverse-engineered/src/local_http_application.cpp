#include "zseries/local_http_application.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace zseries { namespace oem {
LocalHttpResult route_local_http_post(LocalHttpServer& server, ProgramBuffer& program,
        const RecipeEnvironment& environment, LocalRecipePublication& publication) {
    const auto& response = server.services.response;
    if (!response.headers || !response.write) return LocalHttpResult::MissingService;
    char error[129] = "BAD REQUEST";
    if (std::strcmp(server.parser.request.path.c_str(), "/recipe") == 0) {
        server.frozen = true;
        const auto parsed = parse_local_recipe(program, server.buffer.data(), server.length,
                                               environment, publication.notification());
        error[0] = 0;
        if (parsed.json_error != RecipeJsonError::Ok) {
            ::snprintf(error, sizeof error, "deserializeJson error %u %s",
                static_cast<unsigned>(parsed.json_error), recipe_json_error_name(parsed.json_error));
        } else if (parsed.program_error != ProgramBuildResult::Ok) {
            // Preserve this reconstruction's bounded ProgramBuffer policy.
            // OEM diagnostic assertions return: unnamed recipes can continue
            // to ProgramLoaded and HTTP 200. Rejecting them here with HTTP 400
            // is an explicit input-validation deviation; see the evidence.
            ::snprintf(error, sizeof error, "program build error %u", static_cast<unsigned>(parsed.program_error));
        }
    }
    const std::size_t size = std::strlen(error);
    if (!response.headers(response.context, size ? 400 : 200, static_cast<std::int32_t>(size),
                          size ? "text/plain" : nullptr)) return LocalHttpResult::ServiceFailure;
    if (size && !response.write(response.context, reinterpret_cast<const std::uint8_t*>(error), size))
        return LocalHttpResult::ServiceFailure;
    return LocalHttpResult::Ok;
}

LocalHttpApplication::LocalHttpApplication(LocalHttpServer& server, ProgramBuffer& program,
        LocalRecipePublication& publication, const HeaterSensors& sensors,
        const HeaterController& heater, const AuxiliarySensors& auxiliary,
        const z_msp_state& msp, const ProgramRuntime& runtime, const std::int32_t& rssi) noexcept
    : server(server), program(program), publication(publication), sensors(sensors),
      heater(heater), auxiliary(auxiliary), msp(msp), runtime(runtime), rssi(rssi) {}

void LocalHttpApplication::bind() noexcept {
    server.parser.services.decimal_integer = [](void*, const char* text) { return static_cast<std::int32_t>(std::atoi(text)); };
    server.services.application_context = this;
    server.services.post = [](void* context, LocalHttpServer& value) {
        auto& app = *static_cast<LocalHttpApplication*>(context);
        return route_local_http_post(value, app.program,
            RecipeEnvironment(app.auxiliary.pressure_pa, app.sensors.estimated_boil_celsius), app.publication) == LocalHttpResult::Ok;
    };
    auto& response = server.services.response;
    response.context = this;
    response.headers = [](void* context, std::uint32_t status, std::int32_t length, const char* type) {
        auto& app = *static_cast<LocalHttpApplication*>(context);
        return write_local_http_headers(app.server.services.transport, status, length, type) == LocalHttpResult::Ok;
    };
    response.write = [](void* context, const std::uint8_t* bytes, std::size_t size) {
        const auto& io = static_cast<LocalHttpApplication*>(context)->server.services.transport;
        return io.write && io.write(io.context, bytes, size);
    };
    response.state = [](void* context) {
        auto& app = *static_cast<LocalHttpApplication*>(context);
        const auto value = local_http_state(app.sensors, app.heater, app.msp, app.runtime, app.rssi);
        return send_local_http_state(app.server, value) == LocalHttpResult::Ok;
    };
}
}} // namespace zseries::oem
