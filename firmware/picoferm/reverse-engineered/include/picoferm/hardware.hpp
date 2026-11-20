// Reconstructed from the preserved PicoFerm 0.2.7 image.
#pragma once
#include "picoferm/core.hpp"

namespace picoferm { namespace oem {
struct HardwarePorts {
    virtual ~HardwarePorts() = default;
    virtual std::uint32_t millis() = 0;
    virtual void pin_mode(unsigned pin,unsigned mode) = 0;
    virtual std::int32_t analog_read(unsigned pin) = 0;
    virtual void wire_begin(unsigned sda,unsigned scl) = 0;
    virtual void wire_begin_transmission(unsigned address) = 0;
    virtual void wire_write(std::uint8_t value) = 0;
    virtual void wire_end_transmission() = 0;
    virtual void wire_request(unsigned address,unsigned count) = 0;
    virtual std::uint32_t wire_read() = 0;
};
class Adc {
public:
    explicit Adc(HardwarePorts& ports) noexcept : ports_(ports) {}
    std::uint8_t address = 0;
    float reference_voltage = 3.3f;
    bool begin(unsigned sda,unsigned scl,unsigned device); // 0x40202c90
    void initialize(); // 0x40202c14
    void write(unsigned reg,unsigned value); // 0x402029ac
    std::uint8_t read(unsigned reg); // 0x402029e8
    void set(unsigned reg,unsigned bit); // 0x40202a28
    void clear(unsigned reg,unsigned bit); // 0x40202a7c
    void wait_set(unsigned reg,unsigned bit); // 0x40202b70
    void wait_clear(unsigned reg,unsigned bit); // 0x40202ba8
    void calibrate(); // 0x40202bec
    void pressure_channel() { clear(2,7); } // 0x40202abc
    void temperature_channel() { set(2,7); } // 0x40202a68
    void configure_rate(); // 0x40202ad0
    void configure_gain(); // 0x40202b00
    void configure_reference(); // 0x40202b34
    std::uint32_t read24(unsigned reg); // 0x40202cd8
    std::int32_t sample(); // 0x40202d30
private:
    HardwarePorts& ports_;
};
struct AcquisitionEvents {
    virtual ~AcquisitionEvents() = default;
    virtual void measurement(float temperature,std::uint32_t raw_pressure) = 0;
};
class Acquisition {
public:
    Acquisition(Adc& adc,Ports& ports,AcquisitionEvents& events) noexcept
        : adc_(adc),ports_(ports),events_(events) {}
    std::uint32_t state=0,deadline=0;
    std::int32_t raw_temperature=0,raw_pressure=0;
    bool continuous=false;
    bool complete() const noexcept { return state==13; }
    bool idle() const noexcept { return state==0 || complete(); }
    void start(); // 0x4020319c
    void advance(std::uint32_t now); // 0x40203210, returning states 0..12
    void poll(HardwarePorts& hardware); // acquisition portion of 0x40207ba8
    static float temperature_fahrenheit(std::int32_t raw);
private:
    Adc& adc_;Ports& ports_;AcquisitionEvents& events_;
};
float battery_voltage(HardwarePorts& ports); // 0x40202db0

enum class Animation : std::uint32_t {
    Off=0,AccessPointMode=1,Updating=2,Busy=3,TalkingToPicobrew=4,
    FermentationInProgress=5,FermentationDone=6,FermentationError=7,
    AccountNotAssociated=8,GenericError=9
};
struct AnimationDefinition { std::int32_t mode,steps,period;bool enabled[3]; };
extern const AnimationDefinition animations[10]; // original 0x3ffe9d30
const char* animation_name(Animation animation) noexcept;
struct LedEvents {virtual ~LedEvents()=default;virtual void animation_changed(Animation animation)=0;};
class Leds {
public:
    Leds(Ports& ports,HardwarePorts& hardware,LedEvents& events) noexcept
        : ports_(ports),hardware_(hardware),events_(events) {}
    std::uint8_t pins[3]={4,5,12};
    Animation animation=static_cast<Animation>(0xffffffffu);
    std::int32_t mode=0,steps=0,period=0;
    bool enabled[3]={false,false,false};
    std::uint32_t deadline=0,toggle=0,rotating_index=0;
    void initialize(); // setup 0x40206e80
    void set_active(std::uint32_t level); // 0x4020565c
    void set(Animation value); // 0x402056a4
    void set_text(const char* value); // 0x402058ac
    void poll(); // LED portion of 0x40207ba8
private:
    Ports& ports_;HardwarePorts& hardware_;LedEvents& events_;
};
struct ButtonEvents {virtual ~ButtonEvents()=default;virtual void long_press()=0;};
class Button {
public:
    Button(Ports& ports,HardwarePorts& hardware,ButtonEvents& events) noexcept
        : ports_(ports),hardware_(hardware),events_(events) {}
    bool pressed=false,long_reported=false,woke_board=false;
    std::uint32_t deadline=0;
    void initialize(std::uint8_t persisted_button); // setup 0x40206e80
    void poll(); // button portion of 0x40207ba8
private:
    Ports& ports_;HardwarePorts& hardware_;ButtonEvents& events_;
};
}} // namespace picoferm::oem
