import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.util.*;
import java.util.zip.*;
import com.google.gson.*;
public class EmulatePicoFermBackend extends EmulatePicoFermData {
    private static final long B=0x3ff04000L,TEXT=B+0x1700,KEY=B+0x1b00,VALUE=B+0x1f00;
    private List<Long> items,connections;private int position,attempt,tick;private boolean connected;private StringBuilder sent;
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
        default:super.boundary(e,target);
        }
    }
    private Map<String,Object> backend(PicoFermExecution e,JsonObject input)throws Exception{
        e.resetRegisters();e.clear(B,0x4000);events=new ArrayList<>();now=input.get("now").getAsLong();tick=input.get("tick").getAsInt();position=0;attempt=0;sent=new StringBuilder();connected=input.get("connected").getAsInt()!=0;
        items=new ArrayList<>();for(JsonElement value:input.getAsJsonArray("items"))items.add(value.getAsLong());connections=new ArrayList<>();for(JsonElement value:input.getAsJsonArray("connect"))connections.add(value.getAsLong());
        unhex(e,B,input.get("data").getAsString());unhex(e,B+0x1000,input.get("record").getAsString());text(e,B+0x1400,"001122334455");text(e,B+0x1500,"0.2.7");text(e,B+0x1600,"12345678");
        text(e,TEXT,input.get("text").getAsString());text(e,KEY,input.get("key").getAsString());text(e,VALUE,input.get("value").getAsString());text(e,B+0x35,input.get("response").getAsString());text(e,B+0x2300,input.get("error").getAsString());
        String op=input.get("op").getAsString();switch(op){
        case "headers":call(e,0x402054f4L,B,TEXT);break;case "prefix":call(e,0x402053a8L,B,TEXT);break;case "argument":call(e,0x40205450L,B,KEY,VALUE);break;
        case "send":call(e,0x40206a28L,B,TEXT,B+0x364);break;case "fail":call(e,0x40205630L,B,TEXT);break;
        case "sync":case "sequence":call(e,0x40206b0cL,B,input.get("rate").getAsLong(),input.get("voltage").getAsLong(),input.get("logs").getAsLong());
            if(op.equals("sequence"))for(int i=0;i<input.get("polls").getAsInt();i++)call(e,0x40207878L,B);break;
        case "state":call(e,0x40207528L,B);break;case "logs":call(e,0x40207584L,B);break;case "complete":call(e,0x402076c4L,B);break;case "poll":call(e,0x40207878L,B);break;default:throw new IllegalArgumentException("Unknown backend case");}
        long error=e.read(B+0x13c,4);e.write(B+0x13c,0,4);Map<String,Object> out=new LinkedHashMap<>();out.put("id",input.get("id").getAsString());out.put("data",hex(e,B,0x454));out.put("error",error==0?"":e.string(error));out.put("sent",sent.toString());out.put("clock",now);out.put("position",position);out.put("events",events);return out;
    }
    @Override public void run()throws Exception{
        addDataFunctions();nested.addAll(Arrays.asList(0x4020534cL,0x402053a8L,0x40205420L,0x40205450L,0x40205490L,0x40205630L,0x40206a28L,0x40207528L,0x40207584L,0x402076c4L,0x40204350L,0x40105e00L));
        String[] args=getScriptArgs();JsonArray inputs;try(GZIPInputStream in=new GZIPInputStream(Files.newInputStream(Path.of(args[0])))){inputs=JsonParser.parseReader(new InputStreamReader(in,StandardCharsets.UTF_8)).getAsJsonArray();}
        List<Map<String,Object>> cases=new ArrayList<>();Map<String,Object> report=new LinkedHashMap<>();
        try(PicoFermExecution e=new PicoFermExecution(currentProgram,monitor,this::boundary)){
            for(JsonElement input:inputs){try{cases.add(backend(e,input.getAsJsonObject()));}catch(Exception ex){throw new IllegalStateException(input.getAsJsonObject().get("id").getAsString(),ex);}}
            report.put("input_sha256",PicoFermExecution.IMAGE_SHA);report.put("cases_input_gzip_sha256",sha(Path.of(args[0])));report.put("cases",cases);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original backend request buffers, four-attempt connect policy, marker parser, five-command registration/update/session/log sequence, and completion including actual assertions, session parser and SDK dtostrf. Client, SDK time/delay/yield, diagnostics, libc memory/string/itoa and ROM arithmetic are explicit boundaries. Does not contact a live server or install an update.");report.put("hardware_tested",false);
        }
        try(GZIPOutputStream out=new GZIPOutputStream(Files.newOutputStream(Path.of(args[1])))){out.write((new Gson().toJson(report)+"\n").getBytes(StandardCharsets.UTF_8));}println("Executed "+cases.size()+" original PicoFerm Backend cases");
    }
}
