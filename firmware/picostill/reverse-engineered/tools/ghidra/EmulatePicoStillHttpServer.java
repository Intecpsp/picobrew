import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.util.*;
import java.util.zip.*;
import com.google.gson.*;

public class EmulatePicoStillHttpServer extends EmulatePicoStillHttp {
    private static final long BASE=0x3ff00000L,APP=BASE+0x1000,VTABLE=BASE+0x1100,CLIENT_VTABLE=BASE+0x1200,COUNTER=0x3ffc3078L;
    private JsonObject input;private JsonArray flags,strings;private StringBuilder output;private int stringCount;
    @Override protected String string(PicoStillExecution e,long pointer)throws Exception {
        StringBuilder s=new StringBuilder();for(int offset=0;offset<65536;offset+=128){byte[] block=e.emulator.readMemory(e.address(pointer+offset),128);if(block==null)throw new IllegalStateException("Unreadable string");for(byte b:block){if(b==0)return s.toString();s.append((char)(b&255));}}
        throw new IllegalStateException("Unterminated response string");
    }
    private void put(PicoStillExecution e,long pointer,String s){e.emulator.writeMemory(e.address(pointer),(s+"\0").getBytes(StandardCharsets.ISO_8859_1));}
    private long flag(int index){return flags.get(index).getAsLong()&0xffffffffL;}
    private long textPointer(int index){return BASE+0x6000+index*512;}
    private void device(String name){events.add("device:"+name);}
    private String ip(long value){return (value&255)+"."+((value>>>8)&255)+"."+((value>>>16)&255)+"."+(value>>>24);}
    private void ipObject(PicoStillExecution e,long pointer,long value){e.write(pointer,0x3f413a90L,4);e.write(pointer+4,value,4);}
    private void decimal(double value,int digits){
        if(Double.isNaN(value)){output.append("nan");return;}if(Double.isInfinite(value)){output.append("inf");return;}
        if(value>4294967040.0||value< -4294967040.0){output.append("ovf");return;}if(value<0){output.append('-');value=-value;}
        double round=.5;for(int i=0;i<digits;i++)round/=10;value+=round;long integer=(long)value;output.append(integer);double rest=value-integer;if(digits>0)output.append('.');
        for(int i=0;i<digits;i++){rest*=10;long digit=(long)rest;output.append(digit);rest-=digit;}
    }
    private void deviceBoundary(PicoStillExecution e,int offset)throws Exception {
        switch(offset){
        case 8:events.add("device:configure:"+string(e,e.register(11))+":"+string(e,e.register(12)));return;
        case 12:device("has_ssid");e.register(10,flag(0));return;
        case 16:device("ssid");e.register(10,textPointer(1));return;
        case 20:device("has_station_ip");e.register(10,flag(1));return;
        case 24:device("station_ip");e.write(e.register(11)+4,flag(7),4);return;
        case 28:device("needs_update");e.register(10,flag(2));return;
        case 32:device("connect_to_lan");return;
        case 36:device("wifi_connected");e.register(10,flag(4));return;
        case 40:events.add("device:start:"+e.read(e.register(11),4));return;
        case 44:device("stop_program");return;
        case 48:device("is_program_running");e.register(10,flag(3));return;
        case 52:device("program_name");e.register(10,textPointer(2));return;
        case 56:device("sensor_csv");e.register(10,textPointer(3));return;
        case 60:device("fan_enabled");e.register(10,flag(6));return;
        case 64:events.add("device:fan:"+e.read(e.register(11),1));return;
        case 68:events.add("device:pump:"+e.read(e.register(11),1));return;
        case 72:device("sensors_enabled");e.register(10,flag(5));return;
        case 76:events.add("device:sensors:"+e.read(e.register(11),1));return;
        case 80:device("version");e.register(10,textPointer(0));return;
        case 84:device("hardware_error");e.register(10,textPointer(4));return;
        case 88:{device("factory_result");String raw=input.get("factory").getAsString();for(int i=0;i<raw.length()/2;i++)e.write(e.register(11)+i,Integer.parseInt(raw.substring(2*i,2*i+2),16),1);return;}
        case 96:device("factory_reset");return;
        default:throw new IllegalStateException("Unknown device method "+offset);
        }
    }
    @Override protected void boundary(PicoStillExecution e,long target)throws Exception {
        if(target>=0x7ff04000L && target<0x7ff04100L){e.countBoundary("HTTP_device_method");deviceBoundary(e,(int)(target-0x7ff04000L));return;}
        switch((int)target){
        case 0x400db4e8:e.countBoundary("WiFiServer_available");events.add("accept");e.write(e.register(10),CLIENT_VTABLE,4);return;
        case 0x400da170:super.boundary(e,0x7ff0203cL);return;
        case 0x400da0ec:e.countBoundary("Client_flush");events.add("flush");return;
        case 0x400da2d0:e.countBoundary("WiFiClient_destructor");events.add("release");return;
        case 0x7ff0201c:e.countBoundary("Client_flush");events.add("flush");return;
        case 0x400dbed4:case 0x400dbf14:{e.countBoundary("Print_string");String s=string(e,e.register(11));output.append(s);e.register(10,s.length());return;}
        case 0x400dbf54:{e.countBoundary("Print_string_line");String s=string(e,e.register(11));output.append(s).append("\r\n");e.register(10,s.length()+2);return;}
        case 0x400dbee8:{e.countBoundary("Print_String");String s=string(e,e.read(e.register(11),4));output.append(s);e.register(10,s.length());return;}
        case 0x400dbfd0:case 0x400dc050:e.countBoundary("Print_unsigned_decimal");if(e.register(12)!=10)throw new IllegalStateException("Nondecimal number");output.append(e.register(11));return;
        case 0x400dc1c8:e.countBoundary("Print_float_decimal");decimal(e.number(12),(int)e.register(14));return;
        case 0x4014a30c:e.countBoundary("Print_IPAddress");output.append(ip(e.read(e.register(11)+4,4)));return;
        case 0x400dbd78:e.countBoundary("IPAddress_default_constructor");ipObject(e,e.register(10),0);return;
        case 0x400dbdac:{e.countBoundary("IPAddress_toString");String s=ip(e.read(e.register(11)+4,4));long pointer=BASE+0x8000+64*stringCount++;put(e,pointer,s);e.write(e.register(10),pointer,4);e.write(e.register(10)+4,s.length(),4);e.write(e.register(10)+8,s.length(),4);return;}
        case 0x4014a048:e.countBoundary("String_c_str");e.register(10,e.read(e.register(10),4));return;
        case 0x400dc33c:e.countBoundary("String_destructor");return;
        case 0x400db30c:e.countBoundary("WiFi_scanComplete");device("scan_count");e.register(10,input.get("scan").getAsInt());return;
        case 0x400db434:{e.countBoundary("WiFi_SSID");int index=(int)e.register(12);events.add("device:scan_ssid:"+index);e.write(e.register(10),textPointer(5+index),4);return;}
        default:super.boundary(e,target);
        }
    }
    private Map<String,Object> server(PicoStillExecution e,JsonObject in)throws Exception {
        input=in;flags=in.getAsJsonArray("flags");strings=in.getAsJsonArray("strings");events=new ArrayList<>();output=new StringBuilder();clock=in.get("now").getAsLong();tick=in.get("tick").getAsInt();keep=true;position=0;stringCount=0;
        items=new JsonArray();for(byte b:in.get("request").getAsString().getBytes(StandardCharsets.ISO_8859_1))items.add(b&255);
        e.resetRegisters();e.clear(BASE,0x10000);e.write(COUNTER,0,4);e.write(0x3ffc4770L,0x12345678L,4);
        e.write(BASE,APP,4);e.write(BASE+0x24,5000,4);e.write(BASE+0x128,0xffffffffL,4);e.write(APP,VTABLE,4);
        for(int offset=8;offset<=0x60;offset+=4)e.write(VTABLE+offset,0x7ff04000L+offset,4);
        e.write(CLIENT_VTABLE+0x10,0x7ff02010L,4);e.write(CLIENT_VTABLE+0x14,0x7ff02014L,4);e.write(CLIENT_VTABLE+0x1c,0x7ff0201cL,4);e.write(CLIENT_VTABLE+0x3c,0x7ff0203cL,4);
        for(int i=0;i<strings.size();i++)put(e,textPointer(i),strings.get(i).getAsString());JsonArray ssids=in.getAsJsonArray("ssids");for(int i=0;i<ssids.size();i++)put(e,textPointer(5+i),ssids.get(i).getAsString());
        call(e,0x400d780cL,BASE);Map<String,Object> out=new LinkedHashMap<>();out.put("id",in.get("id").getAsString());out.put("bytes",Arrays.asList(hex(e,BASE+0x28,0x304),hex(e,BASE+0x331,48),hex(e,BASE+0x361,64)));
        long error=e.read(BASE+0x32c,4);out.put("error",error==0?"":string(e,error));out.put("output",output.toString());out.put("events",events);out.put("assertion_count",e.read(COUNTER,4));out.put("clock",clock);return out;
    }
    @Override public void run()throws Exception {
        nested.addAll(Arrays.asList(0x400d35b0L,0x400d2b74L,0x400d2bb8L,0x40149ff0L,0x4014a014L,0x4014a054L,0x400d2bf0L,0x400d4d20L,0x400d4dc8L,
            0x400d2820L,0x400d3bf8L,0x400d3c80L,0x400d38f8L,0x400d3b44L,0x400d3a18L,0x400d3d1cL,0x400d5770L,0x400d5754L,
            0x400d37d4L,0x400d37f4L,0x400d3820L,0x400d3858L,0x400d387cL,0x400d38b4L,0x400d38dcL));
        String[] args=getScriptArgs();JsonArray inputs;try(GZIPInputStream stream=new GZIPInputStream(Files.newInputStream(Path.of(args[0])))){inputs=JsonParser.parseReader(new InputStreamReader(stream,StandardCharsets.UTF_8)).getAsJsonArray();}
        Map<String,Object> report=new LinkedHashMap<>();List<Map<String,Object>> cases=new ArrayList<>();try(PicoStillExecution e=new PicoStillExecution(currentProgram,monitor,this::boundary)){
            e.instructionLimit=300000;
            e.resetRegisters();for(int i=0;i<804;i++)e.write(BASE+0x3000+i,0xa5,1);call(e,0x400d5754L,BASE+0x3000);
            for(int i=0;i<804;i++)if(e.read(BASE+0x3000+i,1)!=0)throw new IllegalStateException("Original pressure-history loop failed at byte "+i);
            Map<String,Object> probe=new LinkedHashMap<>();probe.put("original_entry","0x400d5754");probe.put("before_byte",165);probe.put("cleared_bytes",804);probe.put("backedges",e.boundaries.get("adapted_loop_backedge"));report.put("loop_control_probe",probe);
            for(JsonElement in:inputs){try{cases.add(server(e,in.getAsJsonObject()));}catch(Exception error){throw new IllegalStateException(in.getAsJsonObject().get("id").getAsString(),error);}}
            report.put("input_sha256",PicoStillExecution.IMAGE_SHA);report.put("cases_input_gzip_sha256",sha(Path.of(args[0])));report.put("cases",cases);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original full HTTP dispatch with nested original request/form parsing, response field assembly and all browser asset literals. Device callbacks, Client/Print/IPAddress/String SDK effects, clock/yield, diagnostics and ROM conversions are explicit boundaries. Numeric Print formatting uses the pinned SDK algorithm. Register windows and bounded hardware LOOP control are explicitly adapted; every loop-body instruction runs. Runtime application callback bindings are validated separately; no network hardware test.");report.put("hardware_tested",false);report.put("full_cpu_emulation",false);
        }
        try(GZIPOutputStream out=new GZIPOutputStream(Files.newOutputStream(Path.of(args[1])))){out.write((new Gson().toJson(report)+"\n").getBytes(StandardCharsets.UTF_8));}println("Executed "+cases.size()+" original PicoStill HTTP route/browser cases");
    }
}
