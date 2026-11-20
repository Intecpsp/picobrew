// Full recovered MQTT wire operations. Socket calls and clocks are boundaries.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.io.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
public class EmulateMqttClient extends GhidraScript {
    private static final long OBJECT=0x3ff30000L,VTABLE=0x3ff32000L,CLIENT=0x3ff33000L,TEXT=0x3ff34000L;
    private long clock,step;private int at,connectResult;private long writeLimit;private boolean connected;private int[] incoming;private List<Object> effects;
    private final Set<Long> bodies=Set.of(0x401bff2cL,0x40126d8cL,0x40126b38L,0x40126b78L,0x40126b90L,0x40126cccL,0x401bfef0L,0x40126f4cL,0x401270fcL,0x4012718cL);
    private void write(Z119MqttExecution e,long p,String s){if(s!=null)e.emulator.writeMemory(e.address(p),(s+"\0").getBytes(StandardCharsets.UTF_8));}
    private String text(Z119MqttExecution e,long p)throws Exception{if(p==0)return null;ByteArrayOutputStream out=new ByteArrayOutputStream();for(int i=0;i<20000;i++){int c=(int)e.read(p+i,1);if(c==0)return out.toString(StandardCharsets.UTF_8);out.write(c);}throw new IllegalStateException("Unterminated");}
    private long number(JsonObject in,String key,long value){return in.has(key)?in.get(key).getAsLong():value;}
    private boolean flag(JsonObject in,String key,boolean value){return in.has(key)?in.get(key).getAsBoolean():value;}
    private String string(JsonObject in,String key,String value){return in.has(key)?in.get(key).isJsonNull()?null:in.get(key).getAsString():value;}
    private String hex(Z119MqttExecution e,long p,int size)throws Exception{return HexFormat.of().formatHex(e.emulator.readMemory(e.address(p),size));}
    private void boundary(Z119MqttExecution e,long target)throws Exception{
        if(bodies.contains(target)){e.invoke(target);return;}
        switch((int)target){
        case 0x40081388:e.countBoundary("millis");e.register(10,clock);clock=(clock+step)&0xffffffffL;return;
        case 0x400014c0:e.countBoundary("ROM_strlen");e.register(10,text(e,e.register(10)).getBytes(StandardCharsets.UTF_8).length);return;
        case 0x4000c2c8:case 0x4000c3c0:e.countBoundary(target==0x4000c2c8L?"ROM_memcpy":"ROM_memmove");if(e.register(12)>0)e.emulator.writeMemory(e.address(e.register(10)),e.emulator.readMemory(e.address(e.register(11)),(int)e.register(12)));return;
        case 0x70000100:e.countBoundary("Client_write");long count=Math.min(e.register(12),writeLimit);effects.add(List.of("write",hex(e,e.register(11),(int)e.register(12)),count));e.register(10,count);return;
        case 0x70000104:e.countBoundary("Client_available");effects.add(List.of("available",incoming.length-at));e.register(10,incoming.length-at);return;
        case 0x70000108:e.countBoundary("Client_read");long value=at<incoming.length?incoming[at++]&0xffffffffL:0xffffffffL;effects.add(List.of("read",value));e.register(10,value);return;
        case 0x7000010c:e.countBoundary("Client_flush");effects.add(List.of("flush"));return;
        case 0x70000110:e.countBoundary("Client_connect_IPAddress");connected=connectResult==1;effects.add(List.of("connect_ip",e.read(e.register(11)+4,4),e.register(12),(long)connectResult&0xffffffffL));e.register(10,connectResult);return;
        case 0x70000114:e.countBoundary("Client_connect_host");connected=connectResult==1;effects.add(List.of("connect",text(e,e.register(11)),e.register(12),(long)connectResult&0xffffffffL));e.register(10,connectResult);return;
        case 0x70000118:e.countBoundary("Client_stop");connected=false;effects.add(List.of("stop"));return;
        case 0x7000011c:e.countBoundary("Client_connected");effects.add(List.of("connected",connected?1:0));e.register(10,connected?1:0);return;
        case 0x70000200:e.countBoundary("MQTT_message_callback");if(e.register(13)>1024)throw new IllegalStateException("Invalid original callback span "+e.register(13));effects.add(List.of("message",text(e,e.register(11)),hex(e,e.register(12),(int)e.register(13)),e.register(13)));return;
        default:throw new IllegalStateException(String.format("Unreviewed MQTT call %08x",target));
        }
    }
    private Map<String,Object> one(Z119MqttExecution e,JsonObject in)throws Exception{
        e.resetRegisters();e.clear(OBJECT,0x11000);effects=new ArrayList<>();clock=number(in,"clock",1000);step=number(in,"clock_step",0);at=0;connected=flag(in,"connected",true);connectResult=(int)number(in,"connect_result",1);writeLimit=number(in,"write_limit",0xffffffffL);JsonArray bytes=in.has("incoming")?in.getAsJsonArray("incoming"):new JsonArray();incoming=new int[bytes.size()];for(int i=0;i<incoming.length;i++)incoming[i]=bytes.get(i).getAsInt();
        e.write(OBJECT,flag(in,"bound",true)?CLIENT:0,4);e.write(CLIENT,VTABLE,4);int[] slots={0xc,0x10,0x14,0x1c,0x20,0x24,0x2c,0x30};for(int i=0;i<slots.length;i++)e.write(VTABLE+slots[i],0x70000100L+i*4,4);
        e.write(OBJECT+0x430,number(in,"state",-1),4);e.write(OBJECT+0x408,number(in,"last_out",0),4);e.write(OBJECT+0x40c,number(in,"last_in",0),4);e.write(OBJECT+0x410,flag(in,"ping",false)?1:0,1);e.write(OBJECT+0x404,number(in,"message_id",0),2);e.write(OBJECT+0x418,flag(in,"callback",true)?0x70000200L:0,4);
        String host=string(in,"host","fixture.example");write(e,TEXT,host);e.write(OBJECT+0x424,host==null?0:TEXT,4);e.write(OBJECT+0x420,number(in,"address",0x01020304L),4);e.write(OBJECT+0x428,number(in,"port",8883),2);
        int op=(int)number(in,"op",0);e.register(10,OBJECT);boolean accepted=false;long length=0,lengthBytes=0;
        if(op==0){e.invoke(0x401bff2cL);accepted=e.register(10)!=0;}else if(op==1)e.invoke(0x40126d48L);
        else if(op==2){String[] names={"client_id","username","password","will_topic","will_message"};String[] defaults={"device","user","password",null,null};long[] pointers=new long[5];for(int i=0;i<5;i++){String s=string(in,names[i],defaults[i]);pointers[i]=s==null?0:TEXT+0x100+i*0x1000;write(e,pointers[i],s);}e.register(11,pointers[0]);e.register(12,pointers[1]);e.register(13,pointers[2]);e.register(14,pointers[3]);e.register(15,number(in,"will_qos",0));e.write(e.register(1),flag(in,"will_retain",false)?1:0,4);e.write(e.register(1)+4,pointers[4],4);e.invoke(0x40126d8cL);accepted=e.register(10)!=0;}
        else if(op==3){String topic=string(in,"topic","devices/fixture/messages/events/"),payload=string(in,"payload","state");write(e,TEXT+0x100,topic);write(e,TEXT+0x2100,payload);e.register(11,TEXT+0x100);e.register(12,TEXT+0x2100);e.register(13,payload.getBytes(StandardCharsets.UTF_8).length);e.register(14,flag(in,"retained",false)?1:0);e.invoke(0x401270fcL);accepted=e.register(10)!=0;}
        else if(op==4){write(e,TEXT+0x100,string(in,"topic","devices/fixture/messages/devicebound/#"));e.register(11,TEXT+0x100);e.register(12,number(in,"qos",0));e.invoke(0x4012718cL);accepted=e.register(10)!=0;}
        else if(op==5){e.invoke(0x40126f6cL);accepted=e.register(10)!=0;}
        else if(op==6){e.register(11,TEXT+0x9000);e.invoke(0x40126b90L);length=e.register(10);lengthBytes=e.read(TEXT+0x9000,1);}
        else throw new IllegalArgumentException("op");
        Map<String,Object> out=new LinkedHashMap<>();out.put("id",in.get("id").getAsString());out.put("effects",effects);out.put("accepted",accepted);out.put("state",e.read(OBJECT+0x430,4));out.put("last_in",e.read(OBJECT+0x40c,4));out.put("last_out",e.read(OBJECT+0x408,4));out.put("ping",e.read(OBJECT+0x410,1));out.put("message_id",e.read(OBJECT+0x404,2));out.put("length",length);out.put("length_bytes",lengthBytes);out.put("buffer",hex(e,OBJECT+4,1024));out.put("clock",clock);out.put("read",at);out.put("connected",connected?1:0);out.put("result",0);return out;
    }
    @Override public void run()throws Exception{String[] args=getScriptArgs();JsonArray cases=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson gson=new Gson();try(Z119MqttExecution e=new Z119MqttExecution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){int count=0;for(JsonElement c:cases){try{out.write(gson.toJson(one(e,c.getAsJsonObject()))+"\n");}catch(Exception failure){throw new IllegalStateException("MQTT case "+count,failure);}++count;}
        Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119MqttExecution.IMAGE_SHA);report.put("input_file_sha256",HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(Path.of(args[0])))));report.put("cases",count);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");}}
}
