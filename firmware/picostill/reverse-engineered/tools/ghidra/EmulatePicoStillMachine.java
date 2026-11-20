import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.util.*;
import java.util.zip.*;
import com.google.gson.*;
public class EmulatePicoStillMachine extends EmulatePicoStillData {
    private static final long BASE=0x3ff00000L,TEXT=BASE+0x3000,COUNTER=0x3ffc3078L;
    private JsonObject input;private JsonArray serial;private int serialPosition;
    private void text(PicoStillExecution e,long pointer,String value){e.emulator.writeMemory(e.address(pointer),(value+"\0").getBytes(StandardCharsets.ISO_8859_1));}
    @Override protected void boundary(PicoStillExecution e,long target)throws Exception {
        if(nested.contains(target)){super.boundary(e,target);return;}
        switch((int)target){
        case 0x400d294c:{e.countBoundary("diagnostic_variable_mask_policy");String prefix=e.register(13)!=0?"masked:":"variable:";events.add(prefix+string(e,e.register(10))+":"+string(e,e.register(11))+":"+string(e,e.register(12)));return;}
        case 0x400d2920:e.countBoundary("diagnostic_banner");events.add("message:"+string(e,e.register(10)));return;
        case 0x400d2a80:e.countBoundary("integer_diagnostic");events.add("integer:"+string(e,e.register(10))+":"+string(e,e.register(11))+":"+e.register(12));return;
        case 0x400d2b34:e.countBoundary("floating_diagnostic");events.add("floating:"+string(e,e.register(10))+":"+string(e,e.register(11))+":"+String.format("%08x",e.register(12)));return;
        case 0x400810c0:e.countBoundary("Arduino_pinMode");events.add("pin_mode:"+e.register(10)+":"+e.register(11));return;
        case 0x400e77a8:e.countBoundary("esp_ota_get_boot_partition");events.add("partition_configured");e.register(10,BASE+(input.get("configured").getAsInt()!=0?0x5080:0x5040));return;
        case 0x400e7680:e.countBoundary("esp_ota_get_running_partition");events.add("partition_running");e.register(10,BASE+0x5040);return;
        case 0x400e3e0c:e.countBoundary("esp_partition_find_first");events.add("partition_factory");e.register(10,BASE+0x5000);return;
        case 0x400d2a00:e.countBoundary("partition_diagnostic");events.add("partition:"+string(e,e.register(10))+":"+e.register(11));return;
        case 0x400d7474:e.countBoundary("hardware_setup_previously_reviewed_driver");events.add("initialize_hardware");return;
        case 0x400d4130:e.countBoundary("NVS_initialize_previously_replayed");return;
        case 0x400d4830:{e.countBoundary("credential_load_previously_replayed");String value=input.get("stored").getAsString();for(int i=0;i<100;i++)e.write(e.register(11)+i,Integer.parseInt(value.substring(2*i,2*i+2),16),1);return;}
        case 0x400da878:e.countBoundary("WiFi_persistent");events.add("persistent:"+e.register(11));return;
        case 0x400dac14:e.countBoundary("WiFi_mode");events.add("mode:"+e.register(10));return;
        case 0x4014a2b0:e.countBoundary("WiFi_setAutoReconnect");events.add("auto_reconnect:"+e.register(11));return;
        case 0x400dbb0c:{e.countBoundary("ESP_getEfuseMac");events.add("mac");long value=input.get("mac").getAsLong();e.register(10,value&0xffffffffL);e.register(11,value>>>32);return;}
        case 0x400ec18c:{String format=string(e,e.register(12));if(format.equals("%02x%02x%02x%02x%02x%02x")){e.countBoundary("newlib_snprintf_MAC_bytes");String value=String.format("%02x%02x%02x%02x%02x%02x",e.register(13),e.register(14),e.register(15),e.read(e.register(1),4),e.read(e.register(1)+4,4),e.read(e.register(1)+8,4));text(e,e.register(10),value);e.register(10,value.length());return;}super.boundary(e,target);return;}
        case 0x400dbb40:e.countBoundary("Serial_available");events.add("serial_available:"+(serial.size()-serialPosition));e.register(10,serial.size()-serialPosition);return;
        case 0x400dbb6c:{e.countBoundary("Serial_read");int value=serial.get(serialPosition++).getAsInt();events.add("serial_read:"+value);e.register(10,value);return;}
        default:super.boundary(e,target);
        }
    }
    private Map<String,Object> machine(PicoStillExecution e,JsonObject in)throws Exception{
        input=in;serial=in.getAsJsonArray("serial");serialPosition=0;e.resetRegisters();e.clear(BASE,0x10000);e.write(COUNTER,0,4);e.write(0x3ffc4770L,0x12345678L,4);events=new ArrayList<>();clock=in.get("now").getAsLong();high=true;pressure=new JsonArray();for(int n:new int[]{64,128,0,0})pressure.add(n);adc=new JsonArray();for(int i=0;i<4;i++){adc.add(8);adc.add(0);}call(e,0x400d8688L);
        String raw=in.get("data").getAsString();for(int i=0;i<raw.length()/2;i++)e.write(BASE+i,Integer.parseInt(raw.substring(2*i,2*i+2),16),1);text(e,TEXT,in.get("command").getAsString());
        switch(in.get("op").getAsString()){
        case "dispatch":call(e,0x400d8a00L,BASE);break;
        case "queue":call(e,0x400d5918L,BASE+0x1448,TEXT);break;
        case "receive":call(e,0x400d764cL,BASE+0x1448);break;
        case "setup":call(e,0x400d7510L,BASE);break;
        default:throw new IllegalArgumentException("op");}
        Map<String,Object> row=new LinkedHashMap<>();row.put("id",in.get("id").getAsString());row.put("data",hex(e,BASE,0x2000));row.put("events",events);row.put("assertion_count",e.read(COUNTER,4));row.put("clock",clock);return row;
    }
    @Override public void run()throws Exception{
        nested.addAll(Arrays.asList(0x400d4860L,0x400d48f4L,0x400d4928L,0x400d4984L,0x400d49e0L,0x400d49f8L,0x400d48b0L,0x400d58c4L,0x400d58fcL,0x400d58e4L,0x400d44f8L,0x400de3d8L,0x400eb62cL,
            0x400d3320L,0x400d3f48L,0x400d5918L,0x400d2a50L,0x400d2b74L,0x400d2bb8L,0x40149ff0L,0x4014a014L,0x400d2c5cL,0x400d5f38L,0x400d5734L,0x400d4c98L,0x4014a0d0L,
            0x400d32a4L,0x400d32c4L,0x400d32e4L,0x400d5bc4L,0x400d5cc4L,0x400d5ce0L,0x400d45c4L,0x400d5dbcL,0x400d5be0L,0x400d5938L,0x400d2c90L,0x400d2d88L,0x400d27c4L,0x400d897cL));
        String[] args=getScriptArgs();JsonArray inputs;try(GZIPInputStream stream=new GZIPInputStream(Files.newInputStream(Path.of(args[0])))){inputs=JsonParser.parseReader(new InputStreamReader(stream,StandardCharsets.UTF_8)).getAsJsonArray();}
        Map<String,Object> report=new LinkedHashMap<>();List<Map<String,Object>> cases=new ArrayList<>();try(PicoStillExecution e=new PicoStillExecution(currentProgram,monitor,this::boundary)){
            e.instructionLimit=200000;for(JsonElement input:inputs){try{cases.add(machine(e,input.getAsJsonObject()));}catch(Exception error){throw new IllegalStateException(input.getAsJsonObject().get("id").getAsString(),error);}}
            report.put("input_sha256",PicoStillExecution.IMAGE_SHA);report.put("cases_input_gzip_sha256",sha(Path.of(args[0])));report.put("cases",cases);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);report.put("scope","Original application setup, console receiver/queue and 36 command cases with owned data/controller/LED/button/reporting bodies nested. NVS initialize/load wrappers already covered by separate original replay, board setup and SDK WiFi/partition/serial APIs are explicit boundaries. Destructive system commands, embedded self-tests and blocking LED display are reviewed/target-linked separately. Full application linked against real SDK; physical qualification remains absent.");report.put("full_cpu_emulation",false);report.put("hardware_tested",false);
        }
        try(GZIPOutputStream out=new GZIPOutputStream(Files.newOutputStream(Path.of(args[1])))){out.write((new Gson().toJson(report)+"\n").getBytes(StandardCharsets.UTF_8));}println("Executed "+cases.size()+" original PicoStill machine integration cases");
    }
}
