// Full original Still backend and JSON callees, including returning assertions.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.io.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
public class EmulateStillAuthorization extends GhidraScript {
    private static final long OBJECT=0x3ff30000L,HTTP=0x3ff31000L,RECORD=0x3ff33000L,TEXT=0x3ff35000L,RESPONSE=0x3ff3a000L;
    private final Set<Long> original=new HashSet<>(Arrays.asList(0x401bf788L,0x401bf764L,0x400d160cL,0x401bfa24L,0x400eff2cL,0x400ef278L,0x400effccL,0x40110cf0L,0x40119d84L,0x4011a228L,0x401163a8L,0x401164acL,0x401118bcL,0x401bf1bcL,0x400fa400L,0x400efe1cL,0x4010c614L,0x4010c4dcL,0x400e1180L,0x401bfa48L,0x400d0f80L,0x400d0ffcL,0x400d1040L,0x400d115cL,0x400d7888L,0x400dade8L,0x400e1250L,0x400ed990L,0x400ee998L,0x400eed28L,0x400eed44L,0x400eefb4L,0x400ef180L,0x400ef214L,0x400ef230L,0x400ee998L,0x400f1efcL,0x400f2010L,0x400f8190L,0x400f81c4L,0x400f82ccL,0x401007f0L,0x40100830L,0x40100938L,0x40100954L,0x400f9310L,0x400d1184L,0x400f04fcL,0x40102120L,0x4010ca44L,0x40110e9cL,0x401108e8L,0x400fafd4L,0x4011b1ecL,0x4011b250L,0x400f0240L,0x400f0268L,0x400d1508L,0x4011d8ccL,0x401bf7c0L,0x401bfab4L,0x400d0e60L,0x400d147cL,0x400d151cL,0x400d1530L,0x400d154cL,0x400d1574L,
        0x400d1590L,0x400d15a8L,0x400d15ccL,0x400d15f0L,0x400d1638L,0x400d165cL,0x400d585cL,0x400d598cL,
        0x400dee30L,0x400defa4L,0x400ef118L,0x400ef160L,0x400ef180L,0x400f005cL,0x400f02a4L,0x400f02b8L,0x400f202cL,
        0x400f50f4L,0x400f82ccL,0x400f82e8L,0x400f88c4L,0x400f9348L,0x400f944cL,0x400fa1f8L,0x400fa8d0L,
        0x40100938L,0x40100954L,0x401bf254L,0x401bf3d4L,0x401bf40cL,0x401bf484L,
        0x401bf62cL,0x401bf678L,0x401bf68cL,0x401bf6b4L,0x401bf6d8L,0x401bf6ecL,0x401bf708L,0x401bf724L,
        0x401bf79cL,0x401bf7acL,0x401bf9dcL,0x401bf9f8L,0x401bfa6cL));
    private JsonObject input;private List<Object> effects,assertions,delays;private long clock;
    private void text(Z119StillExecution e,long p,String value){if(value==null)return;e.emulator.writeMemory(e.address(p),(value+"\0").getBytes(StandardCharsets.UTF_8));}
    private String text(Z119StillExecution e,long p)throws Exception{if(p==0)return null;ByteArrayOutputStream out=new ByteArrayOutputStream();for(int i=0;i<20000;i++){int c=(int)e.read(p+i,1);if(c==0)return out.toString(StandardCharsets.UTF_8);out.write(c);}throw new IllegalStateException("Unterminated");}
    private long number(JsonObject in,String key,long fallback){return in.has(key)?in.get(key).getAsLong():fallback;}
    private boolean flag(JsonObject in,String key,boolean fallback){return in.has(key)?in.get(key).getAsBoolean():fallback;}
    private String value(JsonObject in,String key,String fallback){return in.has(key)?in.get(key).isJsonNull()?null:in.get(key).getAsString():fallback;}
    private void boundary(Z119StillExecution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        switch((int)target){
        case 0x400db014:assertions.add(List.of(text(e,e.register(10)).replace('\\','/').replaceAll("^.*/",""),e.register(11),text(e,e.register(12))));e.invoke(target);return;
        case 0x40081388:e.countBoundary("millis");e.register(10,clock);return;
        case 0x401289a0:e.countBoundary("delay");delays.add(e.register(10));clock=(clock+e.register(10))&0xffffffffL;return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:e.countBoundary("diagnostic_output");if(text(e,e.register(10)).contains("%s:%u"))effects.add(List.of("diagnostic",e.register(15)));return;
        case 0x4010c7ec:e.countBoundary("HttpClient_configure_fixed_WiFiClientSecure");effects.add(List.of("configure",text(e,e.register(12)),e.register(14)));e.register(10,flag(input,"configured",true)?1:0);return;
        case 0x40110e2c:e.countBoundary("HttpClient_authorization_without_credential_capture");effects.add(List.of("authorization",0));return;
        case 0x400f912c:e.countBoundary("HttpClient_POST");effects.add(List.of("POST",text(e,e.register(11)),text(e,e.register(12))));e.register(10,flag(input,"requested",true)?1:0);return;
        case 0x400014c0:e.countBoundary("ROM_strlen");e.register(10,text(e,e.register(10)).getBytes(StandardCharsets.UTF_8).length);return;
        case 0x40001274:e.countBoundary("ROM_strcmp");e.register(10,text(e,e.register(10)).compareTo(text(e,e.register(11))));return;
        case 0x4000c44c:e.countBoundary("ROM_memset");byte[] fill=new byte[(int)e.register(12)];Arrays.fill(fill,(byte)e.register(11));e.emulator.writeMemory(e.address(e.register(10)),fill);return;
        case 0x4000c2c8:e.countBoundary("ROM_memcpy");if(e.register(12)>0)e.emulator.writeMemory(e.address(e.register(10)),e.emulator.readMemory(e.address(e.register(11)),(int)e.register(12)));return;
        case 0x400015d4:e.countBoundary("ROM_strncpy");byte[] src=text(e,e.register(11)).getBytes(StandardCharsets.UTF_8),dest=new byte[(int)e.register(12)];System.arraycopy(src,0,dest,0,Math.min(src.length,dest.length));e.emulator.writeMemory(e.address(e.register(10)),dest);return;
        case 0x4012fecc:e.countBoundary("CXX_static_guard_acquire");e.register(10,e.read(e.register(10),1)==0?1:0);return;
        case 0x4012ff60:e.countBoundary("CXX_static_guard_release");e.write(e.register(10),1,1);return;
        case 0x401c01d8:e.countBoundary("__floatundidf");e.number(10,new java.math.BigInteger(Long.toUnsignedString(e.register(10)|(e.register(11)<<32))).doubleValue());return;
        default:if(original.contains(target)||(target>=0x401118e8L&&target<=0x401199e4L)){e.invoke(target);return;}throw new IllegalStateException(String.format("Unreviewed Still authorization call %08x",target));
        }
    }
    private void bounded(Z119StillExecution e,long p,String value,int capacity)throws Exception{
        String s=value==null?"":value;if(s.length()>capacity)s=s.substring(0,capacity);text(e,p,s);e.write(p+capacity+4,s.length(),4);e.write(p+capacity+8,capacity-s.length(),4);
    }
    private Map<String,Object> one(Z119StillExecution e,JsonObject in)throws Exception{
        input=in;e.resetRegisters();e.clear(0x3ff2f000L,0x11000);e.clear(0x3ffc1018L,0x58);clock=1000;effects=new ArrayList<>();assertions=new ArrayList<>();delays=new ArrayList<>();
        e.write(0x3ffc2e84L,0,4);e.write(0x3ffc4654L,0x5a5a5a5aL,4);e.write(OBJECT+8,RECORD,4);e.write(OBJECT+12,HTTP,4);e.write(OBJECT+0x10,flag(in,"pending",false)?1:0,1);
        e.write(OBJECT+0x270,TEXT+0x4000,4);text(e,TEXT+0x4000,"application/json");e.write(HTTP+0x20,number(in,"http_state",0),4);
        bounded(e,OBJECT+0x60,value(in,"initial_body",""),512);e.write(OBJECT+0x26c,flag(in,"truncated",false)?1:0,1);e.write(OBJECT+0x26d,flag(in,"frozen",false)?1:0,1);
        bounded(e,RECORD,value(in,"initial_uid","previous"),16);bounded(e,RECORD+0x24,value(in,"initial_date","previous-date"),32);e.write(RECORD+0x20,number(in,"initial_profile",99),4);
        String op=in.get("op").getAsString();Map<String,Object> out=new LinkedHashMap<>();out.put("id",in.get("id").getAsString());String machine=value(in,"machine","fixture-machine"),still=value(in,"still","fixture-still");text(e,TEXT,machine);text(e,TEXT+0x1000,still);
        if(op.equals("request")||op.equals("roundtrip")||op.equals("prepare")){
            e.register(10,op.equals("prepare")?OBJECT+0x60:OBJECT);e.register(11,machine==null?0:TEXT);e.register(12,still==null?0:TEXT+0x1000);e.register(13,flag(in,"cleaned",false)?1:0);e.invoke(op.equals("prepare")?0x40119d84L:0x4011a228L);
        }
        if(op.equals("headers")||op.equals("roundtrip")){
            e.write(RESPONSE,number(in,"status",200),4);e.write(RESPONSE+4,number(in,"content_length",-1),4);text(e,RESPONSE+8,value(in,"content_type","application/json"));e.register(10,OBJECT);e.register(11,RESPONSE);e.invoke(0x400effccL);if(op.equals("headers"))out.put("accepted",e.register(10)!=0);
        }
        if(op.equals("body")||op.equals("roundtrip")){
            if(in.has("chunks")){for(JsonElement chunk:in.getAsJsonArray("chunks")){String bytes=chunk.getAsString();text(e,TEXT,bytes);e.register(10,OBJECT);e.register(11,TEXT);e.register(12,bytes.getBytes(StandardCharsets.UTF_8).length);e.invoke(0x401118bcL);}}
            else{String bytes=value(in,"body","");text(e,TEXT,bytes);e.register(10,OBJECT);e.register(11,bytes==null?0:TEXT);e.register(12,number(in,"count",bytes==null?0:bytes.getBytes(StandardCharsets.UTF_8).length));e.invoke(0x401118bcL);}
        }
        if(op.equals("parse")||op.equals("tick")){
            e.clear(OBJECT+0x60,513);String json=value(in,"json","{}");bounded(e,OBJECT+0x60,json,512);e.register(10,op.equals("parse")?OBJECT+0x60:OBJECT);e.register(11,RECORD);e.invoke(op.equals("parse")?0x401163a8L:0x401164acL);if(op.equals("parse"))out.put("accepted",e.register(10)!=0);
        }
        if(op.equals("complete")||op.equals("roundtrip")){e.register(10,OBJECT);e.invoke(0x401bf1bcL);}
        if(op.equals("roundtrip")){e.write(HTTP+0x20,0,4);e.register(10,OBJECT);e.invoke(0x401164acL);}
        out.put("result",0);out.put("pending",e.read(OBJECT+0x10,1)!=0);Map<String,Object> record=new LinkedHashMap<>();record.put("uid",text(e,RECORD));record.put("profile",e.read(RECORD+0x20,4));record.put("date",text(e,RECORD+0x24));record.put("uid_truncated",e.read(RECORD+0x1c,1)!=0);record.put("date_truncated",e.read(RECORD+0x50,1)!=0);out.put("record",record);
        if(!List.of("parse","tick","roundtrip").contains(op))out.put("buffer",text(e,OBJECT+0x60));out.put("length",e.read(OBJECT+0x264,4));out.put("remaining",e.read(OBJECT+0x268,4));out.put("truncated",e.read(OBJECT+0x26c,1)!=0);out.put("frozen",e.read(OBJECT+0x26d,1)!=0);
        out.put("observed",List.of(e.read(OBJECT+0x10,1)!=0,e.read(RECORD+0x14,4)!=0,e.read(RECORD+0x48,4)!=0));out.put("effects",effects);out.put("assertions",assertions);out.put("delays",delays);out.put("counter",e.read(0x3ffc2e84L,4));out.put("clock",clock);return out;
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson gson=new Gson();
        try(Z119StillExecution e=new Z119StillExecution(currentProgram,monitor,this::boundary);BufferedWriter output=Files.newBufferedWriter(Path.of(args[1]))){int count=0;for(JsonElement in:inputs){try{output.write(gson.toJson(one(e,in.getAsJsonObject()))+"\n");}catch(Exception failure){throw new IllegalStateException("Still authorization case "+count,failure);}++count;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119StillExecution.IMAGE_SHA);report.put("cases",count);report.put("input_file_sha256",HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(Path.of(args[0])))));report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
    }
}
