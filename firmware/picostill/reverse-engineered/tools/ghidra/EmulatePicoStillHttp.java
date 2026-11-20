import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.util.*;
import java.util.zip.*;
import java.math.BigInteger;
import com.google.gson.*;

public class EmulatePicoStillHttp extends EmulatePicoStillController {
    private static final long BASE=0x3ff00000L,SERVER=BASE+0x800,DECODED=BASE+0xc00,TEXT=BASE+0x2000,OTHER=BASE+0x3000,ERROR=BASE+0x3100,COUNTER=0x3ffc3078L;
    protected JsonArray items;protected int position,tick;protected boolean keep;
    private void text(PicoStillExecution e,long pointer,String value){e.emulator.writeMemory(e.address(pointer),(value+"\0").getBytes(StandardCharsets.ISO_8859_1));}
    private long decimal(String text){java.util.regex.Matcher m=java.util.regex.Pattern.compile("^[\\t\\n\\r\\f \\u000b]*([+-]?[0-9]+)").matcher(text);if(!m.find())return 0;BigInteger value=new BigInteger(m.group(1));return value.max(BigInteger.valueOf(Integer.MIN_VALUE)).min(BigInteger.valueOf(Integer.MAX_VALUE)).longValue()&0xffffffffL;}
    @Override protected void boundary(PicoStillExecution e,long target)throws Exception {
        switch((int)target){
        case 0x7ff0203c:{e.countBoundary("Client_connected");boolean connected=keep||position<items.size();events.add("connected:"+(connected?1:0));e.register(10,connected?1:0);return;}
        case 0x7ff02010:e.countBoundary("Client_available");events.add("available:"+(position<items.size()?1:0));e.register(10,position<items.size()?1:0);return;
        case 0x7ff02014:{e.countBoundary("Client_read");int value=items.get(position++).getAsInt();events.add("read:"+value);e.register(10,value);return;}
        case 0x400dd98c:e.countBoundary("Arduino_yield");events.add("yield");clock=(clock+tick)&0xffffffffL;return;
        case 0x400d2abc:e.countBoundary("integer_diagnostic");events.add("integer:"+string(e,e.register(10))+":"+string(e,e.register(11))+":"+e.register(12));return;
        case 0x400014c0:e.countBoundary("ROM_strlen");e.register(10,string(e,e.register(10)).length());return;
        case 0x400015d4:{e.countBoundary("ROM_strncpy");int count=(int)e.register(12);byte[] bytes=new byte[count];boolean end=false;for(int i=0;i<count;i++){if(!end){bytes[i]=(byte)e.read(e.register(11)+i,1);end=bytes[i]==0;}}e.emulator.writeMemory(e.address(e.register(10)),bytes);return;}
        case 0x400566c4:e.countBoundary("ROM_atoi_32bit_newlib");e.register(10,decimal(string(e,e.register(10))));return;
        case 0x4005681c:{e.countBoundary("ROM_strtol_hex");if(e.register(11)!=0||e.register(12)!=16)throw new IllegalStateException("Unexpected strtol");String s=string(e,e.register(10));
            java.util.regex.Matcher m=java.util.regex.Pattern.compile("^0x([0-9a-fA-F]+)").matcher(s);e.register(10,m.find()?Long.parseLong(m.group(1),16):0);return;}
        default:super.boundary(e,target);
        }
    }
    private Map<String,Object> http(PicoStillExecution e,JsonObject in)throws Exception {
        events=new ArrayList<>();clock=in.get("now").getAsLong();tick=in.get("tick").getAsInt();keep=in.get("keep").getAsInt()!=0;items=in.getAsJsonArray("items");position=0;
        e.resetRegisters();e.clear(BASE,0x4000);e.write(COUNTER,0,4);e.write(0x3ffc4770L,0x12345678L,4);e.write(BASE+0x100,0xffffffffL,4);
        for(int i=0;i<128;i++)e.write(DECODED+i,0xcc,1);text(e,TEXT,in.get("text").getAsString());text(e,OTHER,in.get("other").getAsString());text(e,ERROR,in.get("error").getAsString());
        if(!in.get("error").getAsString().isEmpty())e.write(BASE+0x304,ERROR,4);long result=0;String op=in.get("op").getAsString();
        if(op.equals("parse")){
            e.write(BASE+0x1000,in.get("timeout").getAsLong(),4);e.write(BASE+0x1100,BASE+0x1200,4);
            e.write(BASE+0x1210,0x7ff02010L,4);e.write(BASE+0x1214,0x7ff02014L,4);e.write(BASE+0x123c,0x7ff0203cL,4);call(e,0x400d35b0L,BASE+0x1000,BASE+0x1100,BASE);
        }else if(op.equals("request"))call(e,0x400d2bf0L,TEXT,BASE);
        else if(op.equals("form")){byte[] data=in.get("text").getAsString().getBytes(StandardCharsets.ISO_8859_1);e.emulator.writeMemory(e.address(BASE+0x104),Arrays.copyOf(data,Math.min(data.length,512)));call(e,0x400d4dc8L,SERVER,BASE);}
        else if(op.equals("decode"))call(e,0x400d4d20L,DECODED,in.get("capacity").getAsLong(),TEXT);
        else if(op.equals("boolean")){call(e,0x400d2c5cL,TEXT);result=e.register(10);}
        else if(op.equals("equal")){call(e,0x400d2b74L,TEXT,OTHER);result=e.register(10);}
        else if(op.equals("prefix")){call(e,0x400d2bb8L,TEXT,OTHER);result=e.register(10);}
        else throw new IllegalArgumentException("Unknown operation");
        Map<String,Object> out=new LinkedHashMap<>();out.put("id",in.get("id").getAsString());out.put("bytes",Arrays.asList(hex(e,BASE,0x304),hex(e,SERVER+0x331,48),hex(e,SERVER+0x361,64),hex(e,DECODED,128)));
        long error=e.read(BASE+0x304,4);out.put("error",error==0?"":string(e,error));out.put("result",result);out.put("events",events);out.put("assertion_count",e.read(COUNTER,4));out.put("clock",clock);return out;
    }
    @Override public void run()throws Exception {
        nested.addAll(Arrays.asList(0x400d2b74L,0x400d2bb8L,0x40149ff0L,0x4014a014L,0x4014a054L,0x400d2bf0L,0x400d4d20L));String[] args=getScriptArgs();JsonArray inputs;
        try(GZIPInputStream stream=new GZIPInputStream(Files.newInputStream(Path.of(args[0])))){inputs=JsonParser.parseReader(new InputStreamReader(stream,StandardCharsets.UTF_8)).getAsJsonArray();}
        Map<String,Object> report=new LinkedHashMap<>();List<Map<String,Object>> cases=new ArrayList<>();
        try(PicoStillExecution e=new PicoStillExecution(currentProgram,monitor,this::boundary)){
            e.instructionLimit=200000;
            for(JsonElement in:inputs){try{cases.add(http(e,in.getAsJsonObject()));}catch(Exception error){throw new IllegalStateException(in.getAsJsonObject().get("id").getAsString(),error);}}
            report.put("input_sha256",PicoStillExecution.IMAGE_SHA);report.put("cases_input_gzip_sha256",sha(Path.of(args[0])));report.put("cases",cases);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original HTTP request/header/body parser, credential form and string/URL-decoding instructions including returning assertions. Client methods, clock/yield, diagnostic formatting, ROM memory/strlen and newlib numeric conversions are explicit boundaries. URI lengths beyond 255 and unterminated form bodies are owned rejection domains, not asserted equivalent to original out-of-bounds behavior.");report.put("hardware_tested",false);report.put("full_cpu_emulation",false);
        }
        try(GZIPOutputStream out=new GZIPOutputStream(Files.newOutputStream(Path.of(args[1])))){out.write((new Gson().toJson(report)+"\n").getBytes(StandardCharsets.UTF_8));}println("Executed "+cases.size()+" original PicoStill HTTP parser cases");
    }
}
