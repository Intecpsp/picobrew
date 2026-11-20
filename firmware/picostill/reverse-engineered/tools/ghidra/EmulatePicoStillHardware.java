// Original actuator, indicator and pump-test instructions; explicit hardware boundaries.
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.zip.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulatePicoStillHardware extends GhidraScript {
    private static final long DATA=0x3ff00000L,TEXT=DATA+0x600,SINK=DATA+0x800,VTABLE=SINK+0x100,SNAPSHOT=0x7ff01000L,COUNTER=0x3ffc3078L;
    private final Set<Long> nested=new HashSet<>(Arrays.asList(0x400d405cL,0x400d4678L,0x400d46b8L,
        0x400d2af8L,0x400d2e64L,0x400d2ee4L,0x4014a074L,0x400d32a4L,0x400d32c4L,0x400d5bc4L,
        0x400d5cc4L,0x400d5ce0L,0x400d45c4L,0x400d4a68L));
    private List<String> events;private long clock;private boolean high;private long pressure;
    private long u(JsonArray a,int i){return a.get(i).getAsLong()&0xffffffffL;}
    private String string(PicoStillExecution e,long p)throws Exception {
        StringBuilder out=new StringBuilder();for(int i=0;i<512;i++){long b=e.read(p+i,1);if(b==0)return out.toString();out.append((char)b);}throw new IllegalStateException("Unterminated string");
    }
    private void boundary(PicoStillExecution e,long target)throws Exception {
        if(target==0x400d4678L){events.add("assertion:"+string(e,e.register(10))+":"+e.register(11)+":"+string(e,e.register(12)));e.invoke(target);return;}
        if(nested.contains(target)){e.invoke(target);return;}if(e.arithmetic(target))return;
        switch((int)target){
        case 0x400d28f8:case 0x400d290c:case 0x400d2938:e.countBoundary("diagnostic_output");events.add("message:"+string(e,e.register(10)));return;
        case 0x400d4600:e.countBoundary("assertion_diagnostic_format_output");return;
        case 0x40081b90:{e.countBoundary("diagnostic_basename");String s=string(e,e.register(10));e.register(10,e.register(10)+Math.max(s.lastIndexOf('\\'),s.lastIndexOf('/'))+1);return;}
        case 0x400d294c:e.countBoundary("diagnostic_variable_output");events.add("variable:"+string(e,e.register(10))+":"+string(e,e.register(11))+":"+string(e,e.register(12)));return;
        case 0x40081b50:{long pc=e.emulator.getExecutionAddress().getOffset();if(pc>=0x400d4678L&&pc<0x400d46b8L)e.countBoundary("diagnostic_millis");else{e.countBoundary("functional_millis");events.add("clock:"+clock);}e.register(10,clock);return;}
        case 0x400dd9bc:e.countBoundary("Arduino_delay");events.add("delay:"+e.register(10));return;
        case 0x400810c0:e.countBoundary("Arduino_pinMode");events.add("mode:"+e.register(10)+":"+e.register(11));return;
        case 0x400812c0:e.countBoundary("Arduino_digitalRead");events.add("read:"+e.register(10)+":"+(high?1:0));e.register(10,high?1:0);return;
        case 0x40081258:e.countBoundary("Arduino_digitalWrite");events.add("write:"+e.register(10)+":"+e.register(11));return;
        case 0x400dd748:e.countBoundary("Arduino_ledcSetup");events.add("pwm_setup:"+e.register(10)+":"+e.number(12)+":"+e.register(14));return;
        case 0x400dd95c:e.countBoundary("Arduino_ledcAttachPin");events.add("pwm_attach:"+e.register(10)+":"+e.register(11));return;
        case 0x400dd880:e.countBoundary("Arduino_ledcWrite");events.add("pwm_write:"+e.register(10)+":"+e.register(11));return;
        case 0x4000c978:{e.countBoundary("ROM_floatundidf");long raw=e.register(10)|(e.register(11)<<32);e.number(10,raw>=0?(double)raw:(double)(raw&Long.MAX_VALUE)+0x1p63);return;}
        case 0x400014c0:e.countBoundary("ROM_strlen");e.register(10,string(e,e.register(10)).length());return;
        case 0x4000c2c8:{e.countBoundary("ROM_memcpy");long size=e.register(12);if(size>4096)throw new IllegalStateException("Copy bound");e.emulator.writeMemory(e.address(e.register(10)),e.emulator.readMemory(e.address(e.register(11)),(int)size));return;}
        case 0x4000c44c:{e.countBoundary("ROM_memset");long size=e.register(12);if(size>4096)throw new IllegalStateException("Clear bound");byte[] bytes=new byte[(int)size];Arrays.fill(bytes,(byte)e.register(11));e.emulator.writeMemory(e.address(e.register(10)),bytes);return;}
        case (int)SNAPSHOT:e.countBoundary("hardware_snapshot_sink");events.add("snapshot");for(int i=0;i<24;i++)e.write(e.register(11)+i*4,i==9?pressure:0x3f000000L+i,4);return;
        default:throw new IllegalStateException(String.format("Unmodeled hardware boundary 0x%08x",target));
        }
    }
    private void call(PicoStillExecution e,long address,long... args)throws Exception{for(int i=0;i<args.length;i++)e.register(10+i,args[i]);e.invoke(address);}
    private void fan(PicoStillExecution e,long base,JsonArray w)throws Exception {
        e.write(base,u(w,0),1);e.write(base+1,u(w,1),1);e.write(base+2,u(w,2),1);e.write(base+8,u(w,3),4);e.write(base+12,u(w,4),4);
        e.write(base+16,u(w,5),1);e.write(base+17,u(w,6),1);e.write(base+18,u(w,7),1);e.write(base+20,u(w,8),4);e.write(base+24,u(w,9),4);e.write(base+28,u(w,10),4);
    }
    private void fanWords(PicoStillExecution e,long base,List<Long> out)throws Exception {
        int[] offsets={0,1,2,8,12,16,17,18,20,24,28},sizes={1,1,1,4,4,1,1,1,4,4,4};
        for(int i=0;i<offsets.length;i++)out.add(e.read(base+offsets[i],sizes[i]));
    }
    private Map<String,Object> run(PicoStillExecution e,JsonObject in)throws Exception {
        e.resetRegisters();e.clear(DATA,4096);e.write(COUNTER,0,4);events=new ArrayList<>();clock=0;high=false;
        List<Long> out=new ArrayList<>();String op=in.get("op").getAsString();
        if(op.equals("output")) {
            e.write(DATA,19,1);e.write(DATA+1,in.get("initial").getAsLong(),1);
            for(JsonElement item:in.getAsJsonArray("steps")){JsonArray s=item.getAsJsonArray();call(e,u(s,0)==0?0x400d2ee4L:0x400d2f68L,DATA,u(s,1));}
            out.add(e.read(DATA+1,1));
        }else if(op.equals("fan")) {
            fan(e,DATA,in.getAsJsonArray("words"));
            for(JsonElement item:in.getAsJsonArray("steps")){JsonArray s=item.getAsJsonArray();clock=u(s,1);high=u(s,2)!=0;call(e,u(s,0)==0?0x400d2e3cL:u(s,0)==1?0x400d2e64L:0x400d4740L,DATA,u(s,3),u(s,4));}
            fanWords(e,DATA,out);
        }else if(op.equals("fanbank")) {
            JsonArray w=in.getAsJsonArray("words");e.write(DATA,18,1);e.write(DATA+1,u(w,0),1);
            for(int i=0;i<2;i++){long p=DATA+8+i*32;e.write(p,i+1,1);e.write(p+1,i==0?12:2,1);e.write(p+2,i==0?5:17,1);e.write(p+17,u(w,i+1),1);}
            for(JsonElement item:in.getAsJsonArray("steps")){JsonArray s=item.getAsJsonArray();clock=u(s,1);high=u(s,2)!=0;
                if(u(s,0)==0)call(e,0x400d2f00L,DATA,u(s,3));else{e.write(TEXT,u(s,3),1);call(e,0x400d2f34L,DATA-0x578,TEXT);}}
            out.add(e.read(DATA+1,1));fanWords(e,DATA+8,out);fanWords(e,DATA+40,out);
        }else if(op.equals("led")) {
            e.write(DATA,26,1);e.write(DATA+1,25,1);e.write(DATA+2,33,1);e.write(DATA+0x4c,63,4);
            boolean nullText=in.get("text").isJsonNull();String text=nullText?"":in.get("text").getAsString();e.emulator.writeMemory(e.address(TEXT),(text+"\0").getBytes(StandardCharsets.US_ASCII));
            for(JsonElement item:in.getAsJsonArray("steps")){JsonArray s=item.getAsJsonArray();clock=u(s,1);long method=u(s,0),arg=u(s,2);
                if(method==0)call(e,0x400d32e4L,DATA);else if(method==1)call(e,0x400d32a4L,DATA,arg);
                else if(method==2)call(e,0x400d5ce0L,DATA,nullText?0:TEXT,arg);else if(method==3)call(e,0x400d5dbcL,DATA,arg);else call(e,0x400d5be0L,DATA);}
            for(int i=0;i<3;i++)out.add(e.read(DATA+3+i,1));for(int i=0;i<64;i++)out.add(e.read(DATA+8+i,1));
            for(int i=0;i<6;i++)out.add(e.read(DATA+0x48+i*4,4));
        }else if(op.equals("pump_test")) {
            e.write(DATA,in.get("state").getAsLong(),4);for(int i=0;i<2;i++){e.write(DATA+8+i*4,u(in.getAsJsonArray("started"),i),4);e.write(DATA+16+i*4,u(in.getAsJsonArray("vacuum"),i),4);e.write(DATA+24+i*4,u(in.getAsJsonArray("deadline"),i),4);}
            for(int i=0;i<3;i++)e.write(DATA+0x80+i*4,u(in.getAsJsonArray("pressures"),i),4);e.write(DATA+0x8c,in.get("error").getAsLong(),4);
            e.write(SINK,VTABLE,4);e.write(VTABLE,SNAPSHOT,4);
            for(JsonElement item:in.getAsJsonArray("steps")){JsonArray s=item.getAsJsonArray();if(u(s,0)==0){e.write(TEXT,u(s,3),4);call(e,0x400d4a68L,DATA,TEXT);}else{
                e.write(TEXT,u(s,1),4);e.write(TEXT+4,u(s,2),4);e.write(TEXT+8,u(s,3),4);e.write(TEXT+12,u(s,4),4);pressure=u(s,5);call(e,0x400d4af0L,DATA,TEXT,TEXT+8,SINK);}}
            out.add(e.read(DATA,4));for(int i=2;i<60;i++)out.add(e.read(DATA+i*4,4));
        }else throw new IllegalArgumentException("Unknown case "+op);
        Map<String,Object> row=new LinkedHashMap<>();row.put("id",in.get("id").getAsString());row.put("words",out);row.put("events",events);row.put("assertion_count",e.read(COUNTER,4));return row;
    }
    private String sha(Path path)throws Exception {StringBuilder s=new StringBuilder();for(byte b:MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(path)))s.append(String.format("%02x",b&255));return s.toString();}
    @Override public void run()throws Exception {
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected compressed inputs and output");
        JsonArray inputs;try(GZIPInputStream stream=new GZIPInputStream(Files.newInputStream(Path.of(args[0])))){inputs=JsonParser.parseReader(new InputStreamReader(stream,StandardCharsets.UTF_8)).getAsJsonArray();}
        Map<String,Object> report=new LinkedHashMap<>();List<Map<String,Object>> cases=new ArrayList<>();
        try(PicoStillExecution e=new PicoStillExecution(currentProgram,monitor,this::boundary)){
            for(JsonElement input:inputs)cases.add(run(e,input.getAsJsonObject()));report.put("input_sha256",PicoStillExecution.IMAGE_SHA);report.put("cases_input_gzip_sha256",sha(Path.of(args[0])));
            report.put("cases",cases);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original output/fan/LED/pump-test instruction bodies, nested helpers and returning assertions. Explicit ROM arithmetic/memory, GPIO/PWM, clock/delay, diagnostic and hardware snapshot boundaries. Single thread and adapted register windows, no physical qualification.");
            report.put("full_cpu_emulation",false);report.put("hardware_tested",false);
        }
        try(GZIPOutputStream out=new GZIPOutputStream(Files.newOutputStream(Path.of(args[1])))){out.write((new Gson().toJson(report)+"\n").getBytes(StandardCharsets.UTF_8));}
        println("Executed "+cases.size()+" original PicoStill hardware cases");
    }
}
