// Original CoreEvent initialization/reset and one-slot payload publication.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.security.MessageDigest;
import java.io.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulateCoreEvent extends GhidraScript {
    private static final long EVENT=0x3ff30000L,QUEUE=0x3ff34000L;
    private static String sha(byte[] bytes)throws Exception{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));}
    private void boundary(DisplayExecution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        switch((int)target){
        case 0x400eef6c:case 0x400eed28:case 0x400ee998:case 0x400d0f80:case 0x401bf484:case 0x400f02e4:
            e.invoke(target);return;
        case 0x4000c44c:{
            e.countBoundary("ROM_memset");byte[] value=new byte[(int)e.register(12)];Arrays.fill(value,(byte)e.register(11));
            e.emulator.writeMemory(e.address(e.register(10)),value);return;
        }
        case 0x4000c2c8:
            e.countBoundary("ROM_memcpy");if(e.register(12)>0)e.emulator.writeMemory(e.address(e.register(10)),e.emulator.readMemory(e.address(e.register(11)),(int)e.register(12)));return;
        case 0x40081388:e.countBoundary("diagnostic_millis");e.register(10,1000);return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:e.countBoundary("diagnostic_log");return;
        case 0x400d06b4:e.countBoundary("CoreEvent_diagnostic_description");e.write(e.register(11),0,1);return;
        case 0x401367a0:throw new IllegalStateException("Original stack guard failed");
        case 0x400db014:throw new IllegalStateException("Unexpected assertion line "+e.register(11));
        default:throw new IllegalStateException(String.format("Unreviewed event call %08x",target));
        }
    }
    private Map<String,Object> snapshot(DisplayExecution e)throws Exception{
        Map<String,Object> out=new LinkedHashMap<>();out.put("pending",e.read(QUEUE+0xdf8,1)!=0);
        out.put("current_sha256",sha(e.emulator.readMemory(e.address(EVENT+4),0xdec)));
        out.put("queued_sha256",sha(e.emulator.readMemory(e.address(QUEUE+12),0xdec)));return out;
    }
    private Map<String,Object> one(DisplayExecution e,JsonObject item)throws Exception{
        e.resetRegisters();e.clear(EVENT,0x1000);e.clear(QUEUE,0x1000);e.write(0x3ffc4654L,0x4a6d8917L,4);
        e.emulator.writeMemory(e.address(EVENT),HexFormat.of().parseHex(item.get("current_hex").getAsString()));
        e.emulator.writeMemory(e.address(QUEUE+8),HexFormat.of().parseHex(item.get("queued_hex").getAsString()));
        e.write(EVENT,0x3f41f8d0L,4);e.write(QUEUE+8,0x3f41f8d0L,4);
        e.write(QUEUE+0xdf8,item.get("pending").getAsBoolean()?1:0,1);
        List<Map<String,Object>> snapshots=new ArrayList<>();
        for(JsonElement input:item.getAsJsonArray("actions")){
            JsonArray action=input.getAsJsonArray();int op=action.get(0).getAsInt();
            e.register(10,EVENT);e.register(11,action.get(1).getAsLong());
            if(op==0)e.invoke(0x400eef6cL);
            else if(op==1)e.invoke(0x401bf484L);
            else{
                if(op==3){e.register(11,7);e.invoke(0x401bf484L);e.write(EVENT+0xdec,0,4);}
                e.register(10,QUEUE);e.register(11,EVENT);e.invoke(0x400f02e4L);
            }
            snapshots.add(snapshot(e));
        }
        Map<String,Object> out=new LinkedHashMap<>();out.put("id",item.get("id").getAsString());out.put("snapshots",snapshots);return out;
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input JSON and output JSONL");
        JsonArray cases=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson gson=new Gson();int count=0;
        try(DisplayExecution e=new DisplayExecution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){
            for(JsonElement value:cases){try{out.write(gson.toJson(one(e,value.getAsJsonObject()))+"\n");}catch(Exception error){throw new IllegalStateException("Case "+count,error);}count++;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",DisplayExecution.IMAGE_SHA);report.put("cases",count);
            report.put("input_file_sha256",sha(Files.readAllBytes(Path.of(args[0]))));report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original CoreEvent and twenty RecipeRef constructors, metadata reset and one-slot mailbox publication. Complete payload bytes excluding compiler vtable are compared after each operation. ROM memory, diagnostics and sequential MEMW are explicit boundaries; no concurrent mailbox claim.");
            report.put("hardware_tested",false);Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+count+" original CoreEvent cases");
    }
}
