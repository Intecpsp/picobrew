namespace {
bool console_thermistor(const ConsoleServices& services,const ThermistorParameters& parameters,std::int32_t raw,double& celsius){
    if(raw<MinimumRawAdc||raw>MaximumRawAdc){
        char message[128];const bool below=raw<MinimumRawAdc;
        const int size=::snprintf(message,sizeof message,below?"rawAdc %d below minimum %s":"rawAdc %d above maximum %s",raw,to_string(parameters.type));
        if(size<0||static_cast<std::size_t>(size)>=sizeof message||!services.log||
           !services.log(services.context,ConsoleLevel::Error,"ADC","MAX11613.h",below?0x4e:0x51,message))return false;
    }
    celsius=thermistor_celsius(parameters,raw);return true;
}
}
bool ApplicationConsole::Implementation::information_msp() {
    static const char* pumps[]={"---","--K","-D-","-DK","H--","H-K","HD-","HDK",
        "????","Home","Pass","Rest","Mash","Z1  ","Z2P1","P2  "};
    static const char* valve[]={"????","Home","Pass","Rest","Mash","Z1  ","Z2P1","P2  ","ZP3 ","ZP4 "};
    const auto& value=app.msp_state;const auto info=ConsoleLevel::Info;const auto tag="MSPS";
    return log(info,tag,nullptr,0,"StateId = %s",to_string(static_cast<MspStateId>(value.id)))&&
        log(info,tag,nullptr,0,"Pumps = %s",pumps[value.pumps>>4])&&
        log(info,tag,nullptr,0,"Valve = %s",value.moving_valve?"****":valve[value.valve_position>=1&&value.valve_position<=9?value.valve_position:0])&&
        log(info,tag,nullptr,0,"Errors = %x",value.errors)&&
        log(info,tag,nullptr,0,"Version = %u.%u",value.version_major,value.version_minor)&&
        log(info,tag,nullptr,0,"BarePcb = %s",boolean(value.use_flags&1))&&
        log(info,tag,nullptr,0,"UseServos = %s",boolean(value.use_flags&2));
}
bool ApplicationConsole::Implementation::information_adc() {
    const auto& adc=app.acquisition.adc;
    if(!log(ConsoleLevel::Info,"MAX11613",nullptr,0,"IsReading = %s",boolean(adc.polling_enabled)))return false;
    for(std::size_t i=0;i<4;++i){
        if(!raw_adc_valid(adc.raw[i])){
            if(!assertion("MAX11613.h",0xe2,"_hasValidTemp(id)")||!assertion("MAX11613.h",0xd9,"_isRawAdcValid(rawAdc)"))return false;
        }
        const auto* parameters=thermistor_parameters(adc.sensor_types[i]);
        if(!parameters){if(!assertion("MAX11613.h",0xdb,"0 <= idx && idx < ARRAY_LENGTH(TempSensorTypeParams)"))return false;return false;}
        double celsius=0;if(!console_thermistor(services,*parameters,adc.raw[i],celsius))return false;
        if(!log(ConsoleLevel::Info,"MAX11613","MAX11613.h",0x119,"%+3.1fC %+3.0fF %4d %-7s %s",celsius,celsius*1.8+32,
            adc.raw[i],to_string(static_cast<TempSensorId>(i)),to_string(adc.sensor_types[i])))return false;
    }
    return true;
}
bool ApplicationConsole::Implementation::detect_adc() {
    const auto polled=max11613_poll(app.acquisition.adc,app.ports.sensor_bus,&services);
    if(polled!=AcquisitionResult::Ok&&polled!=AcquisitionResult::ShortReply&&polled!=AcquisitionResult::ReadFailure)return false;
    const auto& adc=app.acquisition.adc;
    static const unsigned channels[]={3,1,2,0,0};
    static const TempSensorType types[]={static_cast<TempSensorType>(2),static_cast<TempSensorType>(1),static_cast<TempSensorType>(1),static_cast<TempSensorType>(0),static_cast<TempSensorType>(3)};
    static const char* names[]={"ambient","wort","drain","hex10","hex100"};
    double temperatures[5]{};
    for(unsigned i=0;i<5;++i){
        if(!raw_adc_valid(adc.raw[channels[i]])&&!assertion("MAX11613.h",0xd9,"_isRawAdcValid(rawAdc)"))return false;
        if(!console_thermistor(services,*thermistor_parameters(types[i]),adc.raw[channels[i]],temperatures[i]))return false;
    }
    // The original computes all five values before emitting the information
    // rows. Returning assertions and clamp diagnostics therefore come first.
    for(unsigned i=0;i<5;++i){
        if(!log(ConsoleLevel::Info,"MAX11613","MAX11613.h",0x12e + i,"%+3.1fC %+4.0fF %-7s %s",temperatures[i],temperatures[i]*1.8+32,names[i],to_string(types[i])))return false;
    }
    HexTemperatures input{temperatures[0],temperatures[1],temperatures[2],temperatures[3],temperatures[4]};HexDetection result;
    if(!evaluate_hex_detection(input,result))return false;
    if(result.error==HexDetectError::None)return log(ConsoleLevel::Warning,"MAX11613","MAX11613.h",0x153,"HEX is %s",to_string(result.sensor));
    return log(ConsoleLevel::Error,"MAX11613","MAX11613.h",0x151,"%s",to_string(result.error));
}
bool ApplicationConsole::Implementation::information_session() {
    const auto& logger=app.session_logger;const auto& session=app.commands.session;const auto& core=app.core.state;
    const auto info=ConsoleLevel::Info;const auto tag="ZSES";const char* states[]={"Idle","Creating","Waiting","Logging","Ending"};
    const auto state=static_cast<std::uint32_t>(logger.state);
    if(!log(info,tag,nullptr,0,"State = %s",state<5?states[state]:"<State::INVALID>")||
       !log(info,tag,nullptr,0,"hasError = %s",boolean(logger.error))||
       !log(info,tag,nullptr,0,"Id = %d",static_cast<std::int32_t>(session.session_id))||
       !log(info,tag,nullptr,0,"Type = %s",to_string(core.session_type))||
       !log(info,tag,nullptr,0,"Mode = %s",to_string(core.session_mode))||
       !log(info,tag,nullptr,0,"ProgramId = %s",to_string(static_cast<ZProgramId>(core.program_id)))||
       !log(info,tag,nullptr,0,"RecipeId = %d",static_cast<std::int32_t>(session.unique_program_id))||
       !log(info,tag,nullptr,0,"Name = %s",session.name.data())||
       !log(info,tag,nullptr,0,"GroupSession = %s",boolean(session.use_azure)))return false;
    const auto& parameters=session.parameters;
    if(!log(info,"PPRMS","ProgramParams.h",0x8d,"Id = %s",to_string(parameters.id))||
       !log(info,"PPRMS","ProgramParams.h",0x8e,"ABV = %d",parameters.abv)||
       !log(info,"PPRMS","ProgramParams.h",0x8f,"IBU = %d",parameters.ibu))return false;
    const auto convert=[&](const Quantity& q,Unit unit,double& converted){
        if(quantity_in(q,unit,converted))return true;
        return assertion("Quantity.h",0x7d,"areCompatible(_unit, unit)");};
    double first=0,second=0;
    if(static_cast<std::int32_t>(parameters.water.unit)<8){
        if(!convert(parameters.water,Unit::Liter,first)||!convert(parameters.water,Unit::Gallon,second)||
           !log(info,"PPRMS","ProgramParams.h",0x91,"Water = %3.2f L (%3.2f Gal)",first,second))return false;
    }else if(!log(info,"PPRMS","ProgramParams.h",0x93,"Water = <undefined>"))return false;
    if(static_cast<std::int32_t>(parameters.intensity.unit)<8){if(!log(info,"PPRMS","ProgramParams.h",0x96,"Intensity = %3.0f point",parameters.intensity.value))return false;}
    else if(!log(info,"PPRMS","ProgramParams.h",0x98,"Intensity = <undefined>"))return false;
    if(static_cast<std::int32_t>(parameters.temperature.unit)<8){
        if(!convert(parameters.temperature,Unit::Celsius,first)||!convert(parameters.temperature,Unit::Fahrenheit,second)||
           !log(info,"PPRMS","ProgramParams.h",0x9b,"Temperature = %3.2f C (%3.2f F)",first,second))return false;
    }else if(!log(info,"PPRMS","ProgramParams.h",0x9d,"Temperature = <undefined>"))return false;
    if(!log(info,"PPRMS",nullptr,0,"isResumedSession = %s",boolean(parameters.resumed_session)))return false;
    const auto& stats=session.stats;
    return log(info,"PST","SessionState.h",0x2f,"heatSlope = %3.1f C/min",stats.heating_rate_celsius_per_minute)&&
        log(info,"PST","SessionState.h",0x30,"DurationSec = %d",static_cast<std::int32_t>(stats.duration_seconds))&&
        log(info,"PST","SessionState.h",0x31,"Pressure = %3.1fPa",stats.pressure_pascal)&&
        log(info,"PST","SessionState.h",0x32,"MaxTemp = %3.1fC",stats.maximum_temperature_celsius)&&
        log(info,"PST","SessionState.h",0x33,"MaxTempAddedSec = %d",static_cast<std::int32_t>(stats.maximum_temperature_added_seconds));
}
bool ApplicationConsole::Implementation::information_still() {
    const auto info=ConsoleLevel::Info;const auto& client=app.still_client;
    return log(info,"STILLC",nullptr,0,"STATE = %s",to_string(app.still.state.id))&&
        log(info,"STILLC",nullptr,0,"_retryCount = %d",app.still.retry_count)&&
        log(info,"STILLH",nullptr,0,"_uri = %s",client.uri.data())&&
        log(info,"STILLH",nullptr,0,"_picoStillIp = %s",client.host.data())&&
        log(info,"STILLH",nullptr,0,"response = %s",client.body.data())&&
        log(info,"STILLH",nullptr,0,"hasPending = %s",boolean(client.pending!=Command::Empty))&&
        log(info,"STILLH",nullptr,0,"hasError = %s",boolean(client.error))&&
        log(info,"STILLH",nullptr,0,"hasSuccess = %s",boolean(client.succeeded()));
}
bool ApplicationConsole::Implementation::information_still_sensors() {
    const auto& value=app.still_samples.current;const auto info=ConsoleLevel::Info;
    if(!log(info,"STILS","IPicoStill.h",0xc6,"Timestamp = %d",static_cast<std::int32_t>(value.timestamp)))return false;
    for(unsigned i=0;i<4;++i)if(!log(info,"STILS","IPicoStill.h",0xc7+i,"T%u = %3.1f (%3.1f )",i+1,value.temperature[i],value.speed[i]))return false;
    return log(info,"STILS","IPicoStill.h",0xcb,"Psi = %3.1f (%3.1f) ",value.pressure,value.speed[4])&&
        log(info,"STILS","IPicoStill.h",0xcc,"IsPumpRunning = %s",boolean(value.pump_running))&&
        log(info,"STILS","IPicoStill.h",0xcd,"IsFanRunning = %s",boolean(value.fan_running))&&
        log(info,"STILS","IPicoStill.h",0xce,"IsProgramRunning = %s",boolean(value.program_running))&&
        log(info,"STILS","IPicoStill.h",0xcf,"Error = %s",value.error.data());
}
bool ApplicationConsole::Implementation::information_runtime() {
    const auto& state=app.runtime.state;const auto info=ConsoleLevel::Info;const auto tag="RUNT";char time[64],instruction[64];
    head_duration_text(time,sizeof(time),state.total_left_seconds);
    if(!head_instruction_text(instruction,sizeof(instruction),state.instruction))return false;
    if(!log(info,tag,nullptr,0,"stateId = %s",to_string(state.id))||
       !log(info,tag,nullptr,0,"screenId = %s",to_string(static_cast<ScreenId>(state.screen)))||
       !log(info,tag,nullptr,0,"isHwPaused = %s",boolean(state.paused))||
       !log(info,tag,nullptr,0,"pauseReason = %s",to_string(static_cast<PauseReason>(state.pause_reason)))||
       !log(info,tag,nullptr,0,"stepName = %s",state.step_name)||!log(info,tag,nullptr,0,"subStepName = %s",state.substep_name)||
       !log(info,tag,nullptr,0,"totalTimeLeft = %s",time)||!log(info,tag,nullptr,0,"instruction = %s",instruction))return false;
    const auto& still=state.still;
    if(!log(info,tag,"RuntimeState.h",0x433,"still state = %s",to_string(static_cast<RuntimeStillStateId>(still.id)))||
       !log(info,tag,"RuntimeState.h",0x434,"still heat loss = %5.1f [wort = %5.1f] [t3 = %5.1f]",still.reference_wort-still.reference_t3,still.reference_wort,still.reference_t3)||
       !log(info,tag,"RuntimeState.h",0x435,"still boil Temp = %5.2f",still.boil_t3)||
       !log(info,tag,"RuntimeState.h",0x436,"still boil psi  = %5.2f",still.boil_scalar_978)||
       !log(info,tag,"RuntimeState.h",0x437,"still step      = %5.2f",still.step_target))return false;
    if(!owner.hooks.scope||!owner.hooks.scope(owner.hooks.context,true,"STK","Runtime.h",0x7c,"out"))return false;
    bool ok=log(info,"STK",nullptr,0,"codeSize = %d",static_cast<int>(app.runtime.stack.size()));
    for(std::size_t i=0;ok&&i<app.runtime.stack.size();++i){const auto& operation=app.runtime.stack.at(i);
        char opcode[32],argument[32];::snprintf(opcode,sizeof(opcode),"OpCode::%s,",to_string(operation.opcode));
        if(!head_instruction_argument_text(argument,sizeof(argument),operation)){ok=false;break;}
        const bool valid=instruction_valid(operation);
        ok=log(valid?info:ConsoleLevel::Error,"PGM","RuntimeState.h",valid?0x332:0x330,
            valid?"{ %-22s 0x%04x }, // %s":"{ %-22s 0x%04x }, // INVALID %s",opcode,operation.argument,argument);
    }
    const bool closed=owner.hooks.scope(owner.hooks.context,false,"STK","Runtime.h",0x7c,"out");
    if(!ok||!closed)return false;
    const auto& watch=state.t1_drop_watch;
    return log(info,"DMON","RuntimeState.h",0x3bc,"%s %s  [dropped = %s] [max = %5.1f]","T1DROP",watch.enabled?"ACTIVE":"INACTIVE",boolean(watch.triggered),watch.maximum);
}
