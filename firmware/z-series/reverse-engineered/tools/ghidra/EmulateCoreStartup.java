// Execute original Core startup, heater setup and returning timer assertion.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
public class EmulateCoreStartup extends GhidraScript {
    private static final long CORE=0x3ff40000L,EEPROM=0x3ff72000L,INTERFACE=0x3ff70000L,VTABLE=0x3ff71000L;
    private static final long[] COMPONENTS={0x400f6ae8L,0x400f7e98L,0x400f6e78L,0x40112f88L,0x401007dcL,
        0x400fbc1cL,0x400f7028L,0x400d0364L,0x400de078L,0x400de10cL,0x400fc0a4L,0x400ff63cL,
        0x400fb8a4L,0x40112378L,0x4010c0a0L,0x401122a0L,0x400fae40L,0x400fb46cL,0x4010dd9cL,
        0x400f71b0L,0x4011038cL,0x400efc24L};
    private List<List<Long>> events;private int boiler,timerStart;
    private void event(long... values){List<Long> item=new ArrayList<>();for(long value:values)item.add(value);while(item.size()<5)item.add(0L);events.add(item);}
    private void boundary(Z119Execution e,long target)throws Exception {
        if(e.arithmetic(target))return;
        for(int i=0;i<COMPONENTS.length;i++)if(target==COMPONENTS[i]){e.countBoundary("component_"+i);event(0,i);return;}
        switch((int)target){
        case 0x4012a798:case 0x400d0b50:case 0x400d5a44:case 0x400d5a68:e.countBoundary("startup_banner_and_scope");return;
        case 0x40080904:e.countBoundary("pinMode");event(1,e.register(10),e.register(11));return;
        case 0x40080a9c:e.countBoundary("digitalWrite");event(2,e.register(10),e.register(11));return;
        case 0x400d5b24:e.countBoundary("screenPower");event(2,33,e.register(11));return;
        case 0x400de248:e.countBoundary("Eeprom_getData");event(3,boiler);e.register(10,EEPROM);return;
        case 0x400fad30:
            e.countBoundary("separately_validated_ADC_setup");
            if(e.register(11)!=boiler)throw new IllegalStateException("Unexpected ADC boiler");
            event(4,0x34);event(5,0x80);event(6,10);event(4,0x34);event(5,0x82);event(5,7);event(7,1);event(16);return;
        case 0x400fc3e4:case 0x400d1a40:case 0x400db014:e.invoke(target);return;
        case 0x400fc174:e.countBoundary("Heater_console_registry");event(13);return;
        case 0x400de27c:case 0x400d02cc:e.countBoundary("separately_validated_PID_configuration");return;
        case 0x400922bc:
            e.countBoundary("xTimerCreate");
            if(e.register(11)!=1000||e.register(12)!=1||e.register(13)!=1||e.register(14)!=0x40080570L)throw new IllegalStateException("Timer create arguments");
            e.register(10,0x8002);return;
        case 0x40090a4c:e.countBoundary("xTaskGetTickCount");e.register(10,1234);return;
        case 0x400922e0:
            e.countBoundary("xTimerGenericCommand_start");
            if(e.register(10)!=0x8002||e.register(11)!=1||e.register(12)!=1234||e.register(13)!=0||e.register(14)!=10)throw new IllegalStateException("Timer start arguments");
            event(8,1000,1,0,10);e.register(10,timerStart);return;
        case 0x40129640:e.countBoundary("timerBegin");event(9,e.register(10),e.register(11),e.register(12));e.register(10,0x8000+e.register(10));return;
        case 0x401294dc:{
            e.countBoundary("timerAttachInterrupt");long callback=e.register(11)==0x400805dcL?1:e.register(11)==0x40080540L?2:-1;
            if(callback<0)throw new IllegalStateException("Timer callback");event(10,e.register(10),callback,e.register(12));return;}
        case 0x40129450:e.countBoundary("timerAlarmWrite");event(11,e.register(10),e.register(12),e.register(13),e.register(14));return;
        case 0x400d2160:e.countBoundary("Heater_timer_enable");event(12,0x8000,e.register(11));event(12,0x8001,e.register(11));return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x40081388:e.countBoundary("millis");e.register(10,12345);return;
        case 0x400d21cc:e.countBoundary("assertion_log");event(14,e.register(15));return;
        case 0x401289a0:e.countBoundary("assertion_delay");if(e.read(0x3ffc2e84L,4)!=1)throw new IllegalStateException("Missing real assertion counter update");event(15,e.register(10));return;
        default:throw new IllegalStateException(String.format("Unreviewed startup call %08x",target));
        }
    }
    @Override public void run()throws Exception {
        String[] args=getScriptArgs();if(args.length!=1)throw new IllegalArgumentException("Expected output JSON");
        List<Map<String,Object>> cases=new ArrayList<>();
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary)){
            for(boiler=0;boiler<2;boiler++)for(timerStart=0;timerStart<2;timerStart++){
                e.resetRegisters();e.clear(CORE,0x18000);e.clear(INTERFACE,0x3000);e.write(0x3ffc2e84L,0,4);
                e.write(EEPROM+0x74,boiler,4);e.write(CORE+0x5d58,INTERFACE,4);e.write(INTERFACE,VTABLE,4);e.write(VTABLE+12,0x400fc0a4L,4);
                e.write(CORE+0x4948,CORE+0x9d8,4);e.write(CORE+0x4950,CORE+0xc58,4);e.write(CORE+0x4940+0x38,12,1);
                events=new ArrayList<>();e.register(10,CORE);e.invoke(0x40113078L);
                Map<String,Object> item=new LinkedHashMap<>();item.put("boiler",boiler);item.put("timer_start",timerStart);
                item.put("assertions",e.read(0x3ffc2e84L,4));item.put("events",events);cases.add(item);
            }
            Map<String,Object> report=new LinkedHashMap<>();report.put("schema_version",2);report.put("input_sha256",Z119Execution.IMAGE_SHA);report.put("function","0x40113078");
            report.put("scope","Original Core startup and Heater setup bodies, including real returning timer assertion/counter/delay and continued hardware timer/diagnostic setup. SDK and other component entry points are explicit boundaries; ADC pin/bus effects use separately validated behavior. Invalid null hardware-timer handles remain an owned guard, not an OEM-equivalent case.");
            report.put("original_functions_executed",e.functions);report.put("boundary_calls",e.boundaries);report.put("cases",cases);report.put("full_cpu_or_hardware_emulation",false);report.put("hardware_tested",false);
            Files.writeString(Path.of(args[0]),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed four original Core startups, including timer assertion continuation");
    }
}
