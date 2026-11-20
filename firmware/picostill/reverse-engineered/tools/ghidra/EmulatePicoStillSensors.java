// Original static calibration stores, ADC and pressure reads, sample metrics.
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.zip.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulatePicoStillSensors extends GhidraScript {
    private static final long DATA=0x3ff00000L,COUNTER=0x3ffc3078L;
    private final Set<Long> nested=new HashSet<>(Arrays.asList(0x400d405cL,0x400d4678L,0x400d46b8L,
        0x400d4074L,0x400d2f9cL,0x400d6ed4L,0x400d6ef4L,0x400d68c4L,0x400d2fbcL));
    private List<String> events;private JsonArray rx;private int readIndex,returned,available;
    private String string(PicoStillExecution e,long p)throws Exception {
        StringBuilder s=new StringBuilder();for(int i=0;i<512;i++){long b=e.read(p+i,1);if(b==0)return s.toString();s.append((char)b);}throw new IllegalStateException("Unterminated original string");
    }
    private void boundary(PicoStillExecution e,long target)throws Exception {
        if(target==0x400d4678L){events.add("assertion:"+string(e,e.register(10))+":"+e.register(11)+":"+string(e,e.register(12)));e.invoke(target);return;}
        if(nested.contains(target)){e.invoke(target);return;}if(e.arithmetic(target))return;
        switch((int)target){
        case 0x400dbd88:e.countBoundary("IP_constructor_outside_calibration_scope");return;
        case 0x400d82dc:e.countBoundary("PicoStill_constructor_outside_calibration_scope");return;
        case 0x400d28f8:case 0x400d290c:case 0x400d2938:e.countBoundary("diagnostic_output");events.add("message:"+string(e,e.register(10)));return;
        case 0x400d4600:e.countBoundary("assertion_diagnostic_format_output");return;
        case 0x40081b90:{e.countBoundary("diagnostic_basename");String s=string(e,e.register(10));e.register(10,e.register(10)+Math.max(s.lastIndexOf('\\'),s.lastIndexOf('/'))+1);return;}
        case 0x40081b50:e.countBoundary("diagnostic_millis");e.register(10,1234);return;
        case 0x400dd9bc:e.countBoundary("Arduino_delay");events.add("delay:"+e.register(10));return;
        case 0x4014a1a4:e.countBoundary("Wire_beginTransmission");events.add("begin:"+e.register(11));return;
        case 0x4014a164:e.countBoundary("Wire_write");events.add("write:"+e.register(11));e.register(10,1);return;
        case 0x400d9a7c:e.countBoundary("Wire_endTransmission");events.add("end");e.register(10,0);return;
        case 0x400d9a68:e.countBoundary("Wire_requestFrom");events.add("request:"+e.register(11)+":"+e.register(12));e.register(10,returned);return;
        case 0x4014a120:e.countBoundary("Wire_available");events.add("available:"+available);e.register(10,available);return;
        case 0x4014a130:{e.countBoundary("Wire_read");int value=readIndex<rx.size()?rx.get(readIndex++).getAsInt():-1;events.add("read:"+value);e.register(10,value);return;}
        case 0x4000c44c:{e.countBoundary("ROM_memset");int size=(int)e.register(12);if(size<0||size>4096)throw new IllegalStateException("Memory bound");byte[] b=new byte[size];Arrays.fill(b,(byte)e.register(11));e.emulator.writeMemory(e.address(e.register(10)),b);return;}
        default:throw new IllegalStateException(String.format("Unmodeled sensor boundary 0x%08x",target));
        }
    }
    private void call(PicoStillExecution e,long target,long... args)throws Exception{for(int i=0;i<args.length;i++)e.register(10+i,args[i]);e.invoke(target);}
    private Map<String,Object> run(PicoStillExecution e,JsonObject in)throws Exception {
        e.resetRegisters();e.clear(DATA,4096);e.clear(0x3ffc3080L,160);e.write(COUNTER,0,4);events=new ArrayList<>();
        call(e,0x400d8688L);String op=in.get("op").getAsString();Map<String,Object> out=new LinkedHashMap<>();out.put("id",in.get("id").getAsString());
        List<Long> words=new ArrayList<>();
        if(op.equals("convert")) {
            e.write(DATA,in.get("adc").getAsInt(),4);call(e,0x400d4074L,0x3ffc3080L+40L*in.get("channel").getAsInt(),DATA);
            out.put("double_bits",String.format("%016x",e.register(10)|(e.register(11)<<32)));
        } else {
            rx=in.getAsJsonArray("bytes");readIndex=0;returned=in.get("returned").getAsInt();available=in.get("available").getAsInt();
            e.write(DATA+0x1c,0x34,1);e.write(DATA+0x1d,in.has("three")?in.get("three").getAsInt():0,1);
            e.write(DATA+0x38,in.has("status")?in.get("status").getAsInt():0x40,1);e.write(DATA+0x39,0x18,1);e.write(DATA+0x3a,in.has("range")?in.get("range").getAsInt():25,1);
            if(in.has("counts")){JsonArray counts=in.getAsJsonArray("counts");for(int i=0;i<2;i++)e.write(DATA+0x6c+i*4,counts.get(i).getAsLong(),4);
                for(int i=0;i<5;i++){e.write(DATA+0x58+i*4,in.getAsJsonArray("values").get(i).getAsLong(),4);e.write(DATA+0x74+i*4,in.getAsJsonArray("rates").get(i).getAsLong(),4);e.write(DATA+0x88+i*4,in.getAsJsonArray("averages").get(i).getAsLong(),4);}}
            if(op.equals("adc"))call(e,0x400d6ef4L,DATA+0x1c);else if(op.equals("pressure"))call(e,0x400d68c4L,DATA+0x38);
            else call(e,op.equals("temperatures")?0x400d6f7cL:0x400d6964L,DATA);
            for(int i=0;i<4;i++)words.add(e.read(DATA+0x20+i*4,4));for(int i=0;i<4;i++)words.add(e.read(DATA+0x30+i,1));
            words.add(e.read(DATA+0x40,4));words.add(e.read(DATA+0x44,4));words.add(e.read(DATA+0x48,1));
            for(int i=0;i<17;i++)words.add(e.read(DATA+0x58+i*4,4));
        }
        out.put("words",words);out.put("events",events);out.put("assertion_count",e.read(COUNTER,4));return out;
    }
    private String sha(Path p)throws Exception{StringBuilder s=new StringBuilder();for(byte b:MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(p)))s.append(String.format("%02x",b&255));return s.toString();}
    @Override public void run()throws Exception {
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected gzip inputs/output");JsonArray inputs;
        try(GZIPInputStream in=new GZIPInputStream(Files.newInputStream(Path.of(args[0])))){inputs=JsonParser.parseReader(new InputStreamReader(in,StandardCharsets.UTF_8)).getAsJsonArray();}
        Map<String,Object> report=new LinkedHashMap<>();List<Map<String,Object>> cases=new ArrayList<>();
        try(PicoStillExecution e=new PicoStillExecution(currentProgram,monitor,this::boundary)){
            for(JsonElement in:inputs)cases.add(run(e,in.getAsJsonObject()));List<List<Long>> calibration=new ArrayList<>();
            for(int i=0;i<4;i++){List<Long> words=new ArrayList<>();for(int j=0;j<9;j++)words.add(e.read(0x3ffc3080L+i*40+j*4,4));calibration.add(words);}
            report.put("input_sha256",PicoStillExecution.IMAGE_SHA);report.put("cases_input_gzip_sha256",sha(Path.of(args[0])));report.put("calibration_words",calibration);
            report.put("cases",cases);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original static initializer calibration stores; unrelated IP/application constructors are explicit external boundaries. Original ADC/pressure acquisition and sample/quality/average updates with Wire byte-stream and ROM arithmetic/libm boundaries. Register windows adapted, no physical sensor qualification.");
            report.put("hardware_tested",false);report.put("full_cpu_emulation",false);
        }
        try(GZIPOutputStream out=new GZIPOutputStream(Files.newOutputStream(Path.of(args[1])))){out.write((new Gson().toJson(report)+"\n").getBytes(StandardCharsets.UTF_8));}
        println("Executed "+cases.size()+" original sensor/calibration cases");
    }
}
