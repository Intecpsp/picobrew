import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.util.*;
import java.util.zip.*;
import com.google.gson.*;
public class EmulatePicoFermApplication extends EmulatePicoFermData {
    private static final long APP=0x3fff01d8L,STORE=0x3ff20000L,STRINGS=0x3ff30000L,ARGS=0x3ff17000L;
    private List<Long> items,connections;private int position,attempt,tick,strings,updateResult;private boolean connected,dnsResult;private StringBuilder sent;private String updateError;private long[] wifiWords;private long storageSize;
    private void ip(PicoFermExecution e,long pointer,long value){e.write(pointer,0x401072a0,4);e.write(pointer+4,value,4);}
    private String ipText(long value){return (value&255)+"."+((value>>>8)&255)+"."+((value>>>16)&255)+"."+(value>>>24);}
    private void stringObject(PicoFermExecution e,long object,String value){long pointer=STRINGS+(strings++)*256;text(e,pointer,value);e.write(object,pointer,4);e.write(object+4,value.length(),4);e.write(object+8,value.length(),4);}
    @Override protected void boundary(PicoFermExecution e,long target)throws Exception{
        switch((int)target){

        case 0x40106870:e.countBoundary("Arduino_millis");events.add("millis:"+now);e.register(2,now);now=(now+tick)&0xffffffffL;return;
        case 0x40201a64:e.countBoundary("Arduino_delay");events.add("delay:"+e.register(2));now=(now+e.register(2))&0xffffffffL;return;
        case 0x402096dc:{e.countBoundary("WiFiClient_connect_IP");events.add("connect:"+e.read(e.register(3)+4,4)+":"+e.register(4));int index=Math.min(attempt++,connections.size()-1);e.register(2,connections.get(index));return;}
        case 0x4020c2bc:{e.countBoundary("Print_print_C_string");String value=e.string(e.register(3));sent.append(value);events.add("write:"+value);e.register(2,value.length());return;}
        case 0x402092d4:e.countBoundary("WiFiClient_available");events.add("available:"+(position<items.size()?1:0));e.register(2,position<items.size()?1:0);return;
        case 0x40209b28:{e.countBoundary("WiFiClient_read");long value=items.get(position++);events.add("read:"+value);e.register(2,value);return;}
        case 0x4020db94:e.countBoundary("WiFiClient_connected");events.add("connected:"+(connected?1:0));e.register(2,connected?1:0);return;
        case 0x402094b8:e.countBoundary("WiFiClient_stop");events.add("stop");return;
        
        case 0x40208d3c:e.countBoundary("WiFi_setAutoConnect");events.add("auto_connect:"+e.register(3));return;
        case 0x40208a70:e.countBoundary("WiFi_persistent");events.add("persistent:"+e.register(3));return;
        case 0x40208a78:e.countBoundary("WiFi_mode");events.add("mode:"+e.register(3));e.register(2,1);return;
        case 0x40208e30:e.countBoundary("WiFi_status");events.add("status:"+wifiWords[10]);e.register(2,wifiWords[10]);return;
        case 0x4020896c:e.countBoundary("WiFi_softAPdisconnect");events.add("disconnect_ap:"+e.register(3));e.register(2,1);return;
        case 0x40208cdc:e.countBoundary("WiFi_disconnect");events.add("disconnect_station:"+e.register(3));e.register(2,1);return;
        case 0x402090e4:e.countBoundary("WiFi_scanDelete");events.add("scan_delete");return;
        case 0x40209118:e.countBoundary("WiFi_scanNetworks");events.add("scan:"+e.register(3)+":"+e.register(4));e.register(2,0);return;
        case 0x402090c0:e.countBoundary("WiFi_scanComplete");events.add("scan_complete:"+(int)wifiWords[11]);e.register(2,wifiWords[11]);return;
        case 0x402086f8:e.countBoundary("WiFi_softAPConfig");events.add("ap_config:"+e.read(e.register(3)+4,4)+":"+e.read(e.register(4)+4,4)+":"+e.read(e.register(5)+4,4));e.register(2,1);return;
        case 0x402087bc:e.countBoundary("WiFi_softAP");events.add("ap_start:"+e.string(e.register(3))+":"+e.string(e.register(4))+":"+e.register(5)+":"+e.register(6)+":"+e.register(7));e.register(2,wifiWords[12]);return;
        case 0x40208e54:e.countBoundary("WiFi_begin");if(e.register(6)!=0)throw new IllegalStateException("Non-null BSSID");events.add("begin_station:"+e.string(e.register(3))+":"+e.string(e.register(4))+":"+e.register(5)+":"+e.register(7));e.register(2,wifiWords[13]);return;
        case 0x40208be0:e.countBoundary("WiFi_hostByName");events.add("resolve:"+e.string(e.register(3)));e.write(e.register(4)+4,wifiWords[15],4);e.register(2,wifiWords[14]);return;
        case 0x40208d6c:e.countBoundary("WiFi_localIP");events.add("local_ip:"+wifiWords[16]);ip(e,e.register(2),wifiWords[16]);return;
        case 0x40208e04:e.countBoundary("WiFi_gatewayIP");events.add("gateway_ip:"+wifiWords[17]);ip(e,e.register(2),wifiWords[17]);return;
        case 0x4020bfc0:e.countBoundary("IPAddress_default");ip(e,e.register(2),0);return;
        case 0x4020bfcc:e.countBoundary("IPAddress_constructor");ip(e,e.register(2),(e.register(3)&255)|((e.register(4)&255)<<8)|((e.register(5)&255)<<16)|((e.register(6)&255)<<24));return;
        case 0x4020bff0:{e.countBoundary("IPAddress_toString");String value=ipText(e.read(e.register(3)+4,4));long pointer=STRINGS+strings++*32;text(e,pointer,value);e.write(e.register(2),pointer,4);e.write(e.register(2)+4,value.length(),4);e.write(e.register(2)+8,value.length(),4);return;}
        
        
        case 0x4020cc6c:e.countBoundary("String_constructor");stringObject(e,e.register(2),e.string(e.register(3)));return;
        case 0x4020cb58:e.countBoundary("String_destructor");return;
        case 0x4020d074:e.countBoundary("String_equals");e.register(2,e.string(e.read(e.register(2),4)).equals(e.string(e.register(3)))?1:0);return;
        case 0x4020c310:e.countBoundary("Serial_println");events.add("serial:"+e.string(e.register(3)));return;
        case 0x4020bf00:e.countBoundary("Serial_begin");events.add("serial_begin:"+e.register(3)+":"+e.register(4)+":"+e.register(5)+":"+e.register(6));return;
        case 0x40208d94:e.countBoundary("WiFi_macAddress");events.add("mac");for(int i=0;i<6;i++)e.write(e.register(3)+i,i*17,1);e.register(2,e.register(3));return;
        case 0x4020b580:e.countBoundary("EEPROM_begin");events.add("storage_begin:"+e.register(3));return;
        case 0x4020dd48:e.countBoundary("EEPROM_read");e.register(2,e.register(3)<storageSize?e.read(STORE+e.register(3),1):0);return;
        case 0x40202de0:{e.countBoundary("EEPROM_dirty_buffer_and_flash_commit");events.add("persist:"+hex(e,e.register(2),236));if(storageSize>=236)e.emulator.writeMemory(e.address(STORE),e.emulator.readMemory(e.address(e.register(2)),236));return;}
        case 0x40201b58:e.countBoundary("Arduino_pinMode");events.add("pinmode:"+e.register(2)+":"+e.register(3));return;
        case 0x4020b690:e.countBoundary("DNSServer_setTTL");events.add("dns_ttl:"+e.read(e.register(3),4));return;
        case 0x4020dd64:e.countBoundary("DNSServer_setReplyCode");events.add("dns_reply:"+e.read(e.register(3),4));return;
        case 0x4020b7c0:e.countBoundary("DNSServer_start");events.add("dns_start:"+e.read(e.register(3),2)+":"+e.string(e.read(e.register(4),4))+":"+e.read(e.register(5)+4,4));e.register(2,dnsResult?1:0);return;
        case 0x4020b824:e.countBoundary("DNSServer_stop");events.add("dns_stop");return;
        case 0x40209e0c:e.countBoundary("WiFiServer_begin");events.add("http_start");return;
        case 0x402079c0:e.countBoundary("HttpServer_poll_separately_replayed");events.add("web_poll");return;
        case 0x4020ba4c:e.countBoundary("Esp_restart");events.add("restart");return;
        case 0x40234a00:e.countBoundary("SDK_system_restore");events.add("remove_os_wifi");return;
        case 0x4020ba20:e.countBoundary("Esp_deepSleep");events.add("sleep:"+Long.toUnsignedString(e.register(4)|(e.register(5)<<32))+":"+e.register(6));return;
        case 0x40208680:e.countBoundary("ESPhttpUpdate_update");events.add("update:"+e.string(e.read(e.register(3),4))+":"+e.string(e.read(e.register(4),4)));e.register(2,updateResult);return;
        case 0x402080a0:e.countBoundary("ESPhttpUpdate_getLastErrorString");events.add("update_error");stringObject(e,e.register(2),updateError);return;
        default:super.boundary(e,target);
        }
    }
    private Map<String,Object> application(PicoFermExecution e,JsonObject in)throws Exception{
        e.resetRegisters();e.clear(APP,0x1000);e.clear(STORE,0x1000);e.clear(STRINGS,0x10000);e.clear(ARGS,0x1000);events=new ArrayList<>();strings=0;
        unhex(e,APP,in.get("data").getAsString());unhex(e,STORE,in.get("storage").getAsString());storageSize=in.get("storage_size").getAsLong();e.write(0x3fff0de0L,STORE,4);e.write(0x3fff0de4L,storageSize,4);e.write(0x3fff0d48L,1,1);
        now=in.get("now").getAsLong();tick=in.get("tick").getAsInt();analog=in.get("analog").getAsLong();gpio=in.get("gpio").getAsLong();readIndex=0;position=0;attempt=0;sent=new StringBuilder();connected=in.get("connected").getAsInt()!=0;dnsResult=in.get("dns").getAsInt()!=0;updateResult=in.get("update").getAsInt();updateError=in.get("update_error").getAsString();
        reads=new ArrayList<>();for(JsonElement x:in.getAsJsonArray("reads"))reads.add(x.getAsLong());items=new ArrayList<>();for(JsonElement x:in.getAsJsonArray("items"))items.add(x.getAsLong());connections=new ArrayList<>();for(JsonElement x:in.getAsJsonArray("connect"))connections.add(x.getAsLong());wifiWords=new long[18];for(int i=0;i<18;i++)wifiWords[i]=in.getAsJsonArray("wifi").get(i).getAsLong();
        text(e,0x3ff18000L,in.get("wifi_error").getAsString());text(e,0x3ff18200L,in.get("backend_error").getAsString());if(e.read(APP+0x324,4)!=0)e.write(APP+0x324,0x3ff18200L,4);
        long arg=in.get("arg").getAsLong(),arg2=in.get("arg2").getAsLong(),result=0;String label="";String operation=in.get("op").getAsString();
        switch(operation){
        case "transition":call(e,0x40206bf8L,APP,arg);break;case "timeout":call(e,0x40204d78L,APP,now,arg);result=e.register(2);break;
        
        case "info":call(e,0x4020526cL,APP);break;case "restart":call(e,0x40203088L);break;case "sleep":call(e,0x402030e4L,arg);break;
        case "factory_reset":call(e,0x4020385cL,APP);break;case "calibrate":call(e,0x40202fe4L,APP);break;
        case "calibration":call(e,0x402039ccL,APP,ARGS);result=e.read(ARGS,1)|(e.read(ARGS+1,1)<<8);label=e.string(e.read(ARGS+4,4));break;
        case "raw_storage":call(e,0x4020484cL,APP+0x710);break;case "load_storage":call(e,0x40203b30L,APP+0xc0);break;case "persist":call(e,0x402050b4L,APP);break;
        case "led_callback":call(e,0x40203014L,APP);break;case "button_callback":call(e,0x40203038L,APP);break;
        case "wifi_callback":call(e,0x40205a38L,APP,arg,arg2);break;
        case "update":call(e,0x402057a8L,APP);result=e.register(2);break;case "handle_sync":call(e,0x4020597cL,APP);break;case "console":call(e,0x402070f0L,APP);break;
        case "poll":for(int i=0;i<in.get("polls").getAsInt();i++)call(e,0x40207ba8L,APP);break;case "loop":call(e,0x40207fe8L);break;
        case "setup":call(e,0x40206e80L,APP);break;case "begin":call(e,0x40206ffcL);break;case "self_tests":call(e,0x4020686cL);break;
        default:throw new IllegalStateException("Unknown operation "+operation);
        }
        // TwoWire SDK base storage includes an OEM uninitialized timeout-start word.
        // Compare owned state; preserve the SDK object as an explicit boundary.
        e.clear(APP+0x1b4,16);
        String wifiError=e.string(e.read(APP+0x744,4));e.write(APP+0x744,0,4);long backendError=e.read(APP+0x324,4);e.write(APP+0x324,0,4);
        Map<String,Object> out=new LinkedHashMap<>();out.put("id",in.get("id").getAsString());out.put("data",hex(e,APP,0xaf0));out.put("storage",hex(e,STORE,512));out.put("wifi_error",wifiError);out.put("backend_error",backendError==0?"":e.string(backendError));out.put("sent",sent.toString());out.put("clock",now);out.put("position",position);out.put("result",result);out.put("label",label);out.put("update_reboot",e.read(0x3fff0d48L,1));out.put("events",events);return out;
    }
    @Override public void run()throws Exception{
        addDataFunctions();
        for(long address:new long[]{0x402029acL,0x402029e8L,0x40202a28L,0x40202a7cL,0x40202b70L,0x40202ba8L,0x40202becL,0x40202c14L,0x40202c90L,0x40202ad0L,0x40202b34L,0x40202b00L,0x40202a68L,0x40202abcL,0x40202cd8L,0x40202d30L,0x402056a4L,0x4020565cL,0x402142d4L,0x402143d0L,0x40214288L,0x40214844L,0x40105e90L,0x40105ec0L,
        0x40205b5cL,0x40205d78L,0x40204ad0L,0x40204ab4L,0x40202e94L,0x40203394L,0x4020da30L,0x40205e08L,0x402055e4L,0x40205558L,
        0x4020534cL,0x402053a8L,0x40205420L,0x40205450L,0x40205490L,0x402054f4L,0x40205630L,0x40206a28L,0x40207528L,0x40207584L,0x402076c4L,0x40204350L,0x40105e00L,0x40206b0cL,0x40207878L,
        0x40202db0L,0x40202fe4L,0x40203014L,0x40203038L,0x40203060L,0x40203088L,0x402030e4L,0x4020319cL,0x40203210L,0x402033c8L,0x4020385cL,0x402039ccL,0x40203b30L,0x40203bbcL,0x40204440L,0x40204480L,0x40204578L,0x4020484cL,0x40204d78L,0x40205114L,0x4020526cL,0x402057a8L,0x402058acL,0x4020597cL,0x40205a38L,0x40206bf8L,0x40206e80L,0x402070f0L,0x40207ba8L,
        0x40203410L,0x402034e4L,0x4020da39L,0x4020da54L,0x4020da5cL,0x4020da7cL,0x402035d0L,0x40204680L,0x40204cf8L,0x40206758L,0x40204058L,0x402049acL,0x40204208L,0x4020686cL,0x40203de0L,0x40105d6cL,0x40205f40L,0x40206720L,0x402066d4L,0x401067acL})nested.add(address);
        String[] args=getScriptArgs();JsonArray inputs;try(GZIPInputStream in=new GZIPInputStream(Files.newInputStream(Path.of(args[0])))){inputs=JsonParser.parseReader(new InputStreamReader(in,StandardCharsets.UTF_8)).getAsJsonArray();}
        List<Map<String,Object>> cases=new ArrayList<>();Map<String,Object> report=new LinkedHashMap<>();
        try(PicoFermExecution e=new PicoFermExecution(currentProgram,monitor,this::boundary)){
            for(JsonElement input:inputs){try{cases.add(application(e,input.getAsJsonObject()));}catch(Exception ex){throw new IllegalStateException(input.getAsJsonObject().get("id").getAsString(),ex);}}
            report.put("input_sha256",PicoFermExecution.IMAGE_SHA);report.put("cases_input_gzip_sha256",sha(Path.of(args[0])));report.put("cases",cases);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original application setup, state transitions, scheduler, callbacks, queued console, updater and embedded vectors. Actual ADC/Wi-Fi/backend/helper instruction bodies and returning assertions execute. SDK EEPROM/GPIO/I2C/clock/network/update/serial/DNS plus separately replayed inbound HTTP poll are explicit boundaries. No live update, RF or hardware qualification.");report.put("hardware_tested",false);
        }
        try(GZIPOutputStream out=new GZIPOutputStream(Files.newOutputStream(Path.of(args[1])))){out.write((new Gson().toJson(report)+"\n").getBytes(StandardCharsets.UTF_8));}println("Executed "+cases.size()+" original PicoFerm Application cases");
    }
}
