// Original Z119 Head flow; event bodies and presentation are explicit services.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulateHeadFlow extends GhidraScript {
    private static final long HEAD=0x3ff20000L, STATE=0x3ff70000L, DOWNLOAD=0x3ff78000L, SINK=0x7ff10000L, SYSTEM=0x7ff11000L;
    private static final int[] PENDING={0xfd0,0x1164,0x11f4,0x147c,0x150c,0x159c,0x162c,0x16bc,0x1760,0x1c,0x150,0xb8,0x1398,0x10c8};
    private static final long[] HANDLERS={0x401095b4L,0x4010718cL,0x400fdd0cL,0x40107578L,0x4010722cL,0x4010730cL,0x401077f8L,0x40107aa0L,0x40107bb8L,0x400f41ecL,0x401094c8L,0x40107984L,0x401080d4L,0x40105fbcL};
    private final List<List<Long>> events=new ArrayList<>();
    private long clock,increment; private int clocks;
    private static String sha(byte[] bytes)throws Exception{
        byte[] digest=MessageDigest.getInstance("SHA-256").digest(bytes);StringBuilder out=new StringBuilder();
        for(byte b:digest)out.append(String.format("%02x",b&255));return out.toString();
    }
    private void record(long type,long a,long b,long c){events.add(List.of(type&0xffffffffL,a&0xffffffffL,b&0xffffffffL,c&0xffffffffL));}
    private void boundary(DisplayExecution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        for(int i=0;i<HANDLERS.length;i++)if(target==HANDLERS[i]){
            long expected=i==9?HEAD+0xc:HEAD;
            if(e.register(10)!=expected||e.read(HEAD+PENDING[i],1)==0)throw new IllegalStateException("Invalid Head handler boundary");
            e.countBoundary("selected_event_handler_"+i);e.write(HEAD+PENDING[i],0,1);record(100+i,0,0,0);return;
        }
        switch((int)target){
            case 0x40081388:{
                e.countBoundary("millis");long value=(clock+clocks*increment)&0xffffffffL;clocks++;record(200,value,0,0);e.register(10,value);return;
            }
            case 0x400d5a44:e.countBoundary("scope_log_enter");return;
            case 0x400d5a68:e.countBoundary("scope_log_exit");return;
            case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
            case 0x400d21cc:e.countBoundary("diagnostic_log");return;
            case 0x400db014:throw new IllegalStateException("Unexpected original assertion at line "+e.register(11));
            case 0x401367a0:throw new IllegalStateException("Original stack guard failed");
            case 0x400d283c:
                e.countBoundary("translation_lookup");e.register(10,0x7ff20000L+e.register(11)*4);return;
            case 0x40106c48:e.countBoundary("running_screen");record(0,0,0,0);return;
            case 0x40103b1c:
                e.countBoundary("message_screen");record(1,(e.register(11)-0x7ff20000L)/4,e.register(12),e.register(13));return;
            case 0x401045dc:e.countBoundary("network_required_screen");record(2,0,0,0);return;
            case 0x40103858:e.countBoundary("home_screen");record(3,0,0,0);return;
            case 0x400dbc40:
                if(e.register(10)!=SYSTEM)throw new IllegalStateException("Unexpected System instance");
                e.countBoundary("system_alarm");record(4,e.register(11),0,0);return;
            case 0x400ef230:
                e.countBoundary("CoreCommand_default_constructor");e.clear(e.register(10),0x2f0);e.write(e.register(10)+4,e.register(11),4);return;
            case 0x400f3d3c:
                if(e.register(10)!=SINK)throw new IllegalStateException("Unexpected Core command sink");
                e.countBoundary("Core_command_queue_put");record(5,e.read(e.register(11)+4,4),0,0);return;
            case 0x40105044:
                if(e.register(10)!=HEAD||e.register(11)!=STATE+0x940||e.register(12)!=STATE+0x10)throw new IllegalStateException("Unexpected still screen inputs");
                e.countBoundary("still_screen");record(6,0,0,0);return;
            case 0x400ea01c:e.countBoundary("download_progress_screen");record(7,e.register(11),0,0);return;
            case 0x40103e44:
                if(e.register(10)!=HEAD+0x38a8)throw new IllegalStateException("Unexpected ScreenLibrary instance");
                e.countBoundary("ScreenLibrary_tick");record(8,e.register(11),0,0);return;
            case 0x400e7904:
                if(e.register(10)!=HEAD+0x17f8)throw new IllegalStateException("Unexpected Display instance");
                e.countBoundary("Display_tick");record(9,e.register(11),0,0);return;
            case 0x400d1cbc:case 0x400deb2c:case 0x400deb58:case 0x401070e8:case 0x401074f8:
                e.invoke(target);return;
            default:throw new IllegalStateException(String.format("Unmodeled Head flow call %08x",target));
        }
    }
    private void invoke(DisplayExecution e,long entry,long... arguments)throws Exception{
        for(int i=0;i<arguments.length;i++)e.register(10+i,arguments[i]);e.invoke(entry);
    }
    private void timer(DisplayExecution e,long pointer,JsonArray values){
        e.write(pointer,values.get(0).getAsLong(),1);e.write(pointer+4,values.get(1).getAsLong(),4);e.write(pointer+8,values.get(2).getAsLong(),4);
    }
    private long value(JsonObject input,String key){return input.get(key).getAsLong()&0xffffffffL;}
    private Map<String,Object> runCase(DisplayExecution e,JsonObject input)throws Exception{
        e.resetRegisters();e.clear(HEAD,0xa300);e.clear(STATE,0x1000);e.clear(DOWNLOAD,0x1000);events.clear();clocks=0;
        e.write(0x3ffc4654L,0x5a61c0deL,4); // Deterministic original stack guard, unchanged by modeled services.
        clock=value(input,"clock");increment=value(input,"clock_step");
        e.write(HEAD+0x387c,STATE,4);e.write(HEAD+0x3878,DOWNLOAD,4);e.write(HEAD+0x3868,SINK,4);e.write(HEAD+0x386c,SYSTEM,4);
        e.write(STATE+4,value(input,"core"),4);e.write(STATE+0x2f4,value(input,"runtime"),4);e.write(STATE+0x36c,value(input,"instruction"),4);
        e.write(HEAD+0xa021,value(input,"downloading"),1);e.write(DOWNLOAD+0x858,value(input,"done"),4);e.write(DOWNLOAD+0x85c,value(input,"total"),4);
        long mask=value(input,"pending");for(int i=0;i<PENDING.length;i++)e.write(HEAD+PENDING[i],(mask>>>i)&1,1);
        timer(e,HEAD+0x9f80,input.getAsJsonArray("transient"));timer(e,HEAD+0x9f8c,input.getAsJsonArray("alarm"));
        e.write(HEAD+0x17f0,value(input,"alarm_duration"),4);
        switch(input.get("op").getAsString()){
            case "sync":invoke(e,0x401070e8L,HEAD);break;
            case "dispatch":invoke(e,0x401074f8L,HEAD,value(input,"argument"));break;
            case "alarm":invoke(e,0x400deb58L,HEAD,value(input,"argument"));break;
            case "tick":invoke(e,0x40109958L,HEAD,value(input,"now"));break;
            case "timer_start":invoke(e,0x400deb2cL,HEAD+0x9f80,value(input,"argument"));break;
            case "timer_reset":invoke(e,0x400d1cbcL,HEAD+0x9f80);break;
            default:throw new IllegalArgumentException("Unknown Head operation");
        }
        List<Long> timers=new ArrayList<>();for(long pointer:new long[]{HEAD+0x9f80,HEAD+0x9f8c}){
            timers.add(e.read(pointer,1));timers.add(e.read(pointer+4,4));timers.add(e.read(pointer+8,4));
        }
        long remaining=0;for(int i=0;i<PENDING.length;i++)remaining|=e.read(HEAD+PENDING[i],1)<<i;
        Map<String,Object> record=new LinkedHashMap<>();record.put("id",input.get("id").getAsString());record.put("result",0);
        record.put("timers",timers);record.put("pending",remaining);record.put("clocks",clocks);record.put("events",new ArrayList<>(events));return record;
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input JSON and output JSONL");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson gson=new Gson();int count=0;
        try(DisplayExecution e=new DisplayExecution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){
            for(JsonElement input:inputs){out.write(gson.toJson(runCase(e,input.getAsJsonObject()))+"\n");count++;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",DisplayExecution.IMAGE_SHA);report.put("cases",count);
            report.put("input_file_sha256",sha(Files.readAllBytes(Path.of(args[0]))));report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original Head event selection, timer, alarm, command and screen-routing instructions; event-specific bodies, presentation, System alarm, command construction/queue and diagnostics are explicit boundaries, not complete Head execution");
            report.put("full_cpu_or_hardware_emulation",false);report.put("hardware_tested",false);
            Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+count+" original Z Head flow scenarios");
    }
}
