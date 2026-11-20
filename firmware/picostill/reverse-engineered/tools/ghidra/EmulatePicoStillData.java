import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.util.*;
import java.util.zip.*;
import com.google.gson.*;

public class EmulatePicoStillData extends EmulatePicoStillHttp {
    private static final long BASE=0x3ff00000L,DATA=BASE+0x18,TEXT=BASE+0x3000,OTHER=BASE+0x3200,OUT=BASE+0x3400,COUNTER=0x3ffc3078L;
    private String mode;
    private void text(PicoStillExecution e,long pointer,String value){e.emulator.writeMemory(e.address(pointer),(value+"\0").getBytes(StandardCharsets.ISO_8859_1));}
    @Override protected void boundary(PicoStillExecution e,long target)throws Exception {
        if(nested.contains(target)){super.boundary(e,target);return;}
        switch((int)target){
        case 0x400ec18c:{e.countBoundary("newlib_snprintf_literal_MAC_domain");String value=string(e,e.register(12));if(value.contains("%"))throw new IllegalStateException("Format outside literal MAC domain");int capacity=(int)e.register(11);text(e,e.register(10),value.substring(0,Math.min(value.length(),capacity-1)));e.register(10,value.length());return;}
        case 0x40063790:case 0x40063768:e.countBoundary("ROM_double_compare");e.register(10,e.number(10)<e.number(12)?-1:e.number(10)>e.number(12)?1:0);return;
        case 0x40002a78:e.countBoundary("ROM_fixdfsi");e.register(10,(int)e.number(10));return;
        case 0x400d294c:e.countBoundary("credential_diagnostic_output");events.add((mode.equals("configure")?"credential:":"variable:")+string(e,e.register(10))+":"+string(e,e.register(11))+":"+string(e,e.register(12))+(mode.equals("configure")?":"+e.register(13):""));return;
        default:super.boundary(e,target);
        }
    }
    private Map<String,Object> data(PicoStillExecution e,JsonObject in)throws Exception {
        e.resetRegisters();e.clear(BASE,0x4000);e.write(COUNTER,0,4);e.write(0x3ffc4770L,0x12345678L,4);events=new ArrayList<>();clock=1000;
        String raw=in.get("data").getAsString();for(int i=0;i<raw.length()/2;i++)e.write(DATA+i,Integer.parseInt(raw.substring(2*i,2*i+2),16),1);
        for(int i=0;i<128;i++)e.write(OUT+i,0xcc,1);
        for(int i=0;i<100;i++)e.write(BASE+0x108+i,0xcc,1);
        long[] pointers={DATA,DATA+16,DATA+48,DATA+64,DATA+96,DATA+128,DATA+192};for(int i=0;i<7;i++)e.write(DATA+208+i*4,pointers[i],4);
        text(e,TEXT,in.get("text").getAsString());text(e,OTHER,in.get("other").getAsString());String bytes=in.get("raw").getAsString();for(int i=0;i<bytes.length()/2;i++)e.write(OTHER+i,Integer.parseInt(bytes.substring(2*i,2*i+2),16),1);
        long result=0;String op=in.get("op").getAsString();mode=op;long number=in.get("number").getAsLong(),capacity=in.get("capacity").getAsLong();
        switch(op){
        case "init":call(e,0x400d50e8L,DATA,TEXT,OTHER);break;
        case "digit":call(e,0x400d48f4L,number);result=e.register(10)&255;break;
        case "copy":call(e,0x400d4860L,OUT,capacity,TEXT);break;
        case "hex":call(e,0x400d4928L,OUT,capacity,OTHER,number);break;
        case "uid":call(e,0x400d48b0L,DATA,TEXT);break;
        case "ssid":call(e,0x400d49e0L,DATA,TEXT);break;
        case "password":call(e,0x400d49f8L,DATA,TEXT);break;
        case "report":{
            long controller=BASE+0x1000;for(int i=0;i<5;i++)e.write(controller+0x480+4*i,e.read(OTHER+4*i,4),4);
            e.write(controller+0x421,e.read(OTHER+20,4),1);e.write(controller+0x3d9,e.read(OTHER+24,4),1);e.write(controller+0x411,e.read(OTHER+28,4),1);
            e.write(controller+0x4c8,e.read(OTHER+32,4),4);e.write(controller+0x568,e.read(OTHER+36,4),4);call(e,0x400d5938L,controller);e.emulator.writeMemory(e.address(OUT),e.emulator.readMemory(e.address(controller+0x4d8),128));break;}
        case "configure":call(e,0x400d4a10L,BASE,TEXT,OTHER);break;
        default:throw new IllegalArgumentException(op);
        }
        Map<String,Object> row=new LinkedHashMap<>();row.put("id",in.get("id").getAsString());row.put("data",hex(e,DATA,208));row.put("stored",hex(e,BASE+0x108,100));row.put("changed",e.read(BASE+0x16d,1));row.put("output",hex(e,OUT,128));row.put("result",result);row.put("events",events);row.put("assertion_count",e.read(COUNTER,4));row.put("clock",clock);return row;
    }
    @Override public void run()throws Exception {
        nested.addAll(Arrays.asList(0x400d4860L,0x400d48f4L,0x400d4928L,0x400d4984L,0x400d49e0L,0x400d49f8L,0x400d58c4L,0x400d58fcL,0x400d58e4L,0x400d44f8L,0x400de3d8L,0x400eb62cL));String[] args=getScriptArgs();JsonArray inputs;
        try(GZIPInputStream stream=new GZIPInputStream(Files.newInputStream(Path.of(args[0])))){inputs=JsonParser.parseReader(new InputStreamReader(stream,StandardCharsets.UTF_8)).getAsJsonArray();}
        Map<String,Object> report=new LinkedHashMap<>();List<Map<String,Object>> cases=new ArrayList<>();
        try(PicoStillExecution e=new PicoStillExecution(currentProgram,monitor,this::boundary)){
            for(JsonElement input:inputs){try{cases.add(data(e,input.getAsJsonObject()));}catch(Exception error){throw new IllegalStateException(input.getAsJsonObject().get("id").getAsString(),error);}}
            report.put("input_sha256",PicoStillExecution.IMAGE_SHA);report.put("cases_input_gzip_sha256",sha(Path.of(args[0])));report.put("cases",cases);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original identity/credential initialization, truncating copy, token XOR/hex and application configure instructions including returning assertion effects. ROM memory/strlen, literal-MAC snprintf and diagnostic output are explicit boundaries. Null/zero copy destinations, insufficient hex capacity, and printf-format MACs are source safety extensions outside claimed OEM equivalence.");report.put("hardware_tested",false);report.put("full_cpu_emulation",false);
        }
        try(GZIPOutputStream out=new GZIPOutputStream(Files.newOutputStream(Path.of(args[1])))){out.write((new Gson().toJson(report)+"\n").getBytes(StandardCharsets.UTF_8));}println("Executed "+cases.size()+" original PicoStill identity/credential cases");
    }
}
