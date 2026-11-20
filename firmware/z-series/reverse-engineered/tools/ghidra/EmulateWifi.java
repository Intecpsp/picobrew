// Execute original Wifi.h and backoff instructions at explicit SDK boundaries.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulateWifi extends GhidraScript {
    private static final long WIFI=0x3ff20000L,STATE=WIFI+0x1000,QUEUE=WIFI+0x2000,CREDS=WIFI+0x3000,STRINGS=WIFI+0x4000;
    private static final String[] SSIDS={"","fixture-network","other-network","12345678901234567890123456789012"};
    private static final String[] PASSWORDS={"","fixture-password","other-password","1234567890123456789012345678901234567890123456789012345678901234"};
    private static final String[] NAMES={"ZSeries","",null,"FixtureZ"};
    private final List<List<Long>> events=new ArrayList<>();
    private long[] input;private long assertion;
    private String string(Z119Execution e,long p)throws Exception{
        if(p==0)return null;StringBuilder s=new StringBuilder();for(int i=0;i<256;i++){int c=(int)e.read(p+i,1);if(c==0)return s.toString();s.append((char)c);}throw new IllegalStateException("Unterminated fixture text");
    }
    private void string(Z119Execution e,long p,String s){e.emulator.writeMemory(e.address(p),(s+"\0").getBytes(java.nio.charset.StandardCharsets.US_ASCII));}
    private long code(String s,String[] table){for(int i=0;i<table.length;i++)if(Objects.equals(s,table[i]))return i;return 0xffffffffL;}
    private void record(long type,long a,long b,long c,long d,long text,long password,long credentials){events.add(List.of(type&0xffffffffL,a&0xffffffffL,b&0xffffffffL,c&0xffffffffL,d&0xffffffffL,text&0xffffffffL,password&0xffffffffL,credentials&0xffffffffL));}
    private void effect(Z119Execution e,long type,long a){e.countBoundary("Wifi_SDK_or_event_"+type);record(type,a,0,0,0,0,0,0);}
    private void boundary(Z119Execution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        long p=e.register(10),arg=e.register(11);
        switch((int)target){
        case 0x40081388:e.countBoundary("clock_millis");e.register(10,input[4]);return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:case 0x400da6a4:case 0x400d9f90:e.countBoundary("diagnostic_output");return;
        case 0x400db014:e.countBoundary("returning_assertion");assertion=arg;return;
        case 0x401367a0:throw new IllegalStateException("Original stack guard changed");
        case 0x400d0a00:e.countBoundary("diagnostic_status_name");e.register(10,STRINGS+0x300);return;
        case 0x4000c2c8:e.countBoundary("ROM_memcpy");e.emulator.writeMemory(e.address(p),e.emulator.readMemory(e.address(arg),(int)e.register(12)));return;
        case 0x4000c44c:e.countBoundary("ROM_memset");byte[] bytes=new byte[(int)e.register(12)];Arrays.fill(bytes,(byte)arg);e.emulator.writeMemory(e.address(p),bytes);return;
        case 0x400f220c:case 0x400f2738:case 0x400d5bc0:case 0x401bf884:case 0x400d5b74:
        case 0x400d782c:case 0x400d77c4:case 0x400e1820:case 0x400e6b84:case 0x400ef214:
        case 0x400ef180:case 0x400ee998:case 0x400d0f1c:case 0x400d0e60:case 0x401bf254:e.invoke(target);return;
        case 0x40122e80:effect(e,0,0);e.register(10,input[17]);return;
        case 0x40122680:effect(e,1,0);e.register(10,input[18]);return;
        case 0x40123120:effect(e,2,arg);e.register(10,input[25]);return;
        case 0x40122b98:effect(e,3,p);e.register(10,input[25]);return;
        case 0x40123484:e.countBoundary("Wifi_scanNetworks");record(4,arg,e.register(12),e.register(13),e.register(14),0,0,0);e.register(10,input[25]);return;
        case 0x40123428:effect(e,5,0);e.register(10,input[19]);return;
        case 0x4012197c:
            e.countBoundary("Wifi_softAP");record(6,e.register(13),e.register(14),e.register(15),0,code(string(e,arg),NAMES),"12345678".equals(string(e,e.register(12)))?1:0xffffffffL,0);e.register(10,input[25]);return;
        case 0x40121ae0:e.countBoundary("Wifi_softAPConfig");record(7,e.read(arg+4,4),e.read(e.register(12)+4,4),e.read(e.register(13)+4,4),0,0,0,0);e.register(10,input[25]);return;
        case 0x40122ecc:
            e.countBoundary("Wifi_begin");record(8,e.register(13),e.register(14),e.register(15),0,0,0,
                100*code(string(e,arg),SSIDS)+10*code(string(e,e.register(12)),PASSWORDS)+e.read(STATE+0x8c,1));e.register(10,input[25]);return;
        case 0x40123378:e.countBoundary("Wifi_setHostname");record(9,0,0,0,0,code(string(e,arg),NAMES),0,0);e.register(10,input[25]);return;
        case 0x401231dc:effect(e,10,0);e.write(p+4,input[21],4);return;
        case 0x40123218:effect(e,11,0);e.write(p+4,input[22],4);return;
        case 0x4012333c:effect(e,12,0);e.register(10,input[20]);return;
        case 0x401226c8:e.countBoundary("Wifi_hostByName");record(13,0,0,0,0,"www.picobrew.com".equals(string(e,p))?1:0xffffffffL,0,0);e.write(arg+4,input[23],4);e.register(10,input[24]);return;
        case 0x40122664:effect(e,14,arg);return;
        case 0x401bfbb0:effect(e,15,arg);e.register(10,0);return;
        case 0x40123200:effect(e,16,0);for(int i=0;i<6;i++)e.write(arg+i,i+10,1);e.register(10,arg);return;
        case 0x400f6894:effect(e,17,0);return;
        case 0x400f20c8:e.countBoundary("Wifi_event_sink");record(18,e.read(arg+4,4),e.read(arg+8,4),e.read(arg+12,4),0,0,0,0);return;
        case 0x4012a5d8:
            e.countBoundary("SDK_IPAddress_octets");e.write(p,0x3f44a8b8L,4);e.write(p+4,arg|(e.register(12)<<8)|(e.register(13)<<16)|(e.register(14)<<24),4);return;
        default:throw new IllegalStateException(String.format("Unmodeled Wifi call %08x",target));
        }
    }
    private void credentials(Z119Execution e,long p,int s,int pass,int internet){
        e.clear(p,0x84);string(e,p,SSIDS[s]);e.write(p+0x24,SSIDS[s].length(),4);e.write(p+0x28,32,4);
        string(e,p+0x30,PASSWORDS[pass]);e.write(p+0x74,PASSWORDS[pass].length(),4);e.write(p+0x78,64,4);e.write(p+0x80,internet,1);
    }
    private void invoke(Z119Execution e,long target,long... args)throws Exception{for(int i=0;i<args.length;i++)e.register(10+i,args[i]);e.invoke(target);}
    private Map<String,Object> runCase(Z119Execution e,JsonObject item)throws Exception{
        input=new long[39];JsonArray words=item.getAsJsonArray("words");for(int i=0;i<input.length;i++)input[i]=words.get(i).getAsLong();
        e.resetRegisters();e.clear(WIFI,0x5000);events.clear();assertion=input[32];e.write(0x3ffc4654L,0x119aabbc,4);e.write(0x3ffc2ea0L,0,4);
        e.write(WIFI+4,input[14],4);e.write(WIFI+8,input[15],4);e.write(WIFI+12,input[16],4);e.write(WIFI+16,QUEUE,4);e.write(WIFI+20,STATE,4);
        e.write(WIFI+24,input[34]==2?0:STRINGS,4);if(input[34]!=2)string(e,STRINGS,NAMES[(int)input[34]]);
        string(e,STRINGS+0x100,"www.picobrew.com");e.write(WIFI+0x4c,STRINGS+0x100,4);string(e,STRINGS+0x300,"WL_STATUS_FIXTURE");
        e.write(WIFI+0x1c,input[7],1);e.write(WIFI+0x20,input[2],4);e.write(WIFI+0x24,input[1],4);
        e.write(WIFI+0x2c,input[8],1);for(int i=0;i<5;i++)e.write(WIFI+0x30+i*4,input[9+i],4);
        e.write(STATE,input[0],4);e.write(STATE+4,input[5],4);e.write(STATE+8,input[6],4);
        credentials(e,STATE+12,(int)input[26],(int)input[27],(int)input[28]);credentials(e,STATE+0x90,(int)input[29],(int)input[30],(int)input[31]);
        credentials(e,CREDS,(int)input[26],(int)input[27],(int)input[28]);
        e.write(STATE+0x118,0x0104a8c0,4);e.write(STATE+0x120,input[36],4);e.write(STATE+0x124,input[35],1);e.write(STATE+0x12c,input[37],4);e.write(STATE+0x134,input[38],4);
        switch(item.get("op").getAsString()){
        case "tick":invoke(e,0x400f27e0L,WIFI,input[3]);break;
        case "transition":e.write(CREDS+0x100,input[33],4);invoke(e,0x400f220cL,WIFI,CREDS+0x100);break;
        case "fail":invoke(e,0x400f2738L,WIFI,input[3],input[33]);break;
        case "target":invoke(e,0x400d5bc0L,WIFI,input[33]);break;
        case "connect":invoke(e,0x400e6b84L,WIFI,CREDS);break;
        case "setup":invoke(e,0x400f6ae8L,WIFI);break;
        case "timer_start":invoke(e,0x400d782cL,WIFI+0x28,input[3]);break;
        case "timer_advance":invoke(e,0x400d77c4L,WIFI+0x28,input[3]);break;
        default:throw new IllegalArgumentException("Unknown Wifi operation");
        }
        List<Long> state=new ArrayList<>(List.of(e.read(STATE,4),e.read(WIFI+0x24,4),e.read(WIFI+0x20,4),e.read(STATE+4,4),e.read(STATE+8,4),e.read(WIFI+0x1c,1),e.read(WIFI+0x2c,1)));
        for(int i=0;i<5;i++)state.add(e.read(WIFI+0x30+i*4,4));state.add(assertion);
        for(long p:new long[]{STATE+12,STATE+0x90}){state.add(code(string(e,p),SSIDS));state.add(code(string(e,p+0x30),PASSWORDS));state.add(e.read(p+0x80,1));}
        state.add(e.read(STATE+0x124,1));state.add(e.read(STATE+0x120,4));state.add(e.read(STATE+0x12c,4));state.add(e.read(STATE+0x134,4));
        for(int i=0;i<6;i++)state.add(e.read(WIFI+0x44+i,1));
        Map<String,Object> out=new LinkedHashMap<>();out.put("id",item.get("id").getAsString());out.put("result",0);out.put("state",state);out.put("events",new ArrayList<>(events));return out;
    }
    private static String sha(byte[] bytes)throws Exception{StringBuilder out=new StringBuilder();for(byte b:MessageDigest.getInstance("SHA-256").digest(bytes))out.append(String.format("%02x",b&255));return out.toString();}
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input JSON and output JSONL");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson gson=new Gson();int count=0;
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){
            for(JsonElement value:inputs){JsonObject item=value.getAsJsonObject();try{out.write(gson.toJson(runCase(e,item))+"\n");}catch(Exception failure){throw new IllegalStateException("Wifi scenario "+item.get("id"),failure);}count++;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);report.put("cases",count);
            report.put("input_file_sha256",sha(Files.readAllBytes(Path.of(args[0]))));report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original Wifi state transitions, tick, target/credential selection, failure/backoff and setup ordering; explicit Arduino radio/DNS/IPAddress, event-delivery, diagnostic and console-registration boundaries. No radio, RTOS, hardware or complete network backend execution.");
            report.put("full_cpu_or_hardware_emulation",false);report.put("hardware_tested",false);
            Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }println("Executed "+count+" original Wifi manager scenarios");
    }
}
