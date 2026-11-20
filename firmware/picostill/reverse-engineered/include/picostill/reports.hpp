#pragma once
#include "picostill/controller.hpp"
#include <cstddef>
namespace picostill { namespace oem {
struct ReportPorts {
    virtual ~ReportPorts() = default;
    virtual void integer(const char* scope,const char* name,std::uint32_t value) = 0;
    virtual void floating(const char* scope,const char* name,float value) = 0;
    virtual void format_decimal(double value,int width,unsigned precision,char* destination,std::size_t capacity) = 0;
    virtual void console(const char* message) = 0;
};
class ApplicationReports final:public ControllerPorts {
public:
    ApplicationReports(HardwarePorts& hardware_ports,ReportPorts& ports,Diagnostics& diagnostics) noexcept
        :hardware_ports_(hardware_ports),ports_(ports),diagnostics_(diagnostics) {}
    // Original constructors install a callback to the owning controller.
    // Bind once after construction, before hardware setup or any polling.
    void bind(Hardware& hardware,Controller& controller) noexcept{hardware_=&hardware;controller_=&controller;}
    std::array<char,128> csv={{0}};
    std::uint32_t length=0,remaining=127;
    void sensors_sampled() override{controller_changed();}
    void controller_changed() override;
    void snapshot_report(const HardwareSnapshot& snapshot) override;
    void full_test_report(const FullTestResult& result) override;
    void console_message(const char* message) override{ports_.console(message);}
private:
    HardwarePorts& hardware_ports_;ReportPorts& ports_;Diagnostics& diagnostics_;
    Hardware* hardware_=nullptr;Controller* controller_=nullptr;
    void append(const char* value,std::size_t size);
};
}} // namespace picostill::oem
