// Full original backend JSON reader/writer bodies; ROM/compiler/log boundaries only.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.io.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulateBackendJson extends GhidraScript {
    private static final long OBJECT=0x3ff30000L,CORE=0x3ff34000L,DATA=0x3ff37000L,TEXT=0x3ff38000L,STATE=0x3ff3a000L,EVENT=0x3ff3b000L,LOCATIONS=0x3ff3d000L;
    private final Set<Long> original=new HashSet<>(Arrays.asList(0x400fa400L,0x400efe1cL,0x4010c614L,0x4010c4dcL,0x400e1180L,0x401bfa48L,0x400d0f80L,0x400d0ffcL,0x400d1040L,0x400d115cL,0x400d7888L,0x400dade8L,0x400e1250L,0x400ed990L,0x400ee998L,0x400eed28L,0x400eed44L,0x400eefb4L,0x400ef180L,0x400ef214L,0x400ef230L,0x400ee998L,0x400f1efcL,0x400f2010L,0x400f8190L,0x400f81c4L,0x400f82ccL,0x401007f0L,0x40100830L,0x40100938L,0x40100954L,0x400f9310L,0x400d1184L,0x400f04fcL,0x40102120L,0x4010ca44L,0x40110e9cL,0x401108e8L,0x400fafd4L,0x4011b1ecL,0x4011b250L,0x400f0240L,0x400f0268L,0x400d1508L,0x4011d8ccL,0x401bf7c0L,0x401bfab4L,0x400d0e60L,0x400d147cL,0x400d151cL,0x400d1530L,0x400d154cL,0x400d1574L,
        0x400d1590L,0x400d15a8L,0x400d15ccL,0x400d15f0L,0x400d1638L,0x400d165cL,0x400d585cL,0x400d598cL,
        0x400dee30L,0x400defa4L,0x400ef118L,0x400ef160L,0x400ef180L,0x400f005cL,0x400f02a4L,0x400f02b8L,0x400f202cL,
        0x400f50f4L,0x400f82ccL,0x400f82e8L,0x400f88c4L,0x400f9348L,0x400f944cL,0x400fa1f8L,0x400fa8d0L,
        0x40100938L,0x40100954L,0x401bf254L,0x401bf3d4L,0x401bf40cL,0x401bf484L,
        0x401bf62cL,0x401bf678L,0x401bf68cL,0x401bf6b4L,0x401bf6d8L,0x401bf6ecL,0x401bf708L,0x401bf724L,
        0x401bf79cL,0x401bf7acL,0x401bf9dcL,0x401bf9f8L,0x401bfa6cL));
    private List<Long> assertions,delays,diagnostics;
    private List<Object> consoleEntries;private String consoleName;private List<Object> effects;private JsonObject input;
    private void event(String name,Object value){effects.add(List.of(name,value));}
    private final Set<Long> diagnosticLines=Set.of(0xedL,0x191L,0x1b2L,0x11cL,0xafL,0x3cbL,0x3cdL,0x397L,0x399L,0x3bfL,0x31aL,0x32aL,0x350L,0xf4L);
    private long clock;private long queued,queuedSource;private long commandId;private Map<String,Object> commandData;
    private static final class TerminalAbort extends Exception {}
    private void text(Z119Execution e,long p,String text){e.emulator.writeMemory(e.address(p),(text+"\0").getBytes(StandardCharsets.UTF_8));}
    private String text(Z119Execution e,long p)throws Exception{if(p==0)return null;ByteArrayOutputStream out=new ByteArrayOutputStream();for(int i=0;i<20000;i++){int c=(int)e.read(p+i,1);if(c==0)return out.toString(StandardCharsets.UTF_8);out.write(c);}throw new IllegalStateException("Unterminated");}
    private void boundary(Z119Execution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        switch((int)target){
        case 0x400f4fec:case 0x400f5084:case 0x4011225c:e.countBoundary("Shell_factory");consoleName=text(e,e.register(11));e.write(e.register(10),STATE,4);return;
        case 0x400ff44c:case 0x400f7d74:case 0x4011213c:e.countBoundary("Shell_ownership_move");e.write(e.register(10),e.read(e.register(11),4),4);e.write(e.register(11),0,4);return;
        case 0x400fec00:case 0x400f7d2c:case 0x401108d0:case 0x401bf3c0:e.countBoundary("Cxx_wrapper_destruction");return;
        case 0x400f6b18:case 0x400f77d4:case 0x40112160:e.countBoundary("Shell_registration");consoleEntries.add(List.of(e.register(11),text(e,e.register(12)),text(e,e.register(13))));return;
        case 0x401c09a8:e.countBoundary("mbedtls_md_init");e.clear(e.register(10),12);return;
        case 0x4014b2bc:e.countBoundary("mbedtls_sha256_metadata");e.register(10,DATA+0x400);return;
        case 0x4014b328:e.countBoundary("mbedtls_md_setup");e.register(10,0);return;
        case 0x4014b63c:e.countBoundary("mbedtls_hmac_sha256_fixture");event("hmac_input",text(e,e.register(13)));for(int i=0;i<32;i++)e.write(e.register(15)+i,(i*17+number(input,"seed",0))&255,1);e.register(10,input.has("hmac_fail")&&input.get("hmac_fail").getAsBoolean()?-7:0);return;
        case 0x4014b2f4:e.countBoundary("mbedtls_md_free");return;
        case 0x40149a94:e.countBoundary("mbedtls_base64_encode");String b64=Base64.getEncoder().encodeToString(e.emulator.readMemory(e.address(e.register(13)),(int)e.register(14)));text(e,e.register(10),b64);e.write(e.register(12),b64.length(),4);e.register(10,0);return;
        case 0x401367a0:e.invoke(target);return;
        case 0x40007d54:e.countBoundary("ROM_stack_failure_message");return;
        case 0x4008d7b4:e.countBoundary("SDK_abort_terminal_boundary");throw new TerminalAbort();
        case 0x401c0088:case 0x4012a784:e.countBoundary("Serial_message_diagnostic");return;
        case 0x4012716c:e.countBoundary("MQTT_publish");event("publish",text(e,e.register(12)));e.register(10,1);return;
        case 0x400f3d3c:
            e.countBoundary("CoreCommand_single_slot_mailbox");if(commandId!=0)return;
            long pointer=e.register(11);commandId=e.read(pointer+4,4);
            if(commandId==46){long session=pointer+0xb0;commandData.put("name",text(e,session+0x14));commandData.put("type",e.read(session+4,4));commandData.put("recipe",e.read(session+0x10,4));commandData.put("program",e.read(session+12,4));commandData.put("parameters",parameterOutput(e,session+0x90));commandData.put("group",e.read(session+0xe8,1));}
            else{long recipe=pointer+0x1e8;commandData.put("id",e.read(recipe,4));commandData.put("name",text(e,recipe+4));commandData.put("kind",e.read(recipe+0x54,4));commandData.put("abv",e.read(recipe+0x5c,4));commandData.put("ibu",e.read(recipe+0x60,4));}return;
        case 0x400db014:assertions.add(e.register(11));e.invoke(target);return;
        case 0x40081388:e.countBoundary("clock");e.register(10,clock);return;
        case 0x401289a0:e.countBoundary("delay");delays.add(e.register(10));clock=(clock+e.register(10))&0xffffffffL;return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:e.countBoundary("diagnostic_output");if(diagnosticLines.contains(e.register(15))&&text(e,e.register(10)).contains("%s:%u"))event("diagnostic",e.register(15));return;
        case 0x400ed68c:e.countBoundary("HttpClient_network_availability");event("network",0);e.register(10,input.has("network")&&input.get("network").getAsBoolean()?1:0);return;
        case 0x4010c7ec:e.countBoundary("HttpClient_configure");event("configure",text(e,e.register(12)));e.register(10,!input.has("configured")||input.get("configured").getAsBoolean()?1:0);return;
        case 0x40110e2c:e.countBoundary("HttpClient_authorization_binding_without_credential_capture");event("authorization",0);return;
        case 0x400f90d8:e.countBoundary("HttpClient_GET");event("GET",0);e.register(10,!input.has("requested")||input.get("requested").getAsBoolean()?1:0);return;
        case 0x400f912c:case 0x400f9238:e.countBoundary(target==0x400f912cL?"HttpClient_POST":"HttpClient_PUT");event(target==0x400f912cL?"POST":"PUT",text(e,e.register(12)));e.register(10,!input.has("requested")||input.get("requested").getAsBoolean()?1:0);return;
        case 0x40101bc4:e.countBoundary("HttpClient_tick");event("tick",e.register(11));return;
        case 0x400f02e4:e.countBoundary("CoreEvent_single_slot_mailbox");if(queued==0){queued=e.read(e.register(11)+4,4);queuedSource=e.read(e.register(11)+0xdec,4);}return;
        case 0x400566b4:e.countBoundary("ROM_itoa_decimal");text(e,e.register(11),Integer.toString((int)e.register(10)));return;
        case 0x400014c0:e.countBoundary("ROM_strlen");e.register(10,text(e,e.register(10)).getBytes(StandardCharsets.UTF_8).length);return;
        case 0x40001274:e.countBoundary("ROM_strcmp");e.register(10,text(e,e.register(10)).compareTo(text(e,e.register(11))));return;
        case 0x4000c44c:e.countBoundary("ROM_memset");byte[] fill=new byte[(int)e.register(12)];Arrays.fill(fill,(byte)e.register(11));e.emulator.writeMemory(e.address(e.register(10)),fill);return;
        case 0x4000c2c8:e.countBoundary("ROM_memcpy");if(e.register(12)==0)return;e.emulator.writeMemory(e.address(e.register(10)),e.emulator.readMemory(e.address(e.register(11)),(int)e.register(12)));return;
        case 0x400015d4:e.countBoundary("ROM_strncpy");byte[] src=text(e,e.register(11)).getBytes(StandardCharsets.UTF_8),dest=new byte[(int)e.register(12)];System.arraycopy(src,0,dest,0,Math.min(src.length,dest.length));e.emulator.writeMemory(e.address(e.register(10)),dest);return;
        case 0x4012fecc:e.countBoundary("CXX_static_guard_acquire");e.register(10,e.read(e.register(10),1)==0?1:0);return;
        case 0x4012ff60:e.countBoundary("CXX_static_guard_release");e.write(e.register(10),1,1);return;
        case 0x4012d42c:e.countBoundary("__fixunsdfsi");double f=e.number(10);e.register(10,Double.isNaN(f)||f>=4294967296.0?0xffffffffL:f<=0?0:(long)f);return;
        case 0x401c01d8:e.countBoundary("__floatundidf");e.number(10,new java.math.BigInteger(Long.toUnsignedString(e.register(10)|(e.register(11)<<32))).doubleValue());return;
        case 0x4013cd98:
            e.countBoundary("snprintf_decimal");String format=text(e,e.register(12));String value;if(format.equals("%d"))value=Integer.toString((int)e.register(13));else if(format.equals("HTTP %d"))value="HTTP "+Integer.toString((int)e.register(13));else if(format.equals("%%%X"))value=String.format("%%%X",e.register(13));else throw new IllegalStateException("format "+format);text(e,e.register(10),value.substring(0,Math.min(value.length(),(int)e.register(11)-1)));e.register(10,value.length());return;
        default:
            if(original.contains(target)||(target>=0x401118e8L&&target<=0x401199e4L)){e.invoke(target);return;}
            throw new IllegalStateException(String.format("Unreviewed JSON call %08x",target));
        }
    }
    private Map<String,Object> parameterOutput(Z119Execution e,long pointer)throws Exception{Map<String,Object> out=new LinkedHashMap<>();out.put("id",e.read(pointer,4));out.put("abv",e.read(pointer+4,4));out.put("ibu",e.read(pointer+8,4));int offset=16;
        for(String name:List.of("duration","water","intensity","temperature")){out.put(name,List.of(String.format("%016x",e.read(pointer+offset,8)),e.read(pointer+offset+8,4)));offset+=16;}return out;}
    private long number(JsonObject o,String name,long fallback){return o.has(name)?o.get(name).getAsLong():fallback;}
    private void parameters(Z119Execution e,long p,JsonObject in){e.write(p,number(in,"program",0),4);e.write(p+4,number(in,"abv",-1),4);e.write(p+8,number(in,"ibu",-1),4);
        int offset=16;for(String name:List.of("duration","water","intensity","temperature")){JsonArray q=in.getAsJsonArray(name);e.writeDouble(p+offset,q==null?0:q.get(0).getAsDouble());e.write(p+offset+8,q==null?8:q.get(1).getAsLong(),4);offset+=16;}}
    private void string(Z119Execution e,long p,JsonObject in,String key,String fallback){text(e,p,in.has(key)?in.get(key).getAsString():fallback);}
    private Map<String,Object> one(Z119Execution e,JsonObject in)throws Exception{
        input=in;effects=new ArrayList<>();consoleEntries=new ArrayList<>();consoleName="";queued=0;queuedSource=0;commandId=0;commandData=new LinkedHashMap<>();e.resetRegisters();e.clear(0x3ff2f000L,0x11000);e.clear(0x3ffc1018L,0x58);clock=1000;assertions=new ArrayList<>();delays=new ArrayList<>();diagnostics=new ArrayList<>();
        e.write(0x3ffc2e84L,0,4);e.write(0x3ffc4654L,number(in,"guard",0x5a5a5a5aL),4);String op=in.get("op").getAsString();Map<String,Object> result=new LinkedHashMap<>();result.put("id",in.get("id").getAsString());
        if(op.equals("session")||op.equals("z_create")||op.equals("z_end")){
            e.write(DATA,number(in,"session",0),4);e.write(DATA+4,number(in,"type",0),4);e.write(DATA+12,number(in,"program",0),4);e.write(DATA+16,number(in,"recipe",-1),4);
            string(e,DATA+20,in,"name","Session");e.write(DATA+0x70,number(in,"seconds",0),4);e.writeDouble(DATA+0x78,in.has("pressure")?in.get("pressure").getAsDouble():0);
            e.writeDouble(DATA+0x80,in.has("maximum")?in.get("maximum").getAsDouble():0);e.write(DATA+0x88,number(in,"added",0),4);e.write(DATA+0xe8,number(in,"group",0),1);
            parameters(e,DATA+0x90,in);string(e,CORE+0x26b8,in,"guid","guid");string(e,CORE+0xef0,in,"uid","still");string(e,CORE+0x1014,in,"version","1.0");
            if(op.equals("session")){e.register(10,TEXT);e.register(11,number(in,"capacity",1024));e.register(12,DATA);e.register(13,CORE);e.invoke(0x401199e4L);result.put("json",text(e,TEXT));}
            else{e.write(OBJECT+8,CORE,4);e.write(OBJECT+0x858,TEXT,4);string(e,TEXT,in,"token","fixture-token");e.write(OBJECT+0x85c,number(in,"state",0),4);e.register(10,OBJECT);e.register(11,DATA);e.invoke(op.equals("z_create")?0x40119c44L:0x40119ce4L);result.put("effects",effects);result.put("state",e.read(OBJECT+0x85c,4));result.put("uri",text(e,OBJECT+0x864));result.put("truncated",e.read(OBJECT+0x8f0,1));}
        }else if(op.equals("created")){
            e.write(OBJECT+0x85c,number(in,"state",1),4);e.write(OBJECT+0x1104,number(in,"created",91),4);string(e,OBJECT+0x8f4,in,"json","");
            e.register(10,OBJECT);e.invoke(0x40117364L);result.put("state",e.read(OBJECT+0x85c,4));result.put("created",e.read(OBJECT+0x1104,4));result.put("completed",e.read(OBJECT+0x860,1));result.put("frozen",e.read(OBJECT+0xd01,1));
        }else if(op.equals("http_parse")||op.equals("http_start")||op.equals("http_tick")){
            long state=number(in,"state",2);e.write(STATE+0x178,number(in,"backend_error",0),4);e.write(STATE+0x19c,0xffffffffL,4);e.write(STATE+0x1a0,16,4);e.write(STATE+0x1a4,0xffffffffL,4);
            e.write(EVENT+0xdec,number(in,"source",3),4);string(e,LOCATIONS,in,"uri","http://local/recipe");e.write(LOCATIONS+0x84,text(e,LOCATIONS).length(),4);e.write(LOCATIONS+0x88,128-text(e,LOCATIONS).length(),4);
            if(op.equals("http_parse")){
                string(e,TEXT,in,"json","");e.write(TEXT+0x1004,text(e,TEXT).getBytes(StandardCharsets.UTF_8).length,4);
                e.register(10,TEXT);e.register(11,number(in,"source",3));e.register(12,EVENT);e.register(13,state==2?STATE:LOCATIONS);
                e.invoke(state==2?0x40117dacL:state==8||state==16?0x40118060L:0x40118218L);result.put("accepted",e.register(10)!=0);result.put("frozen",e.read(TEXT+0x100d,1));
                result.put("event",e.read(EVENT+4,4));result.put("source",e.read(EVENT+0xdec,4));long count=e.read(EVENT+0xdd0,4);result.put("count",count);result.put("offset",e.read(EVENT+0xdd4,4));result.put("total",e.read(EVENT+0xdd8,4));result.put("overflow",e.read(EVENT+0xddc,1));
                List<Object> recipes=new ArrayList<>();for(int i=0;i<count;i++){long p=EVENT+0x10+i*0xb0;Map<String,Object> r=new LinkedHashMap<>();r.put("id",e.read(p,4));r.put("kind",e.read(p+0x54,4));r.put("name",text(e,p+4));r.put("length",e.read(p+0x48,4));r.put("truncated",e.read(p+0x50,1));r.put("abv",e.read(p+0x5c,4));r.put("ibu",e.read(p+0x60,4));recipes.add(r);}result.put("recipes",recipes);result.put("local_uri",text(e,LOCATIONS));result.put("local_guid",text(e,LOCATIONS+0x90));
            }else{
                e.write(OBJECT+0x1c,op.equals("http_tick")?state:state-1,4);e.write(OBJECT+0x10,EVENT,4);e.write(OBJECT+0xc,CORE,4);e.write(OBJECT+0x18,TEXT,4);string(e,TEXT,in,"token","fixture-token");e.write(OBJECT+0x190c,STATE,4);e.write(OBJECT+0x1910,TEXT+0x100,4);text(e,TEXT+0x100,"0.0.119");
                e.write(OBJECT+0x1a34,DATA,4);e.write(DATA,2,4);e.write(OBJECT+0x1a30,LOCATIONS,4);e.write(OBJECT+0x1928,(state==10||(op.equals("http_tick")&&state==9))?0:number(in,"recipe",42),4);
                e.write(OBJECT+0x1918,number(in,"reference",0),4);e.write(OBJECT+0x191c,number(in,"offset",0),4);e.write(OBJECT+0x1920,TEXT+0x200,4);string(e,TEXT+0x200,in,"search","");
                e.register(10,OBJECT);if(op.equals("http_tick")){e.write(OBJECT+0xd0,number(in,"client_state",0),4);e.write(OBJECT+0xdc,number(in,"client_error",0),4);e.write(OBJECT+0x468,number(in,"status",503),4);string(e,OBJECT+0x8fc,in,"json","{}");e.write(OBJECT+0x8fc+0x1004,text(e,OBJECT+0x8fc).getBytes(StandardCharsets.UTF_8).length,4);e.register(11,number(in,"now",100));e.invoke(0x4011c30cL);result.put("queued",queued);result.put("queued_source",queuedSource);}else{e.register(11,state);e.invoke(0x4011b250L);}result.put("state",e.read(OBJECT+0x1c,4));result.put("uri",text(e,OBJECT+0x20));result.put("effects",effects);
            }
            Map<String,Object> b=new LinkedHashMap<>();b.put("registered",e.read(STATE+0x145,1));b.put("ready",e.read(STATE+1,1));b.put("failed",e.read(STATE+2,1));b.put("expired",e.read(STATE+0x144,1));b.put("error",e.read(STATE+0x178,4));b.put("text",text(e,STATE+0x148));b.put("token",text(e,STATE+0x124));b.put("update",text(e,STATE+4));b.put("program_uri",text(e,STATE+0x94));b.put("alias",text(e,STATE+0x17c));b.put("dirty",e.read(STATE+0x19c,4));b.put("last",e.read(STATE+0x1a0,4));b.put("resume",e.read(STATE+0x1a4,4));result.put("backend",b);
        }else if(op.equals("z_endpoint")||op.equals("z_sync")||op.equals("z_tick")||op.equals("z_log")){
            e.write(OBJECT+0x85c,number(in,"state",0),4);e.write(OBJECT+0x2c,number(in,"client_state",0),4);e.write(OBJECT+0x38,number(in,"client_error",0),4);e.register(10,OBJECT);
            if(op.equals("z_endpoint")){string(e,TEXT,in,"type","ZSession");string(e,TEXT+0x400,in,"token","fixture-token");e.register(11,TEXT);e.register(12,TEXT+0x400);e.register(13,number(in,"session",-1));try{e.invoke(0x40111984L);}catch(TerminalAbort terminal){result.put("terminal","SDK_abort_after_stack_canary_corruption");}}
            else if(op.equals("z_sync")){e.invoke(0x400f04fcL);result.put("ready",e.register(10)!=0);}
            else if(op.equals("z_tick")){e.register(11,number(in,"now",100));e.invoke(0x40102120L);}
            else{e.write(OBJECT+8,CORE,4);e.write(OBJECT+0x858,TEXT,4);string(e,TEXT,in,"token","fixture-token");e.write(DATA,number(in,"session",42),4);string(e,TEXT+0x400,in,"step","Mash");e.writeDouble(CORE+0x548,in.has("thermoblock")?in.get("thermoblock").getAsDouble():98);e.writeDouble(CORE+0x558,in.has("wort")?in.get("wort").getAsDouble():65);e.writeDouble(CORE+0x578,in.has("ambient")?in.get("ambient").getAsDouble():22);e.writeDouble(CORE+0x568,in.has("drain")?in.get("drain").getAsDouble():30);e.writeDouble(CORE+0x2b0,in.has("target")?in.get("target").getAsDouble():67);e.write(CORE+0x2e3,number(in,"valve",3),1);e.write(CORE+0x2e1,number(in,"pumps",0x30),1);e.write(CORE+0x2fc,number(in,"pause",0),4);e.write(CORE+0x80c,number(in,"rssi",-65),4);e.write(CORE+0x3a0,number(in,"remaining",123),4);e.write(OBJECT+0x840,number(in,"send",31),4);e.write(OBJECT+0x844,number(in,"wait",42),4);e.write(OBJECT+0x848,number(in,"recv",53),4);e.register(10,OBJECT);e.register(11,DATA);e.register(12,TEXT+0x400);e.invoke(0x4011c4f4L);}
            result.put("state",e.read(OBJECT+0x85c,4));result.put("uri",text(e,OBJECT+0x864));result.put("truncated",e.read(OBJECT+0x8f0,1));result.put("effects",effects);
        }else if(op.equals("backend_console")){
            long[] functions={0x400f7dc8L,0x401122a0L,0x40112378L,0x400ff6e8L};e.register(10,OBJECT);e.invoke(functions[(int)number(in,"kind",0)]);result.put("name",consoleName);result.put("entries",consoleEntries);result.put("effects",effects);
        }else if(op.equals("azure_sas")){
            string(e,TEXT,in,"host","picobrew.azure-devices.net");string(e,TEXT+0x200,in,"device","fixture-device");string(e,TEXT+0x400,in,"expiry","2524636800");e.write(OBJECT+0x570,TEXT,4);e.write(OBJECT+0x458,DATA+0x100,4);e.write(OBJECT+0x45c,4,4);e.write(DATA+0x100,0x04030201,4);e.register(10,OBJECT);e.register(11,OBJECT+0x460);e.register(12,TEXT+0x200);e.register(13,TEXT+0x400);e.invoke(0x4010dcbcL);result.put("password",text(e,OBJECT+0x460));result.put("truncated",e.read(OBJECT+0x460+0x10c,1));result.put("effects",effects);
        }else if(op.equals("azure_receive")){
            e.write(OBJECT+0x724,STATE,4);e.write(STATE,number(in,"state",2),4);e.write(OBJECT+0x728,STATE+4,4);e.write(STATE+4,number(in,"core",11),4);e.write(OBJECT+0x72c,STATE+8,4);e.write(STATE+8+0x70,number(in,"disabled",0),4);
            e.write(OBJECT+0x730,number(in,"changed",0),4);e.write(OBJECT+0x734,number(in,"deadline",0),4);string(e,TEXT,in,"json","");e.register(10,OBJECT);e.register(11,0);e.register(12,TEXT);e.register(13,text(e,TEXT).getBytes(StandardCharsets.UTF_8).length);e.invoke(0x401179a8L);
            result.put("state",e.read(STATE,4));result.put("changed",e.read(OBJECT+0x730,4));result.put("deadline",e.read(OBJECT+0x734,4));result.put("command",commandId);result.put("data",commandData);
            List<Object> filtered=new ArrayList<>();for(Object effect:effects){if(((List<?>)effect).get(0).equals("publish"))filtered.add(effect);}result.put("effects",filtered);
        }else throw new IllegalArgumentException(op);
        result.put("assertions",assertions);result.put("delays",delays);result.put("counter",e.read(0x3ffc2e84L,4));return result;
    }
    @Override public void run()throws Exception{String[] args=getScriptArgs();JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson gson=new Gson();
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter output=Files.newBufferedWriter(Path.of(args[1]))){int count=0;
            for(JsonElement input:inputs){try{output.write(gson.toJson(one(e,input.getAsJsonObject()))+"\n");}catch(Exception failure){throw new IllegalStateException("Backend JSON case "+count,failure);}++count;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);report.put("cases",count);
            report.put("input_file_sha256",HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(Path.of(args[0])))));
            report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
    }
}
