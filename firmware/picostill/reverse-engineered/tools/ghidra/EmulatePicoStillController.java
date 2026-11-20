import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.zip.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulatePicoStillController extends GhidraScript {
    private static final long DATA=0x3ff00000L,TEMP=DATA+0x1800,EVENT=0x7ff01000L,COUNTER=0x3ffc3078L;
    protected final Set<Long> nested=new HashSet<>(Arrays.asList(0x400d405cL,0x400d4678L,0x400d46b8L,0x400d2af8L,
        0x400d2ee4L,0x400d2e64L,0x400d2f00L,0x400d2f68L,0x4014a074L,0x400d4740L,
        0x400d4154L,0x400d4180L,0x4014a084L,0x400d4204L,0x400d426cL,0x4014a0a0L,
        0x400d4074L,0x400d2f9cL,0x400d6ed4L,0x400d6ef4L,0x400d68c4L,0x400d2fbcL,0x400d6f7cL,0x400d6964L,
        0x400d2ff8L,0x400d30e4L,0x400d2730L,0x400d2eb0L,0x40149f24L,0x400d4320L,0x400d2fe4L,
        0x40149f34L,0x400d41ccL,0x400d41f0L,0x400d41dcL,0x400d4334L,0x400d71e8L,
        0x400d2838L,0x400d3124L,0x400d3194L,0x400d3210L,0x400d4a68L,0x400d4af0L,
        0x400d6a08L,0x400d6c34L,0x400d6cf4L));
    protected List<String> events;protected long clock;protected boolean high;protected JsonArray pressure,adc,rx;protected int readIndex;
    protected String string(PicoStillExecution e,long pointer)throws Exception{StringBuilder s=new StringBuilder();for(int i=0;i<512;i++){long b=e.read(pointer+i,1);if(b==0)return s.toString();s.append((char)b);}throw new IllegalStateException("Unterminated original string");}
    protected String hex(PicoStillExecution e,long pointer,int length)throws Exception{StringBuilder s=new StringBuilder();for(byte b:e.emulator.readMemory(e.address(pointer),length))s.append(String.format("%02x",b&255));return s.toString();}
    protected void boundary(PicoStillExecution e,long target)throws Exception {
        if(target==0x400d4678L){events.add("assertion:"+string(e,e.register(10))+":"+e.register(11)+":"+string(e,e.register(12)));e.invoke(target);return;}
        if(nested.contains(target)){e.invoke(target);return;}if(e.arithmetic(target))return;
        switch((int)target){
        case 0x400dbd88:e.countBoundary("IP_constructor_outside_calibration_scope");return;
        case 0x400d82dc:e.countBoundary("PicoStill_constructor_outside_calibration_scope");return;
        case 0x400d28f8:case 0x400d290c:case 0x400d2938:e.countBoundary("diagnostic_output");events.add("message:"+string(e,e.register(10)));return;
        case 0x400d4600:e.countBoundary("assertion_diagnostic_format_output");return;
        case 0x40081b90:{e.countBoundary("diagnostic_basename");String s=string(e,e.register(10));e.register(10,e.register(10)+Math.max(s.lastIndexOf('\\'),s.lastIndexOf('/'))+1);return;}
        case 0x400d294c:e.countBoundary("diagnostic_variable_output");events.add("variable:"+string(e,e.register(10))+":"+string(e,e.register(11))+":"+string(e,e.register(12)));return;
        case 0x40081b50:{long pc=e.emulator.getExecutionAddress().getOffset();if(pc>=0x400d4678L&&pc<0x400d46b8L)e.countBoundary("diagnostic_millis");else{e.countBoundary("functional_millis");events.add("clock:"+clock);}e.register(10,clock);return;}
        case 0x400dd9bc:e.countBoundary("Arduino_delay");events.add("delay:"+e.register(10));clock=(clock+e.register(10))&0xffffffffL;return;
        case 0x400812c0:e.countBoundary("Arduino_digitalRead");events.add("read:"+e.register(10)+":"+(high?1:0));e.register(10,high?1:0);return;
        case 0x40081258:e.countBoundary("Arduino_digitalWrite");events.add("write:"+e.register(10)+":"+e.register(11));return;
        case 0x400dd880:e.countBoundary("Arduino_ledcWrite");events.add("pwm_write:"+e.register(10)+":"+e.register(11));return;
        case 0x4000c978:{e.countBoundary("ROM_floatundidf");long n=e.register(10)|(e.register(11)<<32);e.number(10,n>=0?(double)n:(double)(n&Long.MAX_VALUE)+0x1p63);return;}
        case 0x4000c2c8:{e.countBoundary("ROM_memcpy");long size=e.register(12);if(size==0)return;if(size>4096)throw new IllegalStateException("Copy bound");e.emulator.writeMemory(e.address(e.register(10)),e.emulator.readMemory(e.address(e.register(11)),(int)size));return;}
        case 0x4000c44c:{e.countBoundary("ROM_memset");long size=e.register(12);if(size==0)return;if(size>4096)throw new IllegalStateException("Clear bound");byte[] b=new byte[(int)size];Arrays.fill(b,(byte)e.register(11));e.emulator.writeMemory(e.address(e.register(10)),b);return;}
        case 0x4014a1a4:e.countBoundary("Wire_beginTransmission");events.add("begin:"+e.register(11));return;
        case 0x4014a164:e.countBoundary("Wire_write");events.add("write_byte:"+e.register(11));e.register(10,1);return;
        case 0x400d9a7c:e.countBoundary("Wire_endTransmission");events.add("end");e.register(10,0);return;
        case 0x400d9a68:e.countBoundary("Wire_requestFrom");events.add("request:"+e.register(11)+":"+e.register(12));rx=e.register(11)==0x18?pressure:adc;readIndex=0;e.register(10,rx.size());return;
        case 0x4014a120:e.countBoundary("Wire_available");events.add("available:"+(rx.size()-readIndex));e.register(10,rx.size()-readIndex);return;
        case 0x4014a130:{e.countBoundary("Wire_read");int value=readIndex<rx.size()?rx.get(readIndex++).getAsInt():-1;events.add("read_byte:"+value);e.register(10,value);return;}
        case 0x400d5938:e.countBoundary("controller_state_serializer");events.add("controller_changed");return;
        case 0x400d2c90:e.countBoundary("hardware_snapshot_report");events.add("snapshot_report:"+hex(e,e.register(10),96));return;
        case 0x400d2d88:e.countBoundary("full_test_report");events.add("full_test_report:"+hex(e,e.register(10),56));return;
        case 0x400dbf3c:e.countBoundary("Serial_println");events.add("console:"+string(e,e.register(11)));return;
        case (int)EVENT:e.countBoundary("sensor_callback");events.add("sensors_sampled");return;
        default:throw new IllegalStateException(String.format("Unmodeled controller boundary 0x%08x",target));
        }
    }
    protected void call(PicoStillExecution e,long target,long... args)throws Exception{for(int i=0;i<args.length;i++)e.register(10+i,args[i]);e.invoke(target);}
    private Map<String,Object> run(PicoStillExecution e,JsonObject in)throws Exception{
        e.resetRegisters();e.clear(DATA,0x3000);e.write(COUNTER,0,4);events=new ArrayList<>();clock=in.get("now").getAsLong();high=in.get("high").getAsInt()!=0;
        pressure=in.getAsJsonArray("pressure");adc=in.getAsJsonArray("adc");call(e,0x400d8688L);
        String raw=in.get("data").getAsString();for(int i=0;i<raw.length()/2;i++)e.write(DATA+i,Integer.parseInt(raw.substring(2*i,2*i+2),16),1);
        e.write(DATA+0x1e00,DATA+0x1e10,4);e.write(DATA+0x1e10,EVENT,4);long arg=in.get("arg").getAsLong();e.write(TEMP,arg,8);
        String op=in.get("op").getAsString();List<Long> flags=new ArrayList<>();
        switch(op){
        case "enable":call(e,0x400d30e4L,DATA+0x428,arg);break;
        case "enable_internal":call(e,0x400d2ff8L,DATA+0x428,arg);break;
        case "enable_record": // This wrapper is inlined in controller set-state.
            if(e.read(DATA+0x47c,1)==0){e.write(DATA+0x47d,1,1);call(e,0x400d2ff8L,DATA+0x428,1);}else events.add("variable:SENSORS:Redundant call to enableWithPressureRecording():true");break;
        case "capture":call(e,0x400d2730L,DATA+0x368,DATA+0x370);break;
        case "hardware_error":call(e,0x400d4334L,DATA+0x368);break;
        case "hardware_poll":call(e,0x400d71e8L,DATA+0x368);break;
        case "pin_test":call(e,0x400d3210L,DATA+0x6d0,TEMP,DATA+0x368);break;
        case "connection_test":call(e,0x400d3194L,DATA+0x660,TEMP,DATA+0x368);break;
        case "state":call(e,0x400d6a08L,DATA,arg);break;
        case "start":call(e,0x400d6c34L,DATA,TEMP);break;
        case "stop":call(e,0x400d6cf4L,DATA);break;
        case "poll":call(e,0x400d7288L,DATA);break;
        case "flags":for(long address:new long[]{0x400d2eb0L,0x400d2fe4L,0x40149f34L,0x400d4320L,0x400d41ccL,0x4014a0a0L}){
            call(e,address,address==0x4014a0a0L?DATA+0x428:DATA+0x368);flags.add(e.register(10));}break;
        default:throw new IllegalArgumentException("Unknown operation "+op);
        }
        Map<String,Object> row=new LinkedHashMap<>();row.put("id",in.get("id").getAsString());row.put("data",hex(e,DATA,0xe24));row.put("events",events);row.put("flags",flags);row.put("assertion_count",e.read(COUNTER,4));row.put("clock",clock);return row;
    }
    protected String sha(Path path)throws Exception{StringBuilder s=new StringBuilder();for(byte b:MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(path)))s.append(String.format("%02x",b&255));return s.toString();}
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected gzip input/output");JsonArray inputs;
        try(GZIPInputStream stream=new GZIPInputStream(Files.newInputStream(Path.of(args[0])))){inputs=JsonParser.parseReader(new InputStreamReader(stream,StandardCharsets.UTF_8)).getAsJsonArray();}
        Map<String,Object> report=new LinkedHashMap<>();List<Map<String,Object>> cases=new ArrayList<>();
        try(PicoStillExecution e=new PicoStillExecution(currentProgram,monitor,this::boundary)){
            for(JsonElement input:inputs)cases.add(run(e,input.getAsJsonObject()));report.put("input_sha256",PicoStillExecution.IMAGE_SHA);report.put("cases_input_gzip_sha256",sha(Path.of(args[0])));
            report.put("cases",cases);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original controller/hardware lifecycle, diagnostic-test and dependent sensor/actuator instructions. Original assertions continue with counter and modeled delay advancing the shared clock. GPIO/PWM, Wire, clock, observer/report serializers, SDK and ROM arithmetic/memory are explicit boundaries. enable_record inputs exercise an explicitly modeled inlined wrapper; full set-state cases execute that wrapper's original instructions.");
            report.put("full_cpu_emulation",false);report.put("hardware_tested",false);
        }
        try(GZIPOutputStream out=new GZIPOutputStream(Files.newOutputStream(Path.of(args[1])))){out.write((new Gson().toJson(report)+"\n").getBytes(StandardCharsets.UTF_8));}
        println("Executed "+cases.size()+" original PicoStill controller/lifecycle cases");
    }
}
