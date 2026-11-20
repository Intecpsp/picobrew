import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.util.*;
import java.util.zip.*;
import com.google.gson.*;

// Real request/parser/page/route instruction bodies. SDK networking, clock,
// transport writes, device effects and libc formatting are explicit boundaries.
public class EmulatePicoFermHttp extends EmulatePicoFermData {
    private static final long APP=0x3ff02000L,HTTP=APP+0x75c,REQUEST=HTTP+0x84,
        STREAM=APP+0x2000,VTABLE=APP+0x2100,DEVICEVT=APP+0x2200,PRIOR=APP+0x2300,
        MESSAGE=APP+0x2400,DEST=APP+0x3000,TEXT1=APP+0x4000,TEXT2=APP+0x5000;
    private long[] words;private byte[] wire;private int position,availableCalls;
    private StringBuilder response;private Map<Long,String> strings;
    private String fullString(PicoFermExecution e,long pointer)throws Exception {
        StringBuilder result=new StringBuilder();for(int i=0;i<16384;i++){int value=(int)e.read(pointer+i,1);if(value==0)return result.toString();result.append((char)value);}throw new IllegalStateException("Unterminated HTTP string");
    }
    private String ssid(int index){return index==0?"Home Wi-Fi":index==1?"<raw&SSID>":index==2?"":"network-"+index;}
    private void stream(PicoFermExecution e,long address){e.write(address,VTABLE,4);e.write(VTABLE+8,0x40000200L,4);e.write(VTABLE+0xc,0x40000204L,4);e.write(VTABLE+0x10,0x40000208L,4);e.write(VTABLE+0x30,0x4000020cL,4);}
    @Override protected void boundary(PicoFermExecution e,long target)throws Exception {
        switch((int)target){
        case 0x40106870:e.countBoundary("Arduino_millis_scripted");events.add("millis:"+now);e.register(2,now);now=(now+words[2])&0xffffffffL;return;
        case 0x40000200:case 0x40209a80:e.countBoundary("WiFiClient_flush");events.add("flush:"+response.length());return;
        case 0x40000204:{e.countBoundary("WiFiClient_available");availableCalls++;int value=(words[3]!=0&&availableCalls%words[3]==0)?0:wire.length-position;events.add("available:"+value);e.register(2,value);return;}
        case 0x40000208:{e.countBoundary("WiFiClient_read");int value=position<wire.length?wire[position++]&255:-1;events.add("read:"+value);e.register(2,value);return;}
        case 0x4000020c:{e.countBoundary("WiFiClient_connected");boolean value=position<words[4]&&(words[5]!=0||position<wire.length);events.add("connected:"+(value?1:0));e.register(2,value?1:0);return;}
        case 0x40000220:e.countBoundary("HttpDevice_calibration_status");events.add("calibration");e.write(e.register(3),words[12],1);e.write(e.register(3)+1,words[13],1);e.write(e.register(3)+4,MESSAGE,4);return;
        case 0x40000224:e.countBoundary("HttpDevice_factory_reset");events.add("factory_reset");return;
        case 0x40000228:e.countBoundary("HttpDevice_calibrate");events.add("calibrate");return;
        case 0x402090c0:e.countBoundary("WiFi_scanComplete");events.add("scan_count:"+(int)words[11]);e.register(2,words[11]);return;
        case 0x402091f0:{e.countBoundary("WiFi_SSID_String");int index=(int)e.register(4);events.add("scan_ssid:"+index);strings.put(e.register(2),ssid(index));return;}
        case 0x4020cb58:e.countBoundary("Arduino_String_destructor");strings.remove(e.register(2));return;
        case 0x4020de58:{e.countBoundary("Print_String");String value=strings.get(e.register(3));if(value==null)throw new IllegalStateException("Unknown String value");response.append(value);e.register(2,value.length());return;}
        case 0x4020de0c:case 0x4020c2bc:case 0x4020c2e8:case 0x4020c310:{
            e.countBoundary(target==0x4020de0cL||target==0x4020c2e8L?"Print_flash_text":"Print_RAM_text");
            String value=fullString(e,e.register(3));response.append(value);boolean newline=target==0x4020c2e8L||target==0x4020c310L;if(newline)response.append("\r\n");e.register(2,value.length()+(newline?2:0));return;}
        case 0x4020c3d8:{e.countBoundary("Print_unsigned_decimal");if(e.register(4)!=10)throw new IllegalStateException("Unexpected numeric base");String value=Long.toUnsignedString(e.register(3));response.append(value);e.register(2,value.length());return;}
        case 0x4020f3d4:{String format=e.string(e.register(3));if(!format.equals("<tr><td>#%d</td><td>%s</td><td>%s</td></tr>")){super.boundary(e,target);return;}
            e.countBoundary("libc_sprintf_reviewed_HTTP_row");String value="<tr><td>#"+(int)e.register(4)+"</td><td>"+e.string(e.register(5))+"</td><td>"+e.string(e.register(6))+"</td></tr>";text(e,e.register(2),value);e.register(2,value.length());return;}
        case 0x4020b958:e.countBoundary("DNSServer_processNextRequest");events.add("dns");return;
        case 0x40209ed0:e.countBoundary("WiFiServer_available");events.add("accept");stream(e,e.register(2));return;
        case 0x4020dbc4:e.countBoundary("WiFiClient_boolean");e.register(2,words[7]);return;
        case 0x402097e4:e.countBoundary("WiFiClient_destructor");if(words[7]!=0)events.add("release");return;
        case 0x40201a64:e.countBoundary("Arduino_delay");events.add("delay:"+e.register(2));now=(now+e.register(2))&0xffffffffL;return;
        default:super.boundary(e,target);
        }
    }
    private Map<String,Object> runHttp(PicoFermExecution e,JsonObject in)throws Exception {
        e.resetRegisters();e.clear(APP,0x7000);events=new ArrayList<>();response=new StringBuilder();strings=new HashMap<>();
        words=new long[20];for(int i=0;i<words.length;i++)words[i]=in.getAsJsonArray("words").get(i).getAsLong();now=words[1];position=0;availableCalls=0;wire=in.get("wire").getAsString().getBytes(StandardCharsets.ISO_8859_1);
        text(e,TEXT1,in.get("first").getAsString());text(e,TEXT2,in.get("second").getAsString());text(e,PRIOR,"prior error");text(e,MESSAGE,words[14]!=0?"Pressure requires calibration":"");
        unhex(e,RECORD,in.get("record").getAsString());dataPointers(e,DATA);text(e,DATA,"0.2.7");text(e,DATA+0x10,"example.test");text(e,DATA+0x30,"001122aabbcc");
        e.write(APP,DEVICEVT,4);e.write(DEVICEVT+4,0x40000220L,4);e.write(DEVICEVT+8,0x402044e8L,4);e.write(DEVICEVT+0xc,0x40000224L,4);e.write(DEVICEVT+0x10,0x40000228L,4);
        e.write(HTTP,APP,4);e.write(HTTP+4,RECORD,4);e.write(HTTP+8,DATA+0x30,4);e.write(HTTP+0xc,DATA,4);e.write(HTTP+0x10,DATA+0x10,4);e.write(HTTP+0x78,words[6],1);e.write(HTTP+0x390,words[0],4);
        text(e,REQUEST,"previous-uri");text(e,REQUEST+0x104,in.get("body").getAsString());e.write(REQUEST+0x100,words[10],4);e.write(REQUEST+0x304,words[9],1);e.write(REQUEST+0x308,words[8]!=0?PRIOR:0,4);
        text(e,HTTP+0x394,"previous-ssid");text(e,HTTP+0x3c4,"previous-pass");text(e,HTTP+0x404,"old");for(int i=0;i<128;i++)e.write(DEST+i,0xa5,1);stream(e,STREAM);
        long first=words[17]!=0?0:TEXT1,second=words[18]!=0?0:TEXT2,result=0;int op=in.get("op").getAsInt();
        switch(op){
        case 0:call(e,0x40203394L,first,second);result=e.register(2);break;
        case 1:call(e,0x402033c8L,first,second);result=e.register(2);break;
        case 2:call(e,0x4020da7cL,first,words[16]);result=e.register(2)==0?0xffffffffL:e.register(2)-TEXT1;break;
        case 3:call(e,0x402034e4L,DEST,words[15],TEXT1);break;
        case 4:call(e,0x40203410L,TEXT1,REQUEST);break;
        case 5:call(e,0x40204b2cL,HTTP+0x390,STREAM,REQUEST);break;
        case 6:call(e,0x402064ecL,HTTP,REQUEST);break;
        case 7:call(e,0x40206924L,HTTP,REQUEST,second);break;
        case 8:call(e,0x402069d4L,HTTP,STREAM,REQUEST,second);result=e.register(2);break;
        case 9:call(e,0x40205f84L,HTTP,STREAM);break;
        case 10:call(e,0x40206074L,HTTP,STREAM);break;
        case 11:call(e,0x40206284L,HTTP,STREAM);break;
        case 12:call(e,0x40206338L,HTTP,STREAM);break;
        case 13:call(e,0x402063dcL,HTTP,STREAM);break;
        case 14:call(e,0x40206608L,HTTP,STREAM,REQUEST);break;
        case 15:call(e,0x402079c0L,HTTP);break;
        case 16:call(e,0x402035d0L);break;
        case 17:call(e,0x40204cf8L);break;
        default:throw new IllegalArgumentException("Unknown HTTP case");
        }
        Map<String,Object> out=new LinkedHashMap<>();out.put("id",in.get("id").getAsString());out.put("result",result);
        out.put("uri",hex(e,REQUEST,256));out.put("body",hex(e,REQUEST+0x104,512));out.put("content_length",e.read(REQUEST+0x100,4));out.put("headers_complete",e.read(REQUEST+0x304,1));long error=e.read(REQUEST+0x308,4);out.put("error",error==0?"":e.string(error));
        out.put("form",hex(e,HTTP+0x394,112));out.put("trigger",hex(e,HTTP+0x404,10));out.put("decoded",hex(e,DEST,128));out.put("response",response.toString());out.put("record",hex(e,RECORD,236));out.put("data",hex(e,DATA,112));out.put("changed",e.read(APP+0x30,1));out.put("now",now);out.put("consumed",position);out.put("events",events);return out;
    }
    @Override public void run()throws Exception {
        addDataFunctions();nested.addAll(Arrays.asList(0x40203394L,0x402033c8L,0x40203410L,0x402034e4L,0x4020da30L,0x4020da39L,0x4020da54L,0x4020da5cL,0x4020da7cL,0x40204b2cL,0x40202edcL,0x40202f14L,0x40205f84L,0x40206074L,0x40206284L,0x40206338L,0x402063dcL,0x402064ecL,0x40206608L,0x40206924L,0x402069d4L,0x402044e8L));
        String[] args=getScriptArgs();JsonArray inputs;try(GZIPInputStream input=new GZIPInputStream(Files.newInputStream(Path.of(args[0])))){inputs=JsonParser.parseReader(new InputStreamReader(input,StandardCharsets.UTF_8)).getAsJsonArray();}
        Map<String,Object> report=new LinkedHashMap<>();List<Map<String,Object>> cases=new ArrayList<>();
        try(PicoFermExecution e=new PicoFermExecution(currentProgram,monitor,this::boundary)){
            for(JsonElement input:inputs){try{cases.add(runHttp(e,input.getAsJsonObject()));}catch(Exception ex){throw new IllegalStateException(input.getAsJsonObject().get("id").getAsString(),ex);}}
            report.put("input_sha256",PicoFermExecution.IMAGE_SHA);report.put("cases_input_gzip_sha256",sha(Path.of(args[0])));report.put("cases",cases);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Complete original HTTP string, request-line, parser, forms, trigger, page and route bodies including real returning assertions, original strtol/dtostrf, embedded tests and actual credential mutation. SDK DNS/client/Wi-Fi/String/Print/clock/delay, calibration query and final device actions are explicit boundaries. Oversized URI/body and invalid persistent log indices/overflowing OEM formatter destinations are outside successful returning comparison domain.");report.put("hardware_tested",false);
        }
        try(GZIPOutputStream output=new GZIPOutputStream(Files.newOutputStream(Path.of(args[1])))){output.write((new Gson().toJson(report)+"\n").getBytes(StandardCharsets.UTF_8));}println("Executed "+cases.size()+" original PicoFerm HTTP cases");
    }
}
