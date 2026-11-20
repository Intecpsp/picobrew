// Original Z119 local HTTP parser and static route instructions.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulateLocalHttp extends GhidraScript {
    private static final long PARSER=0x3ff20000L, REQUEST=0x3ff21000L, LINE=0x3ff24000L;
    private static final long SERVER=0x3ff30000L, CLIENT=0x3ff31000L, VTABLE=0x3ff32000L, WRITE=0x7ff31000L;
    private final List<List<Long>> trace=new ArrayList<>();
    private final ByteArrayOutputStream body=new ByteArrayOutputStream();
    private static String sha(byte[] bytes)throws Exception{
        StringBuilder result=new StringBuilder();
        for(byte b:MessageDigest.getInstance("SHA-256").digest(bytes))result.append(String.format("%02x",b&255));
        return result.toString();
    }
    private String string(DisplayExecution e,long pointer)throws Exception{
        if(pointer==0)return null;
        ByteArrayOutputStream bytes=new ByteArrayOutputStream();
        for(int i=0;i<8192;i++){int value=(int)e.read(pointer+i,1);if(value==0)return bytes.toString(StandardCharsets.UTF_8);bytes.write(value);}
        throw new IllegalStateException("Unterminated original string");
    }
    private void string(DisplayExecution e,long pointer,String text){
        byte[] bytes=text.getBytes(StandardCharsets.UTF_8);
        e.emulator.writeMemory(e.address(pointer),bytes);e.write(pointer+bytes.length,0,1);
    }
    private void boundary(DisplayExecution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        switch((int)target){
            case 0x400eeffc:case 0x40109b4c:case 0x40109c34:case 0x4011b0e4:
            case 0x401bf3d4:case 0x401bf3ec:case 0x400d0e90:case 0x401bf278:
            case 0x400d0e60:case 0x401bf254:case 0x400ee998:case 0x40100830:
            case 0x401007f0:case 0x4012a76c:case 0x4012a734:e.invoke(target);return;
            case 0x4000c44c:{
                e.countBoundary("ROM_memset");byte[] bytes=new byte[(int)e.register(12)];Arrays.fill(bytes,(byte)e.register(11));
                e.emulator.writeMemory(e.address(e.register(10)),bytes);return;
            }
            case 0x4000c2c8:{
                e.countBoundary("ROM_memcpy");byte[] bytes=e.emulator.readMemory(e.address(e.register(11)),(int)e.register(12));
                e.emulator.writeMemory(e.address(e.register(10)),bytes);return;
            }
            case 0x400014c0:e.countBoundary("ROM_strlen");e.register(10,string(e,e.register(10)).getBytes(StandardCharsets.UTF_8).length);return;
            case 0x400566c4:{
                e.countBoundary("ROM_atoi_signed_32_domain");String input=string(e,e.register(10));int at=0;
                while(at<input.length()&&" \t\r\n\u000b\u000c".indexOf(input.charAt(at))>=0)at++;
                boolean negative=false;if(at<input.length()&&(input.charAt(at)=='+'||input.charAt(at)=='-'))negative=input.charAt(at++)=='-';
                long value=0;while(at<input.length()&&input.charAt(at)>='0'&&input.charAt(at)<='9')value=value*10+input.charAt(at++)-'0';
                if(negative)value=-value;
                if(value<Integer.MIN_VALUE||value>Integer.MAX_VALUE)throw new IllegalArgumentException("Outside modeled ROM atoi domain");
                e.register(10,value);return;
            }
            case 0x40081388:e.countBoundary("diagnostic_millis");e.register(10,1000);return;
            case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
            case 0x400d21cc:e.countBoundary("diagnostic_log");return;
            case 0x400db014:throw new IllegalStateException("Unexpected original assertion line "+e.register(11));
            case 0x401367a0:throw new IllegalStateException("Original stack guard failed");
            case 0x400d97c4:{
                if(e.register(10)!=SERVER||e.register(11)!=CLIENT)throw new IllegalStateException("Unexpected HTTP header receiver");
                e.countBoundary("response_headers");String type=string(e,e.register(14));
                long kind=type==null?0:type.equals("text/html")?1:type.equals("text/javascript")?2:type.equals("text/css")?3:-1;
                if(kind<0)throw new IllegalStateException("Unknown original MIME type");
                trace.add(List.of(0L,e.register(12),e.register(13),kind));return;
            }
            case 0x4011ae84:
                if(e.register(10)!=SERVER||e.register(11)!=CLIENT)throw new IllegalStateException("Unexpected state receiver");
                e.countBoundary("live_state_response");trace.add(List.of(2L,0L,0L,0L));return;
            case 0x7ff31000:{
                if(e.register(10)!=CLIENT)throw new IllegalStateException("Unexpected write receiver");
                e.countBoundary("Print_virtual_write");long size=e.register(12);
                if(size>32768)throw new IllegalStateException("Unbounded original write");
                byte[] bytes=e.emulator.readMemory(e.address(e.register(11)),(int)size);body.write(bytes);
                trace.add(List.of(1L,size,0L,0L));e.register(10,size);return;
            }
            default:throw new IllegalStateException(String.format("Unmodeled local HTTP call %08x",target));
        }
    }
    private Map<String,Object> snapshot(DisplayExecution e)throws Exception{
        List<Long> values=List.of(e.read(REQUEST,4),e.read(REQUEST+4,4),e.read(REQUEST+8,1),
            e.read(PARSER+5,1),e.read(PARSER+6,1),e.read(REQUEST+0x50,4),e.read(REQUEST+0x54,4),e.read(REQUEST+0x58,1));
        Map<String,Object> out=new LinkedHashMap<>();out.put("values",values);out.put("path",string(e,REQUEST+12));return out;
    }
    private Map<String,Object> runCase(DisplayExecution e,JsonObject input)throws Exception{
        e.resetRegisters();e.clear(PARSER,16);e.clear(REQUEST,128);e.clear(LINE,8192);e.clear(SERVER,16);e.clear(CLIENT,16);e.clear(VTABLE,32);
        trace.clear();body.reset();e.write(PARSER,REQUEST,4);
        JsonArray values=input.getAsJsonArray("initial");
        e.write(REQUEST,values.get(0).getAsLong(),4);e.write(REQUEST+4,values.get(1).getAsLong(),4);e.write(REQUEST+8,values.get(2).getAsInt(),1);
        e.write(PARSER+5,values.get(3).getAsInt(),1);e.write(PARSER+6,values.get(4).getAsInt(),1);e.write(PARSER+4,input.get("debug").getAsInt(),1);
        String path=input.get("path").getAsString();string(e,REQUEST+12,path);e.write(REQUEST+0x50,path.length(),4);e.write(REQUEST+0x54,64-path.length(),4);e.write(REQUEST+0x58,values.get(5).getAsInt(),1);
        e.write(CLIENT,VTABLE,4);e.write(VTABLE+12,WRITE,4);
        List<Map<String,Object>> snapshots=new ArrayList<>();String op=input.get("op").getAsString();
        for(JsonElement line:input.getAsJsonArray("lines")){
            e.register(10,PARSER);long target=0;
            if(op.equals("reset"))target=0x400eeffcL;
            else if(op.equals("route")){target=0x4011b0e4L;e.register(10,SERVER);e.register(11,CLIENT);e.register(12,REQUEST);}
            else{
                if(line.isJsonNull())e.register(11,0);else{string(e,LINE,line.getAsString());e.register(11,LINE);}
                target=op.equals("request")?0x40109b4cL:0x40109c34L;
            }
            e.invoke(target);snapshots.add(snapshot(e));
        }
        Map<String,Object> out=new LinkedHashMap<>();out.put("id",input.get("id").getAsString());out.put("snapshots",snapshots);
        out.put("events",new ArrayList<>(trace));out.put("body_sha256",sha(body.toByteArray()));return out;
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input JSON and output JSONL");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson gson=new Gson();int count=0;
        try(DisplayExecution e=new DisplayExecution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){
            for(JsonElement input:inputs){out.write(gson.toJson(runCase(e,input.getAsJsonObject()))+"\n");count++;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",DisplayExecution.IMAGE_SHA);report.put("cases",count);
            report.put("input_file_sha256",sha(Files.readAllBytes(Path.of(args[0]))));report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original request-line/header/reset and static-route instructions, original bounded path copying and original static-resource bytes. ROM atoi is modeled only in its signed-32-bit input domain. Response headers, live-state serialization, Print output and diagnostics are explicit services. TCP polling, POST handling and backend exchange are not executed.");
            report.put("full_cpu_or_hardware_emulation",false);report.put("hardware_tested",false);
            Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+count+" original Z local HTTP cases");
    }
}
