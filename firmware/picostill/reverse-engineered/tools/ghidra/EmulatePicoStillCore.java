// Original PicoStill 0.0.35 execution; no reconstructed formulas generate results.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulatePicoStillCore extends GhidraScript {
    private static final long DATA=0x3ff00000L, SINK=DATA+0x800, VTABLE=SINK+0x100, EVENT=0x7ff01000L;
    private static final long COUNTER=0x3ffc3078L;
    private final Set<Long> nested=new HashSet<>(Arrays.asList(0x400d405cL,0x400d4678L,
        0x400d4154L,0x400d4204L,0x400d426cL,0x4014a084L,0x400d43d0L,0x400d4428L,0x400d4408L));
    private List<String> events;
    private long clock;
    private boolean gpioHigh;
    private String cstring(PicoStillExecution e,long pointer)throws Exception {
        StringBuilder out=new StringBuilder();
        for(int i=0;i<512;i++) {long c=e.read(pointer+i,1);if(c==0)return out.toString();out.append((char)c);}
        throw new IllegalStateException("Unterminated original string");
    }
    private void boundary(PicoStillExecution e,long target)throws Exception {
        if(target==0x400d4678L) {
            events.add("assertion:"+cstring(e,e.register(10))+":"+e.register(11)+":"+cstring(e,e.register(12)));
            e.invoke(target);return;
        }
        if(nested.contains(target)){e.invoke(target);return;}
        if(e.arithmetic(target))return;
        switch((int)target) {
        case 0x400d28f8: case 0x400d290c: case 0x400d2938:
            e.countBoundary("diagnostic_output");events.add("message:"+cstring(e,e.register(10)));return;
        case 0x400d4600:e.countBoundary("assertion_diagnostic_format_output");return;
        case 0x40081b90: {
            e.countBoundary("diagnostic_basename");String path=cstring(e,e.register(10));
            e.register(10,e.register(10)+Math.max(path.lastIndexOf('\\'),path.lastIndexOf('/'))+1);return;
        }
        case 0x40081b50: {
            long pc=e.emulator.getExecutionAddress().getOffset();
            if(pc>=0x400d4678L&&pc<0x400d46b8L)e.countBoundary("diagnostic_millis");
            else{e.countBoundary("functional_millis");events.add("clock:"+clock);}
            e.register(10,clock);return;
        }
        case 0x400dd9bc:e.countBoundary("Arduino_delay");events.add("delay:"+e.register(10));return;
        case 0x400812c0:
            e.countBoundary("Arduino_digitalRead");if(e.register(10)!=13)throw new IllegalStateException("Wrong button GPIO");
            events.add("gpio:"+(gpioHigh?1:0));e.register(10,gpioHigh?1:0);return;
        case (int)EVENT:
            e.countBoundary("button_event_sink");if(e.register(10)!=SINK)throw new IllegalStateException("Wrong button sink");
            events.add("button:"+e.register(11)+":"+e.read(DATA+8,4));return;
        default:throw new IllegalStateException(String.format("Unmodeled PicoStill boundary 0x%08x",target));
        }
    }
    private long value(JsonArray a,int i){return a.get(i).getAsLong()&0xffffffffL;}
    private void invoke(PicoStillExecution e,long entry,long... args)throws Exception {
        for(int i=0;i<args.length;i++)e.register(10+i,args[i]);e.invoke(entry);
    }
    private Map<String,Object> runCase(PicoStillExecution e,JsonObject in)throws Exception {
        e.resetRegisters();e.clear(DATA,4096);events=new ArrayList<>();clock=1234;gpioHigh=true;
        e.write(COUNTER,in.has("assertion_seed")?in.get("assertion_seed").getAsLong():0,4);
        String op=in.get("op").getAsString();List<Long> results=new ArrayList<>();
        Map<String,Object> out=new LinkedHashMap<>();out.put("id",in.get("id").getAsString());
        switch(op) {
        case "ema": {
            e.writeDouble(DATA,in.get("previous").getAsDouble());e.write(DATA+8,in.get("count").getAsInt(),4);
            e.writeDouble(DATA+16,in.get("sample").getAsDouble());
            invoke(e,0x400d46b8L,DATA,DATA+8,DATA+16);
            out.put("double_bits",String.format("%016x",e.register(10)|(e.register(11)<<32)));break;
        }
        case "thermistor": {
            JsonArray p=in.getAsJsonArray("parameters");for(int i=0;i<4;i++)e.writeDouble(DATA+i*8,p.get(i).getAsDouble());
            e.write(DATA+32,Float.floatToRawIntBits(p.get(4).getAsFloat()),4);e.write(DATA+40,in.get("adc").getAsInt(),4);
            invoke(e,0x400d4074L,DATA,DATA+40);
            out.put("double_bits",String.format("%016x",e.register(10)|(e.register(11)<<32)));break;
        }
        case "sensors": {
            JsonArray counts=in.getAsJsonArray("counts"),bits=in.getAsJsonArray("bits");
            e.write(DATA+0x6c,value(counts,0),4);e.write(DATA+0x70,value(counts,1),4);
            for(int i=0;i<10;i++)e.write(DATA+0x74+i*4,value(bits,i),4);
            invoke(e,0x4014a084L,DATA+0x6c);results.add(e.register(10));
            invoke(e,0x400d4204L,DATA);results.add(e.register(10));
            invoke(e,0x400d426cL,DATA);results.add(e.register(10));break;
        }
        case "fan": {
            JsonArray f=in.getAsJsonArray("fans");
            for(int i=0;i<2;i++) {
                JsonArray a=f.get(i).getAsJsonArray();long p=DATA+i*0x20;
                e.write(p+0x19,value(a,0),1);e.write(p+0x1a,value(a,1),1);
                e.write(p+0x1c,value(a,2),4);e.write(p+0x20,value(a,3),4);
            }
            invoke(e,0x400d4180L,DATA);results.add(e.register(10));
            invoke(e,0x400d4154L,DATA+0x1c);results.add(e.register(10));break;
        }
        case "pump": {
            JsonArray b=in.getAsJsonArray("bits");int[] offsets={0x5c,0x68,8,12,16};
            for(int i=0;i<5;i++)e.write(DATA+offsets[i],value(b,i),4);
            invoke(e,0x4014a0a0L,DATA);results.add(e.register(10));break;
        }
        case "errors": {
            long error=in.get("error").getAsLong(),existing=in.get("existing").getAsLong();
            invoke(e,0x400d554cL,error);results.add(e.register(10));
            e.write(DATA,existing,4);invoke(e,0x400d5734L,DATA,error);results.add(e.read(DATA,4));break;
        }
        case "button": {
            JsonArray w=in.getAsJsonArray("words");e.write(DATA,SINK,4);e.write(SINK,VTABLE,4);e.write(VTABLE,EVENT,4);
            e.write(DATA+4,value(w,0),1);e.write(DATA+8,value(w,1),4);e.write(DATA+12,value(w,2),4);e.write(DATA+16,value(w,3),4);
            for(JsonElement item:in.getAsJsonArray("steps")) {
                JsonArray step=item.getAsJsonArray();clock=value(step,1);gpioHigh=value(step,2)!=0;
                int method=step.get(0).getAsInt();long argument=value(step,3);
                invoke(e,method==0?0x400d43d0L:method==1?0x400d4428L:0x400d446cL,DATA,argument);
            }
            results.add(e.read(DATA+4,1));results.add(e.read(DATA+8,4));results.add(e.read(DATA+12,4));results.add(e.read(DATA+16,4));break;
        }
        default:throw new IllegalArgumentException("Unknown case "+op);
        }
        out.put("results",results);out.put("assertion_count",e.read(COUNTER,4));out.put("events",events);return out;
    }
    private String sha(Path path)throws Exception {
        byte[] bytes=MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(path));StringBuilder out=new StringBuilder();
        for(byte value:bytes)out.append(String.format("%02x",value&255));return out.toString();
    }
    @Override public void run()throws Exception {
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input JSON and output JSON");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();
        List<Map<String,Object>> cases=new ArrayList<>();Map<String,Object> report=new LinkedHashMap<>();
        try(PicoStillExecution e=new PicoStillExecution(currentProgram,monitor,this::boundary)) {
            for(JsonElement input:inputs)cases.add(runCase(e,input.getAsJsonObject()));
            report.put("input_sha256",PicoStillExecution.IMAGE_SHA);report.put("cases_input_sha256",sha(Path.of(args[0])));
            report.put("cases",cases);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original Xtensa instruction bodies including both returning assertion helpers; counted assertions update OEM RAM and execute the delay call. Explicit ROM/libm arithmetic, diagnostic output, GPIO, clock, delay and button event boundaries. Window prologue/return adapted, one thread only.");
            report.put("hardware_tested",false);report.put("full_cpu_emulation",false);
        }
        Files.writeString(Path.of(args[1]),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        println("Executed "+cases.size()+" original PicoStill core cases");
    }
}
