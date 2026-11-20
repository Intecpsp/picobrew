import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.util.*;
import java.util.zip.*;
import com.google.gson.*;
public class EmulatePicoFermWifi extends EmulatePicoFermHardware {
    private static final long BASE=0x3ff02000L,ARG=BASE+0x500,EVENT=0x40000200L;
    private long[] words;private int strings;
    private void ip(PicoFermExecution e,long pointer,long value){e.write(pointer,0x401072a0,4);e.write(pointer+4,value,4);}
    private String ipText(long value){return (value&255)+"."+((value>>>8)&255)+"."+((value>>>16)&255)+"."+(value>>>24);}
    private void text(PicoFermExecution e,long pointer,String value){e.emulator.writeMemory(e.address(pointer),(value+"\0").getBytes(StandardCharsets.ISO_8859_1));}
    @Override protected void boundary(PicoFermExecution e,long target)throws Exception{
        switch((int)target){
        case 0x40208d3c:e.countBoundary("WiFi_setAutoConnect");events.add("auto_connect:"+e.register(3));return;
        case 0x40208a70:e.countBoundary("WiFi_persistent");events.add("persistent:"+e.register(3));return;
        case 0x40208a78:e.countBoundary("WiFi_mode");events.add("mode:"+e.register(3));e.register(2,1);return;
        case 0x40208e30:e.countBoundary("WiFi_status");events.add("status:"+words[10]);e.register(2,words[10]);return;
        case 0x4020896c:e.countBoundary("WiFi_softAPdisconnect");events.add("disconnect_ap:"+e.register(3));e.register(2,1);return;
        case 0x40208cdc:e.countBoundary("WiFi_disconnect");events.add("disconnect_station:"+e.register(3));e.register(2,1);return;
        case 0x402090e4:e.countBoundary("WiFi_scanDelete");events.add("scan_delete");return;
        case 0x40209118:e.countBoundary("WiFi_scanNetworks");events.add("scan:"+e.register(3)+":"+e.register(4));e.register(2,0);return;
        case 0x402090c0:e.countBoundary("WiFi_scanComplete");events.add("scan_complete:"+(int)words[11]);e.register(2,words[11]);return;
        case 0x402086f8:e.countBoundary("WiFi_softAPConfig");events.add("ap_config:"+e.read(e.register(3)+4,4)+":"+e.read(e.register(4)+4,4)+":"+e.read(e.register(5)+4,4));e.register(2,1);return;
        case 0x402087bc:e.countBoundary("WiFi_softAP");events.add("ap_start:"+e.string(e.register(3))+":"+e.string(e.register(4))+":"+e.register(5)+":"+e.register(6)+":"+e.register(7));e.register(2,words[12]);return;
        case 0x40208e54:e.countBoundary("WiFi_begin");if(e.register(6)!=0)throw new IllegalStateException("Non-null BSSID");events.add("begin_station:"+e.string(e.register(3))+":"+e.string(e.register(4))+":"+e.register(5)+":"+e.register(7));e.register(2,words[13]);return;
        case 0x40208be0:e.countBoundary("WiFi_hostByName");events.add("resolve:"+e.string(e.register(3)));e.write(e.register(4)+4,words[15],4);e.register(2,words[14]);return;
        case 0x40208d6c:e.countBoundary("WiFi_localIP");events.add("local_ip:"+words[16]);ip(e,e.register(2),words[16]);return;
        case 0x40208e04:e.countBoundary("WiFi_gatewayIP");events.add("gateway_ip:"+words[17]);ip(e,e.register(2),words[17]);return;
        case 0x4020bfc0:e.countBoundary("IPAddress_default");ip(e,e.register(2),0);return;
        case 0x4020bfcc:e.countBoundary("IPAddress_constructor");ip(e,e.register(2),(e.register(3)&255)|((e.register(4)&255)<<8)|((e.register(5)&255)<<16)|((e.register(6)&255)<<24));return;
        case 0x4020bff0:{e.countBoundary("IPAddress_toString");String value=ipText(e.read(e.register(3)+4,4));long pointer=BASE+0x1000+strings++*32;text(e,pointer,value);e.write(e.register(2),pointer,4);e.write(e.register(2)+4,value.length(),4);e.write(e.register(2)+8,value.length(),4);return;}
        case 0x4020cb58:e.countBoundary("String_destructor");return;
        case (int)EVENT:e.countBoundary("WifiEvents_changed");events.add("wifi_event:"+e.register(3)+":"+e.register(4));return;
        default:super.boundary(e,target);
        }
    }
    private Map<String,Object> wifi(PicoFermExecution e,JsonObject input)throws Exception{
        e.resetRegisters();e.clear(BASE,8192);events=new ArrayList<>();strings=0;words=new long[18];
        for(int i=0;i<18;i++)words[i]=input.getAsJsonArray("words").get(i).getAsLong();now=words[9];
        e.write(BASE,words[0],4);e.write(BASE+4,BASE+0x800,4);e.write(BASE+8,EVENT,4);
        e.write(BASE+0x10,BASE+0x100,4);text(e,BASE+0x100,input.get("empty").getAsBoolean()?"":"PicoFerm_123456");
        e.write(BASE+0x14,words[4],1);e.write(BASE+0x15,words[5],1);e.write(BASE+0x18,words[6],4);
        e.write(BASE+0x1c,BASE+0x200,4);text(e,BASE+0x200,"WifiPassword");e.write(BASE+0x20,BASE+0x180,4);text(e,BASE+0x180,"HomeSSID");
        e.write(BASE+0x24,words[1],4);e.write(BASE+0x28,words[3],4);e.write(BASE+0x2c,words[2],4);
        e.write(BASE+0x30,BASE+0x300,4);text(e,BASE+0x300,input.get("error").getAsString());
        e.write(BASE+0x34,BASE+0x280,4);text(e,BASE+0x280,"www.picobrew.com");ip(e,BASE+0x38,words[7]);ip(e,BASE+0x40,words[8]);
        long argument=input.get("arg").getAsLong();e.write(ARG,argument,4);String op=input.get("op").getAsString(),label="";
        switch(op){case "state":call(e,0x40205b5cL,BASE,ARG);break;case "poll":call(e,0x40205e08L,BASE);break;
        case "fail":call(e,0x40205d78L,BASE,argument);break;case "request":call(e,0x40204ad0L,BASE+0x2c,ARG);break;
        case "label":call(e,0x40204ab4L,ARG);label=e.string(e.register(2));break;case "status":call(e,0x40202e94L,argument);label=e.string(e.register(2));break;
        case "initialize":call(e,0x40205558L,BASE);break;
        case "connect":text(e,BASE+0x380,input.get("empty").getAsBoolean()?"":"NewSSID");text(e,BASE+0x400,"NewPassword");call(e,0x402055e4L,BASE,BASE+0x380,BASE+0x400);break;
        default:throw new IllegalArgumentException("Unknown operation");}
        words[0]=e.read(BASE,4);words[1]=e.read(BASE+0x24,4);words[2]=e.read(BASE+0x2c,4);words[3]=e.read(BASE+0x28,4);words[4]=e.read(BASE+0x14,1);words[5]=e.read(BASE+0x15,1);words[6]=e.read(BASE+0x18,4);words[7]=e.read(BASE+0x3c,4);words[8]=e.read(BASE+0x44,4);
        Map<String,Object> out=new LinkedHashMap<>();out.put("id",input.get("id").getAsString());out.put("words",words);out.put("error",e.string(e.read(BASE+0x30,4)));out.put("label",label);out.put("events",events);return out;
    }
    @Override public void run()throws Exception{
        nested.addAll(Arrays.asList(0x40205b5cL,0x40205d78L,0x40204ad0L,0x40204ab4L,0x40202e94L,0x40203394L,0x4020da30L));
        String[] args=getScriptArgs();JsonArray inputs;try(GZIPInputStream in=new GZIPInputStream(Files.newInputStream(Path.of(args[0])))){inputs=JsonParser.parseReader(new InputStreamReader(in,StandardCharsets.UTF_8)).getAsJsonArray();}
        List<Map<String,Object>> cases=new ArrayList<>();Map<String,Object> report=new LinkedHashMap<>();
        try(PicoFermExecution e=new PicoFermExecution(currentProgram,monitor,this::boundary)){
            for(JsonElement in:inputs){try{cases.add(wifi(e,in.getAsJsonObject()));}catch(Exception ex){throw new IllegalStateException(in.getAsJsonObject().get("id").getAsString(),ex);}}
            report.put("input_sha256",PicoFermExecution.IMAGE_SHA);report.put("cases_input_gzip_sha256",sha(Path.of(args[0])));report.put("cases",cases);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original Wi-Fi initialize, transition, request, failure and poll plus status/state labels and actual returning assertion helpers. Wi-Fi SDK/IPAddress/String/clock/diagnostics/event sink are explicit boundaries. No physical association or RF emulation.");report.put("hardware_tested",false);
        }
        try(GZIPOutputStream out=new GZIPOutputStream(Files.newOutputStream(Path.of(args[1])))){out.write((new Gson().toJson(report)+"\n").getBytes(StandardCharsets.UTF_8));}println("Executed "+cases.size()+" original PicoFerm Wi-Fi cases");
    }
}
