// Complete Z119 Azure password (0x400fa684) and live integration (0x400fec74) diagnostics.
#pragma once
#include "zseries/azure.hpp"
#include "zseries/application_console.hpp"
namespace zseries { namespace oem {
ConsoleResult azure_password_tests(AzureService&,const ConsoleServices&);
ConsoleResult azure_console_self_test(AzureService&,const ConsoleServices&,const ApplicationConsoleHooks&);
}} // namespace zseries::oem
