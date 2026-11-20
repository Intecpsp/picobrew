// Source-evidenced backend assertion and diagnostic rendering.
#pragma once
#include "zseries/console.hpp"
#include <cstddef>
#include <cstdint>
namespace zseries { namespace oem {
class HttpBackend;
class ZBackend;
class SessionLogger;
enum class BackendDiagnosticDomain : std::uint8_t { Http, Session, Helpers };
enum class BackendDiagnosticKind : std::uint8_t { Plain, Text, Integer, JsonError, Scope };
struct BackendAssertionDescription {const char* file;const char* expression;};
struct BackendDiagnosticDescription {
    const char* file;const char* tag;ConsoleLevel level;
    BackendDiagnosticKind kind;const char* format;
};
const BackendAssertionDescription* backend_assertion_description(BackendDiagnosticDomain,std::uint32_t) noexcept;
const BackendDiagnosticDescription* backend_diagnostic_description(BackendDiagnosticDomain,std::uint32_t) noexcept;
bool report_backend_assertion(const ConsoleServices&,BackendDiagnosticDomain,std::uint32_t);
bool report_http_backend_diagnostic(const ConsoleServices&,const HttpBackend&,std::uint32_t);
bool report_z_backend_diagnostic(const ConsoleServices&,const ZBackend&,std::uint32_t);
struct BackendScopeTrace {
    void* context=nullptr;
    // Original ScopeLogger emits its paired entry/exit prefixes and elapsed
    // time. Ordinary ConsoleLevel::Debug cannot represent that prefix pair.
    bool (*emit)(void*,const char* tag,const char* file,std::uint32_t line,const char* name)=nullptr;
};
bool report_core_helper_diagnostic(const ConsoleServices&,const SessionLogger&,std::uint32_t,const BackendScopeTrace& = {});
}} // namespace zseries::oem
