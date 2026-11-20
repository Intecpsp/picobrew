// Execute Z119 PicoStill orchestration at explicit client/SDK boundaries.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulatePicoStillContinuation extends GhidraScript {
    private static final long PICO=0x3ff20000L,STATE=PICO+0x2000,EEPROM=PICO+0x3000,STRINGS=PICO+0x4000,GATE=PICO+0x5000;
    private static final String[] IPS={"192.168.1.73","0.0.0.0",""};
    private static final String[] SCANS={"home-network","PicoStill_A","other","PicoStill_12345678901234567890","PicoStill_","PicoStill_Z","PicoStill_1","PicoStill_2","PicoStill_3","PicoStill_4","PicoStill_5","PicoStill_6"};
    private final List<List<Long>> events=new ArrayList<>();private long[] input;private final StillContinuationSupport diagnostics=new StillContinuationSupport();
    private String string(Z119Execution e,long p)throws Exception{if(p==0)return "";StringBuilder s=new StringBuilder();for(int i=0;i<512;i++){int c=(int)e.read(p+i,1);if(c==0)return s.toString();s.append((char)c);}throw new IllegalStateException("Unterminated original text");}
    private void string(Z119Execution e,long p,String s){e.emulator.writeMemory(e.address(p),(s+"\0").getBytes(java.nio.charset.StandardCharsets.US_ASCII));}
    private void append(List<Long> out,String text){out.add((long)text.length());for(int i=0;i<text.length();++i)out.add((long)text.charAt(i));}
    private void record(long effect,long argument,String text,String ssid,String password,long internet){
        List<Long> row=new ArrayList<>(List.of(effect,argument&0xffffffffL));append(row,text);append(row,ssid);append(row,password);row.add(internet);events.add(row);diagnostics.service(events.size()-1);
    }
    private void effect(Z119Execution e,long id,long arg){e.countBoundary("PicoStill_service_"+id);record(id,arg,"","","",0);}
    private void credentials(Z119Execution e,long p,String ssid,String password,boolean internet){
        bounded(e,p,32,ssid);bounded(e,p+0x30,64,password);e.write(p+0x80,internet?1:0,1);
    }
    private void bounded(Z119Execution e,long p,int capacity,String value){
        int n=Math.min(capacity,value.length());e.clear(p,capacity+1);string(e,p,value.substring(0,n));int aligned=(capacity+4)&~3;
        e.write(p+aligned,n,4);e.write(p+aligned+4,capacity-n,4);e.write(p+aligned+8,value.length()>capacity?1:0,1);e.write(p+aligned+9,0,1);
    }
    private void resetClient(Z119Execution e,long command){e.write(PICO+0x1148,command,4);e.write(PICO+0x1144,0,1);e.write(PICO+0x1145,0,1);e.write(PICO+0x12b4,0,1);}
    private void boundary(Z119Execution e,long target)throws Exception{
        if(diagnostics.boundary(e,target)||e.arithmetic(target))return;long p=e.register(10),arg=e.register(11);
        switch((int)target){
        case 0x401367a0:throw new IllegalStateException("Original stack guard changed");
        case 0x400014c0:e.countBoundary("ROM_strlen");e.register(10,string(e,p).length());return;
        case 0x4000c2c8:e.countBoundary("ROM_memcpy");e.emulator.writeMemory(e.address(p),e.emulator.readMemory(e.address(arg),(int)e.register(12)));return;
        case 0x4000c44c:e.countBoundary("ROM_memset");byte[] bytes=new byte[(int)e.register(12)];Arrays.fill(bytes,(byte)arg);e.emulator.writeMemory(e.address(p),bytes);return;
        case 0x40030000:effect(e,3,0);e.register(10,input[20]);return;
        case 0x40030010:effect(e,4,0);return;
        case 0x40101bc4:effect(e,0,arg);return;
        case 0x400daeb4:effect(e,1,arg);return;
        case 0x401164ac:effect(e,2,arg);return;
        case 0x400d5bc0:effect(e,5,arg);return;
        case 0x400e6b84:e.countBoundary("Wifi_connect");record(6,0,"",string(e,arg),string(e,arg+0x30),e.read(arg+0x80,1));return;
        case 0x4011a228:e.countBoundary("Still_authorize");record(7,e.register(13),string(e,e.register(12)),"","",0);e.write(PICO+0xa78,input[32],1);return;
        case 0x400f83f0:e.countBoundary("StillClient_host");record(8,0,string(e,arg),"","",0);return;
        case 0x4010c8d0:effect(e,9,arg);resetClient(e,arg);return;
        case 0x4010ded0:e.countBoundary("StillClient_configure");record(10,0,"",string(e,arg),string(e,arg+0x30),e.read(arg+0x80,1));resetClient(e,1);return;
        case 0x400f01a8:effect(e,11,0);resetClient(e,0);return;
        case 0x40123428:effect(e,12,0);e.register(10,input[22]);return;
        case 0x4012351c:
            effect(e,13,e.register(12));int index=(int)e.register(12);String ssid=input[21]==0?"no-match":input[21]==1?"PicoStill_A":SCANS[index%12];
            string(e,STRINGS+0x300,ssid);e.write(p,STRINGS+0x300,4);return;
        case 0x4012ab00:e.countBoundary("Arduino_String_c_str");e.register(10,e.read(p,4));return;
        case 0x4012a994:e.countBoundary("Arduino_String_destroy");return;
        case 0x400f4ccc:effect(e,14,e.read(arg+8,4));return;
        case 0x400fbe10:effect(e,15,0);return;
        case 0x4011a378:case 0x4011a818:case 0x4011a66c:case 0x4011a78c:case 0x400f8300:case 0x401bf918:
        case 0x400ee998:case 0x400ef180:case 0x400ef160:case 0x400f82e8:case 0x400f82cc:case 0x400f81c4:case 0x400f8190:
        case 0x400f202c:case 0x400f2010:case 0x400f1efc:case 0x40100954:case 0x40100938:case 0x40100830:case 0x401007f0:case 0x4010109c:case 0x400f4424:case 0x400d0e60:case 0x400d0e90:
        case 0x401bf278:case 0x401bf254:case 0x400d0e3c:case 0x400d0f1c:e.invoke(target);return;
        default:throw new IllegalStateException(String.format("Unmodeled PicoStill call %08x",target));
        }
    }
    private void invoke(Z119Execution e,long target,long... args)throws Exception{for(int i=0;i<args.length;i++)e.register(10+i,args[i]);e.invoke(target);}
    private Map<String,Object> runCase(Z119Execution e,JsonObject item)throws Exception{
        input=new long[33];JsonArray words=item.getAsJsonArray("words");for(int i=0;i<input.length;++i)input[i]=words.get(i).getAsLong();
        e.resetRegisters();e.clear(PICO,0x6000);events.clear();diagnostics.reset(e,input[2],item.has("counter")?item.get("counter").getAsLong():0xffffffffL,input[29]);e.write(0x3ffc4654L,0x119aabbcL,4);
        e.write(PICO+8,input[4],4);e.write(PICO+12,input[5],4);e.write(PICO+16,input[6],4);e.write(PICO+20,STRINGS,4);string(e,STRINGS,"PicoStill_");
        e.write(PICO+24,STRINGS+0x40,4);string(e,STRINGS+0x40,"12345678");e.write(PICO+0x150,STATE,4);e.write(PICO+0x1dc,EEPROM,4);
        e.write(PICO+0x214,GATE,4);e.write(GATE,GATE+0x100,4);e.write(GATE+0x100,0x40030000L,4);e.write(GATE+0x104,0x40030010L,4);
        e.write(PICO+0x133c,input[7],4);e.write(PICO+0x210,input[3],4);
        e.write(PICO+0x1148,input[8],4);e.write(PICO+0x1144,input[9],1);e.write(PICO+0x1145,input[10],1);e.write(PICO+0x12b4,input[11],1);
        e.write(PICO+0xa78,input[12],1);e.write(PICO+0x168,input[13],4);e.write(PICO+0x19c,input[14],4);
        e.write(PICO+0x30,input[15],1);e.write(PICO+0x28,input[16],4);e.write(PICO+0x2c,input[17],4);e.write(PICO+0xc8,input[18],1);e.write(PICO+0xbc,input[19],4);
        e.write(STATE+4,input[0],4);e.write(STATE+8,input[1],4);e.write(STATE+0x10,input[24],4);e.write(STATE+0x94,input[25],4);e.write(STATE+0x734,input[26],4);e.write(STATE+0x738,input[27],4);e.write(STATE+0x73c,input[28],1);
        bounded(e,STATE+0x44,64,"fixture error");bounded(e,STATE+0x5b0,16,"fixtureA");bounded(e,STATE+0x6d4,32,"fixture-version");bounded(e,STATE+0x704,32,IPS[(int)input[23]]);
        bounded(e,PICO+0x1e0,32,"PicoStill_fixtureA");credentials(e,PICO+0x12b8,"saved-home","saved-password",true);
        string(e,EEPROM+12,"configured-home");string(e,EEPROM+44,"configured-password");e.write(STATE+0x6d0,8,4);
        for(int i=0;i<8;++i)bounded(e,STATE+0x5d0+i*32,16,"fixture"+i);
        switch(item.get("op").getAsString()){
        case "advance":invoke(e,0x4011a818L,PICO,input[2]);break;
        case "transition":e.write(STRINGS+0x100,input[30],4);invoke(e,0x4011a378L,PICO,STRINGS+0x100);break;
        case "retry":invoke(e,0x4011a66cL,PICO,input[30],input[5]);break;
        case "wait":invoke(e,0x4011a78cL,PICO,input[31],input[30],input[5]);break;
        case "scan":invoke(e,0x400f8300L,PICO);break;
        case "setup":invoke(e,0x400fc080L,PICO);break;
        case "tick":invoke(e,0x4011ae0cL,PICO,input[2]);break;
        default:throw new IllegalArgumentException("Unknown PicoStill operation");
        }
        List<Long> state=new ArrayList<>(List.of(e.read(STATE+4,4),e.read(STATE+8,4),e.read(PICO+0x133c,4),e.read(PICO+0x210,4),diagnostics.assertionLine,
            e.read(PICO+0x30,1),e.read(PICO+0xc8,1),e.read(PICO+0x1148,4),e.read(PICO+0x1144,1),e.read(PICO+0x1145,1),e.read(PICO+0x12b4,1),
            e.read(PICO+0xa78,1),e.read(PICO+0x168,4),e.read(PICO+0x19c,4),e.read(STATE+0x6d0,4),e.read(STATE+0x10,4),e.read(STATE+0x94,4),e.read(STATE+0x734,4),e.read(STATE+0x738,4),e.read(STATE+0x73c,1),e.read(PICO+0x1338,1)));
        for(long p:new long[]{STATE+0x44,STATE+0x5b0,STATE+0x6d4,STATE+0x704,PICO+0x1e0,PICO+0x12b8,PICO+0x12e8})append(state,string(e,p));
        for(int i=0;i<8;++i){long p=STATE+0x5d0+i*32;append(state,string(e,p));state.add(e.read(p+28,1));}
        Map<String,Object> out=new LinkedHashMap<>();out.put("id",item.get("id").getAsString());out.put("result",0);out.put("state",state);out.put("events",new ArrayList<>(events));diagnostics.finish(out,e);return out;
    }
    private static String sha(byte[] bytes)throws Exception{StringBuilder out=new StringBuilder();for(byte b:MessageDigest.getInstance("SHA-256").digest(bytes))out.append(String.format("%02x",b&255));return out.toString();}
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=3)throw new IllegalArgumentException("Expected input JSON, output JSONL, mapped image layout");StillContinuationSupport.validateImage(currentProgram,Path.of(args[2]));
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson gson=new Gson();int count=0;
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){
            for(JsonElement value:inputs){JsonObject item=value.getAsJsonObject();try{out.write(gson.toJson(runCase(e,item))+"\n");}catch(Exception failure){throw new IllegalStateException("PicoStill scenario "+item.get("id"),failure);}count++;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);report.put("cases",count);report.put("input_file_sha256",sha(Files.readAllBytes(Path.of(args[0]))));
            report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original PicoStill setup/tick, 20-state controller, command/Wi-Fi mailbox priority, scan filtering, retries and response polling; explicit outgoing HTTP/client, cloud authorization, runtime gate, SDK scan strings, event delivery and SDK rendering boundaries; the real returning assertion helper executes counter updates and delays.");
            report.put("full_cpu_or_hardware_emulation",false);report.put("hardware_tested",false);Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }println("Executed "+count+" original PicoStill controller scenarios");
    }
}
