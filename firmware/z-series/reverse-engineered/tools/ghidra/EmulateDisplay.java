// Original Z119 display hardware, geometry and packed-pixel operations.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulateDisplay extends GhidraScript {
    private static final long OBJECT=0x3ff50000L, ARG=0x3ff55000L, DATA=0x3ff60000L, SPI=0x7ff10000L;
    private final List<Object> events=new ArrayList<>();
    private static String sha(byte[] bytes)throws Exception{
        byte[] digest=MessageDigest.getInstance("SHA-256").digest(bytes);StringBuilder out=new StringBuilder();
        for(byte b:digest)out.append(String.format("%02x",b&255));return out.toString();
    }
    private static byte[] pattern(int count,int seed,int multiplier){
        byte[] data=new byte[count];for(int i=0;i<count;i++)data[i]=(byte)((i*multiplier+seed)^(i>>>3));return data;
    }
    private void boundary(DisplayExecution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        switch((int)target){
            case 0x40080904:e.countBoundary("pinMode");events.add(List.of("mode",e.register(10),e.register(11)));return;
            case 0x40080a9c:e.countBoundary("digitalWrite");events.add(List.of("gpio",e.register(10),e.register(11)));return;
            case 0x401257c8:
                if(e.register(10)!=SPI)throw new IllegalStateException("Unexpected SPI instance");
                e.countBoundary("SPI.transfer");events.add(List.of("spi",e.register(11)&255));e.register(10,0);return;
            case 0x401257e4:
                if(e.register(10)!=SPI||e.register(12)!=0||e.register(13)!=4096)throw new IllegalStateException("Unexpected SPI bulk contract");
                e.countBoundary("SPI.transferBytes");events.add(List.of("spi_buffer",4096,sha(e.emulator.readMemory(e.address(e.register(11)),4096))));return;
            case 0x4000c44c:{
                e.countBoundary("memset");int count=(int)e.register(12);if(count<0||count>65536)throw new IllegalStateException("Unexpected memset size");
                if(count>0){byte[] bytes=new byte[count];Arrays.fill(bytes,(byte)e.register(11));e.emulator.writeMemory(e.address(e.register(10)),bytes);}return;
            }
            case 0x4000c2c8:{
                e.countBoundary("memcpy_nonoverlapping");int count=(int)e.register(12);if(count<0||count>65536)throw new IllegalStateException("Unexpected memcpy size");
                if(count>0)e.emulator.writeMemory(e.address(e.register(10)),e.emulator.readMemory(e.address(e.register(11)),count));return;
            }
            case 0x400d6f78:e.countBoundary("display_information");events.add(List.of("info"));return;
            case 0x400fc590:e.countBoundary("console_registration");events.add(List.of("register_commands"));return;
            case 0x400d5a44:e.countBoundary("scope_log_enter");return;
            case 0x400d5a68:e.countBoundary("scope_log_exit");return;
            case 0x4008c2b4:e.countBoundary("basename_for_assertion");return;
            case 0x400db014:throw new IllegalStateException("Original display assertion at line "+e.register(11));
            case 0x400d1b24:case 0x400d1b6c:case 0x400d1b88:case 0x400e70cc:case 0x400e712c:
            case 0x401bf500:case 0x401bf514:case 0x401bf528:case 0x401bf574:case 0x401bf5a4:
            case 0x401bf5c4:case 0x401bf5f4:case 0x400e7460:case 0x400e880c:
                e.invoke(target);return;
            default:throw new IllegalStateException(String.format("Unmodeled display call %08x",target));
        }
    }
    private void rect(DisplayExecution e,long pointer,JsonArray values){
        if(values.size()!=4)throw new IllegalArgumentException("Rectangle shape");
        for(int i=0;i<4;i++)e.write(pointer+4*i,values.get(i).getAsLong(),4);
    }
    private List<Long> rectValues(DisplayExecution e,long pointer)throws Exception{
        List<Long> values=new ArrayList<>();for(int i=0;i<4;i++)values.add(e.read(pointer+4*i,4));return values;
    }
    private void invoke(DisplayExecution e,long entry,long... arguments)throws Exception{
        for(int i=0;i<arguments.length;i++)e.register(10+i,arguments[i]);e.invoke(entry);
    }
    private Map<String,Object> runCase(DisplayExecution e,JsonObject input)throws Exception{
        e.resetRegisters();e.clear(OBJECT,0x2100);e.clear(ARG,0x1000);events.clear();
        int seed=input.get("seed").getAsInt();
        e.emulator.writeMemory(e.address(OBJECT+11),pattern(4096,seed,73));
        e.emulator.writeMemory(e.address(OBJECT+0x100b),pattern(4096,seed+47,73));
        e.write(OBJECT+4,SPI,4);e.write(OBJECT+8,input.get("cs").getAsInt(),1);
        e.write(OBJECT+9,input.get("dc").getAsInt(),1);e.write(OBJECT+10,input.get("reset").getAsInt(),1);
        e.write(OBJECT+0x2068,64,4);e.write(ARG+0x300,OBJECT,4);
        String operation=input.get("op").getAsString();List<Object> result=new ArrayList<>();
        switch(operation){
            case "clear":invoke(e,0x400d1b6cL,OBJECT);break;
            case "command":invoke(e,0x400d1b24L,OBJECT,input.get("value").getAsInt());break;
            case "flush":invoke(e,0x400d1b88L,OBJECT);break;
            case "clear_flush":invoke(e,0x400d1bfcL,ARG+0x300);break;
            case "invert":invoke(e,input.get("value").getAsInt()!=0?0x400d1b4cL:0x400d1b5cL,ARG+0x300);break;
            case "setup":invoke(e,0x400fc9c4L,OBJECT);break;
            case "rectangle":
                rect(e,ARG,input.getAsJsonArray("rect"));
                invoke(e,0x400e880cL,OBJECT,ARG,input.get("value").getAsInt(),input.get("filled").getAsInt());break;
            case "bitmap":{
                JsonArray shape=input.getAsJsonArray("shape");int width=shape.get(0).getAsInt(),height=shape.get(1).getAsInt(),stride=shape.get(2).getAsInt();
                byte[] data=pattern(stride*height,seed+11,37);e.emulator.writeMemory(e.address(DATA),data);
                e.write(ARG+0x80,width,4);e.write(ARG+0x84,height,4);e.write(ARG+0x88,stride,4);
                e.write(ARG+0x8c,0,4);e.write(ARG+0x90,0,4);e.write(ARG+0x94,width-1,4);e.write(ARG+0x98,height-1,4);
                e.write(ARG+0x9c,DATA,4);e.write(ARG+0xa0,data.length,4);
                JsonArray point=input.getAsJsonArray("point");e.write(ARG+0x40,point.get(0).getAsLong(),4);e.write(ARG+0x44,point.get(1).getAsLong(),4);
                rect(e,ARG,input.getAsJsonArray("rect"));invoke(e,0x400e7460L,OBJECT,ARG+0x40,ARG+0x80,ARG,input.get("value").getAsInt());break;
            }
            case "geometry":{
                rect(e,ARG,input.getAsJsonArray("rect"));rect(e,ARG+16,input.getAsJsonArray("other"));
                for(long fn:new long[]{0x401bf500L,0x401bf514L,0x401bf528L,0x401bf5a4L}){invoke(e,fn,ARG);result.add(e.register(10));}
                invoke(e,0x401bf574L,ARG,ARG+16);long intersects=e.register(10);result.add(intersects);
                invoke(e,0x401bf5c4L,ARG,ARG+16);result.add(e.register(10));
                if(intersects!=0 && input.get("intersect_valid").getAsBoolean()){
                    invoke(e,0x400e70ccL,ARG+32,ARG,ARG+16);result.add(rectValues(e,ARG+32));
                }else result.add(List.of());
                JsonArray delta=input.getAsJsonArray("delta");invoke(e,0x401bf5f4L,ARG,delta.get(0).getAsLong(),delta.get(1).getAsLong());result.add(rectValues(e,ARG));break;
            }
            default:throw new IllegalArgumentException("Unknown operation "+operation);
        }
        Map<String,Object> record=new LinkedHashMap<>();record.put("id",input.get("id").getAsString());record.put("result",result);
        record.put("pixels_sha256",sha(e.emulator.readMemory(e.address(OBJECT+11),4096)));
        record.put("transfer_sha256",sha(e.emulator.readMemory(e.address(OBJECT+0x100b),4096)));record.put("events",new ArrayList<>(events));return record;
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input JSON and output JSONL");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson gson=new Gson();int count=0;
        try(DisplayExecution e=new DisplayExecution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){
            for(JsonElement input:inputs){out.write(gson.toJson(runCase(e,input.getAsJsonObject()))+"\n");count++;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",DisplayExecution.IMAGE_SHA);report.put("cases",count);
            report.put("input_file_sha256",sha(Files.readAllBytes(Path.of(args[0]))));
            report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original display hardware, geometry and pixel operation instructions; explicit SPI/GPIO/libc/diagnostic boundaries; register windows adapted, reviewed display LOOP executed with an explicit count/end adapter");
            report.put("full_cpu_or_hardware_emulation",false);report.put("hardware_tested",false);
            Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+count+" original Z display scenarios");
    }
}
