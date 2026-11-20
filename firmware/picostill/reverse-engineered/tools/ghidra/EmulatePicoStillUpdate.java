import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.util.*;
import java.util.zip.*;
import com.google.gson.*;
public class EmulatePicoStillUpdate extends EmulatePicoStillHttp {
    private static final long BASE=0x3ff00000L,PARAMS=BASE+0x2000,URI=BASE+0x3000,HOST=BASE+0x3400,PATH=BASE+0x3500,COUNTER=0x3ffc3078L;
    private JsonObject input;private JsonArray lines;private byte[] body;private int line,position,stringCount;private StringBuilder output;
    private void text(PicoStillExecution e,long pointer,String value){e.emulator.writeMemory(e.address(pointer),(value+"\0").getBytes(StandardCharsets.ISO_8859_1));}
    private void setString(PicoStillExecution e,long object,String value){long pointer=BASE+0x6000+1024*stringCount++;text(e,pointer,value);e.clear(object,12);e.write(object,pointer,4);e.write(object+4,value.length(),2);e.write(object+6,value.length(),2);}
    private String getString(PicoStillExecution e,long object)throws Exception{return string(e,e.read(object,4));}
    @Override protected void boundary(PicoStillExecution e,long target)throws Exception {
        if(nested.contains(target)){super.boundary(e,target);return;}
        switch((int)target){
        case 0x400da66c:e.countBoundary("WiFiClient_connect_host");events.add("connect_host:"+string(e,e.register(11))+":"+e.register(12));e.register(10,input.get("connect").getAsInt());return;
        case 0x400da090:{e.countBoundary("Client_available");int count=line<lines.size()?1:body.length-position;events.add("available:"+count);e.register(10,count);return;}
        case 0x400da290:e.countBoundary("Client_stop");events.add("stop");return;
        case 0x400da0ec:e.countBoundary("Client_flush");events.add("flush");return;
        case 0x400dc224:{e.countBoundary("Stream_readBytes");int requested=(int)e.register(12),count=Math.min(requested,Math.min(body.length-position,input.get("read_limit").getAsInt()));if(count>0)e.emulator.writeMemory(e.address(e.register(11)),Arrays.copyOfRange(body,position,position+count));position+=count;events.add("read_bytes:"+requested+":"+count);e.register(10,count);return;}
        case 0x400dc288:{e.countBoundary("Stream_readStringUntil_newline");String value=line<lines.size()?lines.get(line++).getAsString():"";events.add("read_line:"+value);setString(e,e.register(10),value);return;}
        case 0x400dc4ec:e.countBoundary("String_C_string_constructor");setString(e,e.register(10),string(e,e.register(11)));return;
        case 0x400dc534:e.countBoundary("String_copy_constructor");setString(e,e.register(10),getString(e,e.register(11)));return;
        case 0x400dc5fc:e.countBoundary("String_concat");setString(e,e.register(10),getString(e,e.register(10))+string(e,e.register(11)));return;
        case 0x400dc7f4:e.countBoundary("String_trim");setString(e,e.register(10),getString(e,e.register(10)).trim());return;
        case 0x400dc744:e.countBoundary("String_startsWith");e.register(10,getString(e,e.register(10)).startsWith(getString(e,e.register(11)))?1:0);return;
        case 0x400dc79c:e.countBoundary("String_indexOf");e.register(10,getString(e,e.register(10)).indexOf(getString(e,e.register(11))));return;
        case 0x400dc6b0:e.countBoundary("String_equals_C_string");e.register(10,getString(e,e.register(10)).equals(string(e,e.register(11)))?1:0);return;
        case 0x400dc7ac:{e.countBoundary("String_substring");String value=getString(e,e.register(11));int begin=(int)e.register(12),end=(int)e.register(13);setString(e,e.register(10),value.substring(Math.min(begin,value.length()),Math.min(end,value.length())));return;}
        case 0x4014a048:e.countBoundary("String_c_str");e.register(10,e.read(e.register(10),4));return;
        case 0x400dc33c:e.countBoundary("String_destructor");return;
        case 0x400dbee8:{e.countBoundary("Client_print_String");String value=getString(e,e.register(11));output.append(value);events.add("write:"+value);e.register(10,value.length());return;}
        case 0x400db684:e.countBoundary("Update_begin");events.add("begin:"+e.register(11));e.register(10,input.get("begin").getAsInt());return;
        case 0x400db9ec:{e.countBoundary("Update_write");int requested=(int)e.register(12);events.add("flash_write:"+hex(e,e.register(11),requested));e.register(10,Math.min(requested,input.get("write_limit").getAsInt()));return;}
        case 0x400db8e0:e.countBoundary("Update_end");events.add("end");e.register(10,input.get("end").getAsInt());return;
        case 0x400dbe64:e.countBoundary("MD5Builder_toString");events.add("md5");setString(e,e.register(10),"0123456789abcdef0123456789abcdef");return;
        case 0x400dbf3c:e.countBoundary("Serial_println");events.add("console_line:"+string(e,e.register(11)));return;
        case 0x400dbf14:e.countBoundary("Serial_print");events.add("console:"+string(e,e.register(11)));return;
        case 0x400d2a80:e.countBoundary("integer_diagnostic");events.add("integer:"+string(e,e.register(10))+":"+string(e,e.register(11))+":"+e.register(12));return;
        default:super.boundary(e,target);
        }
    }
    private Map<String,Object> update(PicoStillExecution e,JsonObject in)throws Exception{
        input=in;lines=in.getAsJsonArray("lines");line=position=stringCount=0;String raw=in.get("body").getAsString();body=new byte[raw.length()/2];for(int i=0;i<body.length;i++)body[i]=(byte)Integer.parseInt(raw.substring(2*i,2*i+2),16);
        e.resetRegisters();e.clear(BASE,0x20000);e.write(COUNTER,0,4);e.write(0x3ffc4770L,0x12345678L,4);events=new ArrayList<>();clock=in.get("now").getAsLong();output=new StringBuilder();
        e.write(BASE+0xc4,in.get("state").getAsLong(),4);e.write(BASE+0xc8,in.get("changed").getAsLong(),4);e.write(BASE+0xcc,in.get("written").getAsLong(),4);e.write(BASE+0x28,in.get("length").getAsLong(),4);e.write(BASE+0x2c,in.get("binary").getAsLong(),1);for(int i=0;i<2048;i++)e.write(BASE+0xd0+i,0x5a,1);
        e.write(0x3ffc347cL,in.get("error").getAsLong(),1);e.write(0x3ffc3488L,10,4);e.write(0x3ffc349cL,in.get("finished").getAsInt()!=0?10:11,4);
        text(e,URI,in.get("uri").getAsString());text(e,HOST,"host");text(e,PATH,"/path.bin");String op=in.get("op").getAsString();
        if(op.equals("url")){call(e,0x400d5e1cL,PARAMS,URI);call(e,0x400d37a4L,PARAMS);}
        else if(op.equals("begin"))call(e,0x400d8880L,BASE,HOST,PATH);
        else if(op.equals("sequence")){call(e,0x400d8880L,BASE,HOST,PATH);for(int i=0;i<in.get("polls").getAsInt();i++)call(e,0x400d91a8L,BASE);}
        else call(e,0x400d91a8L,BASE);
        Map<String,Object> row=new LinkedHashMap<>();row.put("id",in.get("id").getAsString());row.put("fields",Arrays.asList(e.read(BASE+0xc4,4),e.read(BASE+0xc8,4),e.read(BASE+0xcc,4),e.read(BASE+0x28,4),e.read(BASE+0x2c,1)));row.put("buffer",hex(e,BASE+0xd0,2048));row.put("parameters",hex(e,PARAMS,145));row.put("sent",output.toString());row.put("events",events);row.put("assertion_count",e.read(COUNTER,4));row.put("clock",clock);return row;
    }
    @Override public void run()throws Exception{
        nested.addAll(Arrays.asList(0x400d5bc4L,0x400d5cc4L,0x400d45c4L,0x400d2bb8L,0x4014a014L,0x4014a054L,0x400d28c8L));String[] args=getScriptArgs();JsonArray inputs;
        try(GZIPInputStream stream=new GZIPInputStream(Files.newInputStream(Path.of(args[0])))){inputs=JsonParser.parseReader(new InputStreamReader(stream,StandardCharsets.UTF_8)).getAsJsonArray();}
        Map<String,Object> report=new LinkedHashMap<>();List<Map<String,Object>> cases=new ArrayList<>();try(PicoStillExecution e=new PicoStillExecution(currentProgram,monitor,this::boundary)){
            e.instructionLimit=200000;for(JsonElement input:inputs){try{cases.add(update(e,input.getAsJsonObject()));}catch(Exception error){throw new IllegalStateException(input.getAsJsonObject().get("id").getAsString(),error);}}
            report.put("input_sha256",PicoStillExecution.IMAGE_SHA);report.put("cases_input_gzip_sha256",sha(Path.of(args[0])));report.put("cases",cases);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);report.put("scope","Original updater URL, request construction and all seven update states, including header interpretation, short-read/write accounting and success/failure continuation. String, Client/Stream, Update flash and MD5 SDK calls, clock, ROM conversion and diagnostics are explicit boundaries. No flash installation or hardware qualification.");report.put("full_cpu_emulation",false);report.put("hardware_tested",false);
        }
        try(GZIPOutputStream out=new GZIPOutputStream(Files.newOutputStream(Path.of(args[1])))){out.write((new Gson().toJson(report)+"\n").getBytes(StandardCharsets.UTF_8));}println("Executed "+cases.size()+" original PicoStill updater cases");
    }
}
