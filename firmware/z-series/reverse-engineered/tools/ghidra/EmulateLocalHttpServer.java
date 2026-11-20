// Execute original incoming HTTP lifecycle, framing and response-header bodies.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulateLocalHttpServer extends GhidraScript {
    private static final long SERVER=0x3ff30000L, CLIENT=SERVER+0x2c, REQUEST=SERVER+0x86c,
        PARSER=SERVER+0x8c8, VTABLE=0x3ff34000L, MIME=0x3ff35000L,
        WRITE=0x7ff31000L, BYTE=0x7ff31004L;
    private final ByteArrayOutputStream events=new ByteArrayOutputStream(),output=new ByteArrayOutputStream(),posted=new ByteArrayOutputStream();
    private boolean connected,accepted,headerMode;
    private JsonArray input;
    private int position,eventCount;
    private static String sha(byte[] bytes)throws Exception{
        return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));
    }
    private void event(long... values){
        for(int i=0;i<4;i++){long value=i<values.length?values[i]:0;for(int n=0;n<4;n++)events.write((int)(value>>>(8*n))&255);}eventCount++;
    }
    private byte[] bytes(DisplayExecution e,long pointer,int limit)throws Exception{
        if(pointer==0)return new byte[0];ByteArrayOutputStream out=new ByteArrayOutputStream();
        for(int i=0;i<limit;i++){int value=(int)e.read(pointer+i,1);if(value==0)return out.toByteArray();out.write(value);}
        throw new IllegalStateException("Unterminated original string");
    }
    private String text(DisplayExecution e,long pointer)throws Exception{
        return pointer==0?null:new String(bytes(e,pointer,8192),StandardCharsets.UTF_8);
    }
    private void text(DisplayExecution e,long pointer,String value){
        byte[] bytes=value.getBytes(StandardCharsets.UTF_8);e.emulator.writeMemory(e.address(pointer),bytes);e.write(pointer+bytes.length,0,1);
    }
    private void boundary(DisplayExecution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        switch((int)target){
        case 0x400d5c54:case 0x4011fc58:case 0x400f50d0:case 0x400fce40:
        case 0x400eeffc:case 0x40109b4c:case 0x40109c34:case 0x4011b0e4:
        case 0x401bf3d4:case 0x401bf3ec:case 0x400d0e90:case 0x401bf278:
        case 0x400d0e60:case 0x401bf254:case 0x400ee998:case 0x40100830:
        case 0x401007f0:case 0x4012a76c:case 0x4012a734:case 0x4012a758:
        case 0x4012a8cc:case 0x4012a880:case 0x4012a7c4:case 0x401c0088:
            e.invoke(target);return;
        case 0x4000c44c:{
            e.countBoundary("ROM_memset");byte[] data=new byte[(int)e.register(12)];Arrays.fill(data,(byte)e.register(11));
            e.emulator.writeMemory(e.address(e.register(10)),data);return;
        }
        case 0x4000c2c8:{
            e.countBoundary("ROM_memcpy");byte[] data=e.emulator.readMemory(e.address(e.register(11)),(int)e.register(12));
            e.emulator.writeMemory(e.address(e.register(10)),data);return;
        }
        case 0x400014c0:e.countBoundary("ROM_strlen");e.register(10,bytes(e,e.register(10),8192).length);return;
        case 0x400566c4:{
            e.countBoundary("ROM_atoi_signed_32_domain");String value=text(e,e.register(10));int at=0;
            while(at<value.length()&&" \t\r\n\u000b\u000c".indexOf(value.charAt(at))>=0)at++;
            boolean negative=false;if(at<value.length()&&(value.charAt(at)=='+'||value.charAt(at)=='-'))negative=value.charAt(at++)=='-';
            long n=0;while(at<value.length()&&value.charAt(at)>='0'&&value.charAt(at)<='9')n=n*10+value.charAt(at++)-'0';
            if(negative)n=-n;if(n<Integer.MIN_VALUE||n>Integer.MAX_VALUE)throw new IllegalStateException("Outside modeled atoi domain");e.register(10,n);return;
        }
        case 0x40081388:e.countBoundary("diagnostic_millis");e.register(10,1000);return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:e.countBoundary("diagnostic_log");return;
        case 0x400db014:throw new IllegalStateException("Unexpected original assertion line "+e.register(11));
        case 0x401367a0:throw new IllegalStateException("Original stack guard failed");
        case 0x401231dc:e.countBoundary("diagnostic_local_ip");e.clear(e.register(10),8);return;
        case 0x40122068:e.countBoundary("WiFiClient_connected");event(1,connected?1:0);e.register(10,connected?1:0);return;
        case 0x40123684:e.countBoundary("WiFiServer_available");event(2,accepted?1:0);return;
        case 0x40122424:e.countBoundary("WiFiClient_assign_accept_result");connected=accepted;return;
        case 0x40122160:e.countBoundary("WiFiClient_temporary_destructor");return;
        case 0x40121c48:e.countBoundary("WiFiClient_available");event(3,input.size()-position);e.register(10,input.size()-position);return;
        case 0x401bfb0c:{
            e.countBoundary("Stream_read");long value=input.get(position++).getAsLong();event(4,value);e.register(10,value);return;
        }
        case 0x40122120:e.countBoundary("WiFiClient_stop");event(5);connected=false;return;
        case 0x40123748:e.countBoundary("WiFiServer_end");event(6);return;
        case 0x401235fc:e.countBoundary("WiFiServer_begin");event(7);return;
        case 0x400d97c4:{
            if(headerMode){e.invoke(target);return;}
            if(e.register(10)!=SERVER||e.register(11)!=CLIENT)throw new IllegalStateException("Unexpected header receiver");
            e.countBoundary("response_header_service");String type=text(e,e.register(14));
            long kind=type==null?0:type.equals("text/html")?1:type.equals("text/javascript")?2:type.equals("text/css")?3:4;
            event(8,e.register(12),e.register(13),kind);return;
        }
        case 0x401172b8:
            if(e.register(10)!=SERVER||e.register(11)!=CLIENT||e.register(12)!=REQUEST)throw new IllegalStateException("Unexpected POST arguments");
            e.countBoundary("POST_application_service");int count=(int)e.read(SERVER+0x860,4);event(10,count);
            if(count>0)posted.write(e.emulator.readMemory(e.address(SERVER+0x5c),count));return;
        case 0x4011ae84:
            e.countBoundary("live_state_response_service");event(11);return;
        case 0x7ff31000:{
            if(e.register(10)!=CLIENT)throw new IllegalStateException("Unexpected write receiver");
            e.countBoundary("Print_virtual_write");int size=(int)e.register(12);
            if(size<0||size>32768)throw new IllegalStateException("Unbounded write");
            if(!headerMode)event(9,size);output.write(e.emulator.readMemory(e.address(e.register(11)),size));e.register(10,size);return;
        }
        case 0x7ff31004:
            e.countBoundary("Print_virtual_byte");output.write((int)e.register(11));e.register(10,1);return;
        default:throw new IllegalStateException(String.format("Unreviewed server call %08x",target));
        }
    }
    private Map<String,Object> snapshot(DisplayExecution e)throws Exception{
        List<Long> values=List.of(e.read(SERVER+0xd,1),e.read(SERVER+0x54,4),e.read(SERVER+0x58,1),
            e.read(SERVER+0x860,4),e.read(SERVER+0x864,4),e.read(SERVER+0x868,1),e.read(SERVER+0x869,1),
            e.read(REQUEST,4),e.read(REQUEST+4,4),e.read(REQUEST+8,1),e.read(PARSER+5,1),e.read(PARSER+6,1),
            e.read(REQUEST+0x50,4),e.read(REQUEST+0x54,4),e.read(REQUEST+0x58,1),connected?1L:0L);
        Map<String,Object> out=new LinkedHashMap<>();out.put("values",values);
        out.put("buffer_hex",HexFormat.of().formatHex(bytes(e,SERVER+0x5c,2049)));
        out.put("path_hex",HexFormat.of().formatHex(bytes(e,REQUEST+12,65)));return out;
    }
    private Map<String,Object> one(DisplayExecution e,JsonObject item)throws Exception{
        e.resetRegisters();e.clear(SERVER,0x1000);e.clear(VTABLE,32);e.clear(MIME,256);e.write(0x3ffc4654L,0x4a6d8917L,4);
        events.reset();output.reset();posted.reset();eventCount=0;headerMode=false;
        JsonArray initial=item.getAsJsonArray("initial");
        int[] offsets={0xd,0x54,0x58,0x860,0x864,0x868,0x869,0x86c,0x870,0x874,0x8cd,0x8ce,0x8bc,0x8c0,0x8c4};
        int[] sizes={1,4,1,4,4,1,1,4,4,1,1,1,4,4,1};
        for(int i=0;i<offsets.length;i++)e.write(SERVER+offsets[i],initial.get(i).getAsLong(),sizes[i]);
        connected=initial.get(15).getAsInt()!=0;e.write(SERVER+8,1000,4);e.write(PARSER,REQUEST,4);
        text(e,REQUEST+12,item.get("path").getAsString());text(e,SERVER+0x5c,item.get("buffer").getAsString());
        e.write(CLIENT,VTABLE,4);e.write(VTABLE+12,WRITE,4);e.write(VTABLE+8,BYTE,4);
        List<Map<String,Object>> snapshots=new ArrayList<>();
        for(JsonElement value:item.getAsJsonArray("actions")){
            JsonObject action=value.getAsJsonObject();String op=action.get("op").getAsString();e.register(10,SERVER);
            if(op.equals("headers")){
                headerMode=true;e.register(11,CLIENT);e.register(12,action.get("status").getAsLong());e.register(13,action.get("length").getAsLong());
                JsonElement type=action.get("mime");if(type.isJsonNull())e.register(14,0);else{text(e,MIME,type.getAsString());e.register(14,MIME);}
                e.invoke(0x400d97c4L);
            }else{
                input=action.getAsJsonArray("data");position=0;accepted=action.get("accept").getAsBoolean();int change=action.get("connection").getAsInt();
                if(change>=0)connected=change!=0;e.register(11,action.get("argument").getAsLong());
                e.invoke(op.equals("enable")?0x400d5c54L:0x4011fc58L);
            }
            snapshots.add(snapshot(e));
        }
        Map<String,Object> out=new LinkedHashMap<>();out.put("id",item.get("id").getAsString());out.put("snapshots",snapshots);
        out.put("event_count",eventCount);out.put("events_sha256",sha(events.toByteArray()));out.put("output_sha256",sha(output.toByteArray()));out.put("post_sha256",sha(posted.toByteArray()));return out;
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input JSON and output JSONL");
        JsonArray cases=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson gson=new Gson();int count=0;
        try(DisplayExecution e=new DisplayExecution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){
            for(JsonElement item:cases){try{out.write(gson.toJson(one(e,item.getAsJsonObject()))+"\n");}catch(Exception error){throw new IllegalStateException("Case "+count+" "+item,error);}count++;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",DisplayExecution.IMAGE_SHA);report.put("cases",count);
            report.put("input_file_sha256",sha(Files.readAllBytes(Path.of(args[0]))));report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original listener enable, accept/deadline/read framing, parser, bounded buffers, static route and response-header/Print instruction bodies. WiFi/socket operations, ROM memory/atoi and diagnostics are modeled services; POST application and live-state serialization are separate boundaries. No concurrent SDK, RF or physical hardware behavior is established.");
            report.put("hardware_tested",false);Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+count+" original HTTP server cases");
    }
}
