import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.util.*;
import java.util.zip.*;
import com.google.gson.*;
public class EmulatePicoStillBackend extends EmulatePicoStillHttp {
    private static final long BASE=0x3ff00000L,TEXT=BASE+0xa00,KEY=BASE+0xd00,VALUE=BASE+0xe00,ARG=BASE+0xf00,COUNTER=0x3ffc3078L;
    private boolean connect,connected;private StringBuilder sent;
    private void text(PicoStillExecution e,long pointer,String value){e.emulator.writeMemory(e.address(pointer),(value+"\0").getBytes(StandardCharsets.ISO_8859_1));}
    @Override protected void boundary(PicoStillExecution e,long target)throws Exception {
        if(nested.contains(target)){super.boundary(e,target);return;}
        switch((int)target){
        case 0x400da620:e.countBoundary("WiFiClient_connect_IP");events.add("connect:"+e.read(e.register(11)+4,4)+":"+e.register(12));e.register(10,connect?1:0);return;
        case 0x400dbf14:{e.countBoundary("Client_print_C_string");String text=string(e,e.register(11));sent.append(text);events.add("write:"+text);e.register(10,text.length());return;}
        case 0x400da090:e.countBoundary("Client_available");events.add("available:"+(position<items.size()?1:0));e.register(10,position<items.size()?1:0);return;
        case 0x400da170:e.countBoundary("Client_connected");events.add("connected:"+(connected?1:0));e.register(10,connected?1:0);return;
        case 0x4014a1f8:{e.countBoundary("Client_read");long value=items.get(position++).getAsLong()&0xffffffffL;events.add("read:"+value);e.register(10,value);return;}
        case 0x400566b4:e.countBoundary("ROM_itoa");text(e,e.register(11),Integer.toString((int)e.register(10)));e.register(10,e.register(11));return;
        default:super.boundary(e,target);
        }
    }
    private Map<String,Object> backend(PicoStillExecution e,JsonObject in)throws Exception{
        e.resetRegisters();e.clear(BASE,0x4000);e.write(COUNTER,0,4);e.write(0x3ffc4770L,0x12345678L,4);events=new ArrayList<>();clock=in.get("now").getAsLong();tick=in.get("tick").getAsInt();position=0;items=in.getAsJsonArray("items");connect=in.get("connect").getAsInt()!=0;connected=in.get("connected").getAsInt()!=0;sent=new StringBuilder();
        String raw=in.get("data").getAsString();for(int i=0;i<raw.length()/2;i++)e.write(BASE+i,Integer.parseInt(raw.substring(2*i,2*i+2),16),1);
        text(e,BASE+0x800,in.get("uid").getAsString());text(e,BASE+0x900,in.get("value").getAsString());text(e,TEXT,in.get("action").getAsString());text(e,KEY,in.get("key").getAsString());text(e,VALUE,in.get("value").getAsString());text(e,BASE+0x45,in.get("response").getAsString());e.write(ARG,in.get("command").getAsLong(),4);
        String op=in.get("op").getAsString();switch(op){
        case "send":call(e,0x400d8770L,BASE,TEXT,KEY,VALUE);break;
        case "sync":call(e,0x400d8834L,BASE,ARG,TEXT,KEY,VALUE);break;
        case "complete":call(e,0x400d4534L,BASE);break;
        case "poll":call(e,0x400d76e0L,BASE);break;
        case "sequence":call(e,0x400d8834L,BASE,ARG,TEXT,KEY,VALUE);call(e,0x400d76e0L,BASE);break;
        default:throw new IllegalArgumentException(op);}
        long error=e.read(BASE+0x14c,4);e.write(BASE+0x14c,0,4);
        Map<String,Object> row=new LinkedHashMap<>();row.put("id",in.get("id").getAsString());row.put("data",hex(e,BASE,0x440));row.put("error",error==0?"":string(e,error));row.put("sent",sent.toString());row.put("events",events);row.put("assertion_count",e.read(COUNTER,4));row.put("clock",clock);return row;
    }
    @Override public void run()throws Exception{
        nested.addAll(Arrays.asList(0x400d44ccL,0x400d8834L,0x400d8770L,0x400d5b5cL,0x400d5b00L,0x400d5b40L,0x400d5f88L,0x400d4534L));String[] args=getScriptArgs();JsonArray inputs;
        try(GZIPInputStream stream=new GZIPInputStream(Files.newInputStream(Path.of(args[0])))){inputs=JsonParser.parseReader(new InputStreamReader(stream,StandardCharsets.UTF_8)).getAsJsonArray();}
        Map<String,Object> report=new LinkedHashMap<>();List<Map<String,Object>> cases=new ArrayList<>();try(PicoStillExecution e=new PicoStillExecution(currentProgram,monitor,this::boundary)){
            e.instructionLimit=200000;for(JsonElement input:inputs){try{cases.add(backend(e,input.getAsJsonObject()));}catch(Exception error){throw new IllegalStateException(input.getAsJsonObject().get("id").getAsString(),error);}}
            report.put("input_sha256",PicoStillExecution.IMAGE_SHA);report.put("cases_input_gzip_sha256",sha(Path.of(args[0])));report.put("cases",cases);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);report.put("scope","Original PicoStill backend request composition, sync lifecycle, marker-response parser and completion bodies. Client connect/read/write, clock/yield, ROM memory/string/numeric conversion and diagnostics are boundaries. No live network or flashing qualification.");report.put("full_cpu_emulation",false);report.put("hardware_tested",false);
        }
        try(GZIPOutputStream out=new GZIPOutputStream(Files.newOutputStream(Path.of(args[1])))){out.write((new Gson().toJson(report)+"\n").getBytes(StandardCharsets.UTF_8));}println("Executed "+cases.size()+" original PicoStill backend cases");
    }
}
