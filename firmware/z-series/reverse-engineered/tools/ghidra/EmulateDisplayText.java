// Original Z119 UTF8Decode, text context and font rendering execution.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulateDisplayText extends GhidraScript {
    private static final long OBJECT=0x3ff50000L,ARG=0x3ff55000L,TEXT=0x3ff60000L,UTF=0x3ff57000L;
    private static final long[] FONTS={0x3f42b31cL,0x3f42ded4L,0x3f437478L,0x3f43d718L,0x3f440f88L};
    private static final int[] WIDTHS={9,17,14,8,6},HEIGHTS={11,20,16,16,8};
    private static class OriginalAssertion extends RuntimeException {final long line;OriginalAssertion(long value){line=value;}}
    private static String sha(byte[] bytes)throws Exception{
        byte[] digest=MessageDigest.getInstance("SHA-256").digest(bytes);StringBuilder out=new StringBuilder();
        for(byte b:digest)out.append(String.format("%02x",b&255));return out.toString();
    }
    private void boundary(DisplayExecution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        switch((int)target){
            case 0x4000c44c:{int count=(int)e.register(12);if(count<0||count>65536)throw new IllegalStateException("memset bound");
                e.countBoundary("memset");if(count>0){byte[] bytes=new byte[count];Arrays.fill(bytes,(byte)e.register(11));e.emulator.writeMemory(e.address(e.register(10)),bytes);}return;}
            case 0x4000c2c8:{int count=(int)e.register(12);if(count<0||count>65536)throw new IllegalStateException("memcpy bound");
                e.countBoundary("memcpy_nonoverlapping");if(count>0)e.emulator.writeMemory(e.address(e.register(10)),e.emulator.readMemory(e.address(e.register(11)),count));return;}
            case 0x400014c0:{
                e.countBoundary("strlen_bounded_fixture");long text=e.register(10);if(text==0)throw new IllegalStateException("Null original strlen");
                int count=0;while(count<65536 && e.read(text+count,1)!=0)count++;if(count==65536)throw new IllegalStateException("Unterminated fixture");e.register(10,count);return;}
            case 0x40081388:e.countBoundary("diagnostic_clock");e.register(10,0);return;
            case 0x400d21cc:e.countBoundary("diagnostic_log");return;
            case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
            case 0x400db014:e.countBoundary("original_assertion");throw new OriginalAssertion(e.register(11));
            case 0x400d1aa8:case 0x400d1ae4:case 0x400de4b4:case 0x400e70cc:case 0x400e712c:
            case 0x401bf500:case 0x401bf514:case 0x401bf574:case 0x401bf5a4:case 0x401bf5c4:case 0x401bf5f4:
            case 0x400e7460:case 0x400d70f8:case 0x400d703c:case 0x400de2d8:case 0x400de30c:
            case 0x400de378:case 0x401bf3d4:case 0x400e7970:
                e.invoke(target);return;
            default:throw new IllegalStateException(String.format("Unmodeled text call %08x",target));
        }
    }
    private void invoke(DisplayExecution e,long target,long... args)throws Exception{
        for(int i=0;i<args.length;i++)e.register(10+i,args[i]);e.invoke(target);
    }
    private List<Long> words(DisplayExecution e,long address,int count)throws Exception{
        List<Long> result=new ArrayList<>();for(int i=0;i<count;i++)result.add(e.read(address+4*i,4));return result;
    }
    private Map<String,Object> runCase(DisplayExecution e,JsonObject input)throws Exception{
        e.resetRegisters();e.clear(OBJECT,0x2100);e.clear(ARG,0x1000);e.clear(UTF,16);e.clear(TEXT,4096);
        int seed=input.get("seed").getAsInt(),font=input.get("font").getAsInt();
        byte[] pixels=new byte[4096];for(int i=0;i<pixels.length;i++)pixels[i]=(byte)((i*73+seed)^(i>>>3));e.emulator.writeMemory(e.address(OBJECT+11),pixels);
        e.write(OBJECT+0x2068,64,4);e.write(OBJECT+0x201c,0,4);e.write(OBJECT+0x2020,0,4);e.write(OBJECT+0x2024,127,4);e.write(OBJECT+0x2028,63,4);
        invoke(e,0x400d1ae4L,OBJECT+0x200c,FONTS[font],input.get("spacing").getAsLong());
        JsonArray bounds=input.getAsJsonArray("bounds");for(int i=0;i<4;i++)e.write(ARG+4*i,bounds.get(i).getAsLong(),4);
        invoke(e,0x400d1aa8L,OBJECT+0x200c,ARG);
        byte[] text=Base64.getDecoder().decode(input.get("text_base64").getAsString());if(text.length>=4096)throw new IllegalArgumentException("Text fixture too long");
        e.emulator.writeMemory(e.address(TEXT),text);e.write(TEXT+text.length,0,1);
        String error="",operation=input.get("op").getAsString();List<Long> codepoints=new ArrayList<>();long length=0;List<Long> decoder=List.of();
        try {
            if(operation.equals("utf8")){
                invoke(e,0x400d703cL,TEXT);length=e.register(10);invoke(e,0x400d70f8L,UTF,TEXT);
                for(int step=0;step<512 && e.read(UTF+4,2)<e.read(UTF+6,2);step++){invoke(e,0x400de378L,UTF);codepoints.add(e.register(10));}
            }else if(operation.equals("glyphs")){
                JsonArray point=input.getAsJsonArray("point");e.write(ARG,point.get(0).getAsLong(),4);e.write(ARG+4,point.get(1).getAsLong(),4);
                invoke(e,0x400e7970L,OBJECT,TEXT,input.get("characters").getAsLong(),ARG,input.get("invert").getAsLong());
            }else if(operation.equals("text")){
                JsonArray options=input.getAsJsonArray("options");if(options.size()!=6)throw new IllegalArgumentException("Text options shape");
                e.clear(ARG,32);for(int i=0;i<6;i++)e.write(ARG+4*i,options.get(i).getAsLong(),4);
                invoke(e,0x400e7bdcL,OBJECT,TEXT,ARG);
            }else if(!operation.equals("metrics"))throw new IllegalArgumentException("Unknown text operation");
        }catch(OriginalAssertion assertion){
            if(assertion.line==0x29)error="truncated";
            else if(assertion.line==0xba || assertion.line==0xc3 || assertion.line==0xcd)error="overlong";
            else if(assertion.line==0x3bf)error="empty";
            else throw assertion;
        }
        if(operation.equals("utf8"))decoder=List.of(e.read(UTF+4,2),e.read(UTF+6,2),e.read(UTF+8,2));
        List<Long> metrics=new ArrayList<>();metrics.add(e.read(OBJECT+0x200c,4));metrics.add(e.read(OBJECT+0x2010,4));
        metrics.add(e.read(OBJECT+0x2018,4));metrics.addAll(words(e,OBJECT+0x201c,10));
        Map<String,Object> result=new LinkedHashMap<>();result.put("id",input.get("id").getAsString());result.put("error",error);
        result.put("pixels_sha256",sha(e.emulator.readMemory(e.address(OBJECT+11),4096)));result.put("metrics",metrics);
        result.put("length",length);result.put("codepoints",codepoints);result.put("decoder",decoder);return result;
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input JSON and output JSONL");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson gson=new Gson();int count=0;
        try(DisplayExecution e=new DisplayExecution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){
            for(JsonElement input:inputs){out.write(gson.toJson(runCase(e,input.getAsJsonObject()))+"\n");count++;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",DisplayExecution.IMAGE_SHA);report.put("input_file_sha256",sha(Files.readAllBytes(Path.of(args[0]))));report.put("cases",count);
            report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original Z UTF8Decode, text context, font rendering and nested packed bitmap instructions; explicit libc/diagnostic/assertion boundaries; recovered fonts read from original DROM");
            report.put("full_cpu_or_hardware_emulation",false);report.put("hardware_tested",false);
            Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+count+" original Z text scenarios");
    }
}
