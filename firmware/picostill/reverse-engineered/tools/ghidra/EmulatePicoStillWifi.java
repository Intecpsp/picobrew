import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.util.*;
import java.util.zip.*;
import com.google.gson.*;

public class EmulatePicoStillWifi extends EmulatePicoStillController {
    private static final long BASE=0x3ff00000L,ARG=BASE+0x500,EVENT=0x7ff01008L,COUNTER=0x3ffc3078L;
    private JsonArray network;private int stringCount;
    private long value(int i){return network.get(i).getAsLong()&0xffffffffL;}
    private void ip(PicoStillExecution e,long pointer,long address){e.write(pointer,0x3f413a90L,4);e.write(pointer+4,address,4);}
    private String ipText(long n){return (n&255)+"."+((n>>>8)&255)+"."+((n>>>16)&255)+"."+(n>>>24);}
    private void text(PicoStillExecution e,long pointer,String value){e.emulator.writeMemory(e.address(pointer),(value+"\0").getBytes(StandardCharsets.US_ASCII));}
    @Override protected void boundary(PicoStillExecution e,long target)throws Exception {
        switch((int)target){
        case 0x400daec4:e.countBoundary("WiFi_status");events.add("status:"+value(0));e.register(10,value(0));return;
        case 0x400dac14:e.countBoundary("WiFi_mode");events.add("mode:"+e.register(10));e.register(10,1);return;
        case 0x400db384:e.countBoundary("WiFi_scanNetworks");events.add("scan:"+e.register(11)+":"+e.register(12)+":"+e.register(13)+":"+e.register(14));e.register(10,2);return;
        case 0x400db30c:e.countBoundary("WiFi_scanComplete");events.add("scan_complete:"+(int)value(1));e.register(10,value(1));return;
        case 0x400dade8:e.countBoundary("WiFi_enableAP");events.add("enable_ap:"+e.register(11));e.register(10,1);return;
        case 0x400d9c5c:e.countBoundary("WiFi_softAPConfig");events.add("ap_config:"+e.read(e.register(11)+4,4)+":"+e.read(e.register(12)+4,4)+":"+e.read(e.register(13)+4,4));e.register(10,1);return;
        case 0x400d9b0c:e.countBoundary("WiFi_softAP");events.add("start_ap:"+string(e,e.register(11))+":"+string(e,e.register(12))+":"+e.register(13)+":"+e.register(14)+":"+e.register(15));e.register(10,value(3));return;
        case 0x400da858:e.countBoundary("WiFi_waitStatusBits");events.add("wait:"+e.register(10)+":"+e.register(11));e.register(10,value(4));return;
        case 0x400d9d24:e.countBoundary("WiFi_softAPIP");events.add("ap_ip:"+value(5));ip(e,e.register(10),value(5));return;
        case 0x400d9cc4:e.countBoundary("WiFi_softAPdisconnect");events.add("disconnect_ap:"+e.register(11));e.register(10,value(10));return;
        case 0x400daee4:e.countBoundary("WiFi_begin");if(e.register(14)!=0)throw new IllegalStateException("Non-null BSSID");events.add("begin_station:"+string(e,e.register(11))+":"+string(e,e.register(12))+":"+e.register(13)+":"+e.register(15));e.register(10,value(2));return;
        case 0x400db268:e.countBoundary("WiFi_setHostname");events.add("hostname:"+string(e,e.register(11)));e.register(10,1);return;
        case 0x400db220:e.countBoundary("WiFi_localIP");events.add("local_ip:"+value(6));ip(e,e.register(10),value(6));return;
        case 0x400db244:e.countBoundary("WiFi_gatewayIP");events.add("gateway_ip:"+value(7));ip(e,e.register(10),value(7));return;
        case 0x400da8a4:e.countBoundary("WiFi_hostByName");events.add("resolve:"+string(e,e.register(10)));e.write(e.register(11)+4,value(9),4);e.register(10,value(8));return;
        case 0x400db180:e.countBoundary("WiFi_disconnect");events.add("disconnect_station:"+e.register(11)+":"+e.register(12));e.register(10,value(11));return;
        case 0x400d2a80:e.countBoundary("integer_diagnostic");events.add("integer:"+string(e,e.register(10))+":"+string(e,e.register(11))+":"+e.register(12));return;
        case 0x400dbd88:e.countBoundary("IPAddress_constructor");ip(e,e.register(10),(e.register(11)&255)|((e.register(12)&255)<<8)|((e.register(13)&255)<<16)|((e.register(14)&255)<<24));return;
        case 0x400dbdac:{e.countBoundary("IPAddress_toString");String s=ipText(e.read(e.register(11)+4,4));long pointer=BASE+0x1000+32*stringCount++;text(e,pointer,s);e.write(e.register(10),pointer,4);e.write(e.register(10)+4,s.length(),4);e.write(e.register(10)+8,s.length(),4);return;}
        case 0x4014a048:e.countBoundary("String_c_str");e.register(10,e.read(e.register(10),4));return;
        case 0x400dc33c:e.countBoundary("String_destructor");return;
        case 0x400ec248:{e.countBoundary("libc_transition_sprintf");if(!string(e,e.register(11)).equals("%s -> %s"))throw new IllegalStateException("Unexpected sprintf");String s=string(e,e.register(12))+" -> "+string(e,e.register(13));text(e,e.register(10),s);e.register(10,s.length());return;}
        case (int)EVENT:e.countBoundary("WiFi_event_sink");events.add("wifi_event:"+e.register(11)+":"+e.register(12));return;
        default:super.boundary(e,target);
        }
    }
    private Map<String,Object> wifi(PicoStillExecution e,JsonObject in)throws Exception {
        events=new ArrayList<>();network=in.getAsJsonArray("network");stringCount=0;clock=in.get("now").getAsLong();e.resetRegisters();e.clear(BASE,0x2000);e.write(COUNTER,0,4);e.write(0x3ffc4770L,0x12345678L,4);e.write(0x3ffc3124L,0,4);
        String data=in.get("data").getAsString();for(int i=0;i<data.length()/2;i++)e.write(BASE+i,Integer.parseInt(data.substring(2*i,2*i+2),16),1);
        text(e,BASE+0x300,in.get("empty_ap").getAsBoolean()?"":"PicoStill0123");text(e,BASE+0x340,"HomeSSID");text(e,BASE+0x380,"WifiPassword");text(e,BASE+0x3c0,"www.picobrew.com");
        e.write(BASE+0x800,BASE+0x900,4);e.write(BASE+0x904,EVENT,4);long arg=in.get("arg").getAsLong();e.write(ARG,arg,4);String op=in.get("op").getAsString(),label="";
        if(op.equals("state"))call(e,0x400d5fa4L,BASE,ARG);else if(op.equals("poll"))call(e,0x400d63b0L,BASE);
        else if(op.equals("fail"))call(e,0x400d638cL,BASE,arg);else if(op.equals("label")){call(e,0x400d27d8L,arg);label=string(e,e.register(10));}else throw new IllegalArgumentException("Unknown operation");
        Map<String,Object> out=new LinkedHashMap<>();out.put("id",in.get("id").getAsString());out.put("data",hex(e,BASE,0x4c));out.put("label",label);out.put("events",events);out.put("assertion_count",e.read(COUNTER,4));out.put("clock",clock);return out;
    }
    @Override public void run()throws Exception {
        nested.addAll(Arrays.asList(0x400d5fa4L,0x400d638cL,0x400d27d8L));String[] args=getScriptArgs();JsonArray inputs;
        try(GZIPInputStream stream=new GZIPInputStream(Files.newInputStream(Path.of(args[0])))){inputs=JsonParser.parseReader(new InputStreamReader(stream,StandardCharsets.UTF_8)).getAsJsonArray();}
        Map<String,Object> report=new LinkedHashMap<>();List<Map<String,Object>> cases=new ArrayList<>();
        try(PicoStillExecution e=new PicoStillExecution(currentProgram,monitor,this::boundary)){
            for(JsonElement in:inputs){try{cases.add(wifi(e,in.getAsJsonObject()));}catch(Exception error){throw new IllegalStateException(in.getAsJsonObject().get("id").getAsString(),error);}}
            report.put("input_sha256",PicoStillExecution.IMAGE_SHA);report.put("cases_input_gzip_sha256",sha(Path.of(args[0])));report.put("cases",cases);
            report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original Wi-Fi state transitions/poll/failure/status labels and actual returning assertions. Wi-Fi SDK methods, event sink, IPAddress/String diagnostic formatting, sprintf, clock/delay and serial output are explicit boundaries. Network association and physical RF are not emulated.");report.put("hardware_tested",false);report.put("full_cpu_emulation",false);
        }
        try(GZIPOutputStream out=new GZIPOutputStream(Files.newOutputStream(Path.of(args[1])))){out.write((new Gson().toJson(report)+"\n").getBytes(StandardCharsets.UTF_8));}println("Executed "+cases.size()+" original PicoStill Wi-Fi cases");
    }
}
