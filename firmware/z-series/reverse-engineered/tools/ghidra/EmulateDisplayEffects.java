// Execute original Z119 display gradients, carets, animation and slide bodies.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulateDisplayEffects extends GhidraScript {
    private static final long OBJECT=0x3ff50000L, ARG=0x3ff55000L, DATA=0x3ff60000L, SPI=0x7ff10000L;
    private final ByteArrayOutputStream events=new ByteArrayOutputStream();
    private int flushes,delays;
    private static String sha(byte[] bytes)throws Exception{
        byte[] digest=MessageDigest.getInstance("SHA-256").digest(bytes);StringBuilder out=new StringBuilder();
        for(byte b:digest)out.append(String.format("%02x",b&255));return out.toString();
    }
    private static byte[] pattern(int count,int seed,int multiplier){
        byte[] data=new byte[count];for(int i=0;i<count;i++)data[i]=(byte)((i*multiplier+seed)^(i>>>3));return data;
    }
    private void word(long value){for(int i=0;i<4;i++)events.write((int)(value>>>(i*8))&255);}
    private void boundary(DisplayExecution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        switch((int)target){
            case 0x40080a9c:e.countBoundary("digitalWrite");events.write(1);events.write((int)e.register(10));events.write((int)e.register(11));return;
            case 0x401257c8:
                if(e.register(10)!=SPI)throw new IllegalStateException("Unexpected SPI instance");
                e.countBoundary("SPI.transfer");events.write(2);events.write((int)e.register(11));e.register(10,0);return;
            case 0x401257e4:
                if(e.register(10)!=SPI||e.register(12)!=0||e.register(13)!=4096)throw new IllegalStateException("Unexpected bulk SPI contract");
                e.countBoundary("SPI.transferBytes");events.write(3);word(4096);
                events.write(e.emulator.readMemory(e.address(e.register(11)),4096));flushes++;return;
            case 0x401289a0:e.countBoundary("delay");events.write(4);word(e.register(10));delays++;return;
            case 0x4000c44c:{
                e.countBoundary("memset");int count=(int)e.register(12);if(count<0||count>65536)throw new IllegalStateException("Unexpected memset size");
                if(count>0){byte[] bytes=new byte[count];Arrays.fill(bytes,(byte)e.register(11));e.emulator.writeMemory(e.address(e.register(10)),bytes);}return;
            }
            case 0x4000c2c8:{
                e.countBoundary("memcpy_nonoverlapping");int count=(int)e.register(12);if(count<0||count>65536)throw new IllegalStateException("Unexpected memcpy size");
                if(count>0)e.emulator.writeMemory(e.address(e.register(10)),e.emulator.readMemory(e.address(e.register(11)),count));return;
            }
            case 0x400d5a44:e.countBoundary("scope_log_enter");return;
            case 0x400d5a68:e.countBoundary("scope_log_exit");return;
            case 0x4008c2b4:e.countBoundary("basename_for_assertion");return;
            case 0x400db014:throw new IllegalStateException("Original display assertion at line "+e.register(11));
            case 0x400d1b24:case 0x400d1b6c:case 0x400d1b88:case 0x400e70cc:case 0x400e712c:
            case 0x401bf500:case 0x401bf514:case 0x401bf528:case 0x401bf574:case 0x401bf5a4:
            case 0x401bf5c4:case 0x401bf5f4:case 0x400e7460:case 0x400e880c:case 0x400e89b0:
            case 0x400e77f0:case 0x400e7a7c:
                e.invoke(target);return;
            default:throw new IllegalStateException(String.format("Unmodeled display effect call %08x",target));
        }
    }
    private void invoke(DisplayExecution e,long entry,long... arguments)throws Exception{
        for(int i=0;i<arguments.length;i++){
            if(i<6)e.register(10+i,arguments[i]);else e.write(e.register(1)+(i-6)*4,arguments[i],4);
        }
        e.invoke(entry);
    }
    private void bitmap(DisplayExecution e,long pointer,long data,int width,int height,int stride,int seed)throws Exception{
        e.emulator.writeMemory(e.address(data),pattern(stride*height,seed,37));
        long[] descriptor={width,height,stride,0,0,width-1,height-1,data,stride*height};
        for(int i=0;i<descriptor.length;i++)e.write(pointer+4*i,descriptor[i],4);
    }
    private Map<String,Object> runCase(DisplayExecution e,JsonObject input)throws Exception{
        e.resetRegisters();e.clear(OBJECT,0x2100);e.clear(ARG,0x1000);events.reset();flushes=delays=0;
        int seed=input.get("seed").getAsInt();
        e.emulator.writeMemory(e.address(OBJECT+11),pattern(4096,seed,73));
        e.emulator.writeMemory(e.address(OBJECT+0x100b),pattern(4096,seed+47,73));
        e.write(OBJECT+4,SPI,4);e.write(OBJECT+8,0,1);e.write(OBJECT+9,2,1);e.write(OBJECT+10,4,1);e.write(OBJECT+0x2068,64,4);
        long[] a=new long[24];JsonArray args=input.getAsJsonArray("args");for(int i=0;i<args.size();i++)a[i]=args.get(i).getAsLong()&0xffffffffL;
        JsonArray shape=input.getAsJsonArray("shape");int width=shape.get(0).getAsInt(),height=shape.get(1).getAsInt(),stride=shape.get(2).getAsInt();
        int mask=input.get("null_mask").getAsInt();
        for(int i=0;i<3;i++){
            bitmap(e,ARG+0x100+i*0x40,DATA+i*0x10000,width,height,stride,seed+11+i*37);
            e.write(ARG+0x300+i*4,(mask&(1<<i))!=0?0:ARG+0x100+i*0x40,4);
        }
        String operation=input.get("op").getAsString();
        switch(operation){
            case "vertical":case "horizontal":
                for(int i=0;i<4;i++)e.write(ARG+i*4,a[i],4);
                invoke(e,operation.equals("vertical")?0x400e7150L:0x400e7274L,OBJECT,ARG,a[4],a[5]);break;
            case "caret":
                e.write(ARG,a[0],4);e.write(ARG+4,a[1],4);invoke(e,0x400e93f8L,OBJECT,ARG,a[2],a[3],a[4]);break;
            case "animation":
                e.write(ARG+0x280,a[0],4);e.write(ARG+0x284,a[1],4);e.write(ARG+0x288,a[2],4);e.write(ARG+0x28c,ARG+0x300,4);
                e.write(OBJECT+0x2044,a[6]!=0?ARG+0x280:0,4);e.write(OBJECT+0x2048,a[3],4);e.write(OBJECT+0x204c,a[4],4);
                e.write(OBJECT+0x2050,a[5],4);e.write(OBJECT+0x2054,a[8],4);e.write(OBJECT+0x2058,a[9],4);e.write(OBJECT+0x205c,a[7],1);
                for(JsonElement time:input.getAsJsonArray("times"))invoke(e,0x400e7904L,OBJECT,time.getAsLong());break;
            case "slide":
                e.write(ARG,a[4],4);e.write(ARG+4,a[5],4);invoke(e,0x400e7a7cL,OBJECT,ARG+0x100,a[3],ARG,a[6],a[7]);break;
            case "aligned":invoke(e,0x400e7b2cL,OBJECT,ARG+0x100,a[3],a[8],a[9],a[6],a[7]);break;
            default:throw new IllegalArgumentException("Unknown display effect "+operation);
        }
        Map<String,Object> record=new LinkedHashMap<>();record.put("id",input.get("id").getAsString());record.put("result",0);
        record.put("pixels_sha256",sha(e.emulator.readMemory(e.address(OBJECT+11),4096)));
        record.put("transfer_sha256",sha(e.emulator.readMemory(e.address(OBJECT+0x100b),4096)));
        record.put("events_sha256",sha(events.toByteArray()));record.put("motion",List.of(e.read(OBJECT+0x2044,4)!=0?1:0,
            e.read(OBJECT+0x2048,4),e.read(OBJECT+0x204c,4),e.read(OBJECT+0x2050,4),e.read(OBJECT+0x205c,1)));
        record.put("flushes",flushes);record.put("delays",delays);return record;
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input JSON and output JSONL");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson gson=new Gson();int count=0;
        try(DisplayExecution e=new DisplayExecution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){
            for(JsonElement input:inputs){out.write(gson.toJson(runCase(e,input.getAsJsonObject()))+"\n");count++;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",DisplayExecution.IMAGE_SHA);report.put("cases",count);
            report.put("input_file_sha256",sha(Files.readAllBytes(Path.of(args[0]))));
            report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original gradient, caret, animation and slide instructions, including nested geometry, drawing and transfer-buffer reversal; GPIO/SPI/libc/delay boundaries recorded explicitly");
            report.put("full_cpu_or_hardware_emulation",false);report.put("hardware_tested",false);
            Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+count+" original Z display effect scenarios");
    }
}
