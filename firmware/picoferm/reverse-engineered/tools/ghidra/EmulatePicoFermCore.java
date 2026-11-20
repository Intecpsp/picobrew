import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.zip.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulatePicoFermCore extends GhidraScript {
    private static final long DATA=0x3ff00000L,STATE=DATA+0x400,RESULT=DATA+0x800,INPUT=DATA+0xc00;
    protected final Set<Long> nested=new HashSet<>(Arrays.asList(0x40202d74L,0x40202d8cL,0x40203700L,0x4020381cL,
        0x40203914L,0x402039fcL,0x40203d3cL,0x4020da98L,0x40204140L,0x4020e1fcL,0x40210694L,0x40210554L,
        0x40105d94L,0x40105db4L,0x40105e20L,0x40105b4cL,0x40105cb0L,0x402050b4L,0x40202e30L));
    protected List<String> events;protected long gpio;
    protected String bits(long value){return String.format("%08x",value&0xffffffffL);}
    protected void boundary(PicoFermExecution e,long target)throws Exception {
        if(nested.contains(target)){e.invoke(target);return;}
        switch((int)target){
        case 0x4020d764:e.countBoundary("Arduino_yield");events.add("yield");return;
        case 0x40202f50:e.countBoundary("application_diagnostic_text");events.add("text:"+e.string(e.register(2))+":"+e.string(e.register(3))+":"+e.string(e.register(4)));return;
        case 0x402030a8:e.countBoundary("application_diagnostic_integer");events.add("integer:"+e.string(e.register(2))+":"+e.string(e.register(3))+":"+e.register(4));return;
        case 0x40203144:e.countBoundary("application_diagnostic_boolean");events.add("boolean:"+e.string(e.register(2))+":"+e.string(e.register(3))+":"+((e.register(4)&255)!=0?1:0));return;
        case 0x40203164:e.countBoundary("application_diagnostic_float");events.add("number:"+e.string(e.register(2))+":"+e.string(e.register(3))+":"+bits(e.register(4)));return;
        case 0x401069d0:e.countBoundary("Arduino_digitalWrite");events.add("write:"+e.register(2)+":"+e.register(3));return;
        case 0x40106a30:e.countBoundary("Arduino_digitalRead");events.add("read:"+e.register(2)+":"+gpio);e.register(2,gpio);return;
        case 0x40202de0:e.countBoundary("EEPROM_dirty_buffer_and_flash_commit");events.add("persist:"+hex(e,e.register(2),236));return;
        case 0x402140dc:{e.countBoundary("libc_memset");int length=(int)e.register(4);if(length<0||length>4096)throw new IllegalStateException("Memory boundary");byte[] b=new byte[length];Arrays.fill(b,(byte)e.register(3));e.emulator.writeMemory(e.address(e.register(2)),b);return;}
        case 0x4000e268:e.countBoundary("ROM_umodsi3");if(e.register(3)==0)throw new IllegalStateException("Undefined modulo");e.register(2,e.register(2)%e.register(3));return;
        case 0x4000e21c:e.countBoundary("ROM_udivsi3");if(e.register(3)==0)throw new IllegalStateException("Undefined division");e.register(2,e.register(2)/e.register(3));return;
        case 0x4000e2a4:e.countBoundary("ROM_floatunsisf");e.register(2,Float.floatToRawIntBits((float)e.register(2)));return;
        case 0x4000e2ac:e.countBoundary("ROM_floatsisf");e.register(2,Float.floatToRawIntBits((float)(int)e.register(2)));return;
        case 0x4000c268:e.countBoundary("ROM_subsf3");e.register(2,Float.floatToRawIntBits(Float.intBitsToFloat((int)e.register(2))-Float.intBitsToFloat((int)e.register(3))));return;
        default:throw new IllegalStateException(String.format("Unmodeled PicoFerm boundary 0x%08x",target));
        }
    }
    protected String hex(PicoFermExecution e,long pointer,int length)throws Exception{StringBuilder s=new StringBuilder();for(byte b:e.emulator.readMemory(e.address(pointer),length))s.append(String.format("%02x",b&255));return s.toString();}
    protected void call(PicoFermExecution e,long target,long... args)throws Exception{for(int i=0;i<args.length;i++)e.register(2+i,args[i]);e.invoke(target);}
    private Map<String,Object> run(PicoFermExecution e,JsonObject in)throws Exception{
        e.resetRegisters();e.clear(DATA,8192);events=new ArrayList<>();gpio=in.has("gpio")?in.get("gpio").getAsLong():1;
        String op=in.get("op").getAsString();Map<String,Object> out=new LinkedHashMap<>();out.put("id",in.get("id").getAsString());
        long record=op.equals("adc")||op.equals("persist")?STATE+0xc0:DATA;
        if(in.has("data")){String value=in.get("data").getAsString();for(int i=0;i<236;i++)e.write(record+i,Integer.parseInt(value.substring(2*i,2*i+2),16),1);}
        switch(op){
        case "assertion":call(e,0x40202d74L);break;
        case "warning":call(e,0x40202d8cL);break;
        case "session":{byte[] b=in.get("response").getAsString().getBytes(StandardCharsets.ISO_8859_1);e.emulator.writeMemory(e.address(INPUT),b);e.write(INPUT+b.length,0,1);call(e,0x40204140L,RESULT,INPUT);out.put("state",e.read(RESULT,4));out.put("error_flags",e.read(RESULT+4,4));break;}
        case "reset":call(e,0x4020381cL,record);break;
        case "migrate":call(e,0x40203700L,record);break;
        case "sanitize":call(e,0x402039fcL,record);break;
        case "calibration":call(e,0x40203914L,record,RESULT);out.put("error",e.read(RESULT,1));out.put("can_calibrate",e.read(RESULT+1,1));out.put("message",e.string(e.read(RESULT+4,4)));break;
        case "append":call(e,0x40203d3cL,record,in.get("temperature").getAsLong(),in.get("pressure").getAsLong());break;
        case "iterator":e.write(RESULT,record,4);e.write(RESULT+4,in.get("processed").getAsLong(),4);e.write(RESULT+8,in.get("index").getAsLong(),4);call(e,0x4020da98L,RESULT);out.put("done",e.register(2));call(e,0x40203b60L,RESULT);out.put("processed",e.read(RESULT+4,4));out.put("index",e.read(RESULT+8,4));break;
        case "button":call(e,0x40202e30L);out.put("button",e.register(2));break;
        case "adc":case "persist":
            e.write(STATE+0x24,in.get("machine_state").getAsLong(),4);e.write(STATE+0x2c,in.get("session_state").getAsLong(),4);
            if(op.equals("persist"))call(e,0x402050b4L,STATE);else{e.write(INPUT,in.get("temperature").getAsLong(),4);e.write(INPUT+4,in.get("raw_pressure").getAsLong(),4);call(e,0x40205114L,STATE,INPUT);}break;
        default:throw new IllegalArgumentException("Unknown case "+op);
        }
        if(in.has("data"))out.put("data",hex(e,record,236));out.put("events",events);return out;
    }
    protected String sha(Path p)throws Exception{StringBuilder s=new StringBuilder();for(byte b:MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(p)))s.append(String.format("%02x",b&255));return s.toString();}
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected gzip inputs/output");JsonArray inputs;
        try(GZIPInputStream in=new GZIPInputStream(Files.newInputStream(Path.of(args[0])))){inputs=JsonParser.parseReader(new InputStreamReader(in,StandardCharsets.UTF_8)).getAsJsonArray();}
        Map<String,Object> report=new LinkedHashMap<>();List<Map<String,Object>> cases=new ArrayList<>();
        try(PicoFermExecution e=new PicoFermExecution(currentProgram,monitor,this::boundary)){
            for(JsonElement in:inputs)cases.add(run(e,in.getAsJsonObject()));report.put("input_sha256",PicoFermExecution.IMAGE_SHA);report.put("cases_input_gzip_sha256",sha(Path.of(args[0])));
            report.put("cases",cases);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original call0 instruction bodies for packed storage, session parser, ADC application and button latch. Returning assertion bodies execute both SDK yields. OEM newlib parser and float comparators execute. Diagnostic logger, EEPROM commit, GPIO and arithmetic are explicit boundaries; no network/physical qualification.");
            report.put("hardware_tested",false);report.put("full_cpu_emulation",false);
        }
        try(GZIPOutputStream out=new GZIPOutputStream(Files.newOutputStream(Path.of(args[1])))){out.write((new Gson().toJson(report)+"\n").getBytes(StandardCharsets.UTF_8));}
        println("Executed "+cases.size()+" original PicoFerm core cases");
    }
}
