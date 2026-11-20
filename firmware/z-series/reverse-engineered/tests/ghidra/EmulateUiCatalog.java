// Original Z119 localization and screen-body selection instructions.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;

public class EmulateUiCatalog extends GhidraScript {
    private int diagnostics;
    private static String sha(byte[] bytes)throws Exception {
        return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));
    }
    private static Map<String,Object> row(Object... values) {
        Map<String,Object> result=new LinkedHashMap<>();
        for(int i=0;i<values.length;i+=2)result.put((String)values[i],values[i+1]);return result;
    }
    private void boundary(Z119Execution e,long target)throws Exception {
        if(e.arithmetic(target))return;
        switch((int)target){
        case 0x40081388:e.countBoundary("diagnostic_millis");e.register(10,0);return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:e.countBoundary("diagnostic_output");diagnostics++;return;
        default:throw new IllegalStateException(String.format("Unreviewed catalog call %08x",target));
        }
    }
    private byte[] string(Z119Execution e,long address)throws Exception {
        java.io.ByteArrayOutputStream out=new java.io.ByteArrayOutputStream();
        for(int i=0;i<4096;i++){int b=(int)e.read(address+i,1);if(b==0)return out.toByteArray();out.write(b);}
        throw new IllegalStateException("Unterminated original lookup text");
    }
    @Override public void run()throws Exception {
        String[] args=getScriptArgs();if(args.length!=3)throw new IllegalArgumentException("Expected cases, verified image layout and output JSON");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();
        JsonObject layout=JsonParser.parseString(Files.readString(Path.of(args[1]))).getAsJsonObject();
        byte[] raw=Files.readAllBytes(Path.of(layout.get("input_path").getAsString()));
        if(!sha(raw).equals(Z119Execution.IMAGE_SHA))throw new IllegalStateException("Wrong original image");
        for(JsonElement value:layout.getAsJsonArray("segments")){
            JsonObject segment=value.getAsJsonObject();int size=segment.get("size").getAsInt();if(size==0)continue;
            byte[] loaded=new byte[size];currentProgram.getMemory().getBytes(toAddr(segment.get("address").getAsLong()),loaded);
            int offset=segment.get("offset").getAsInt();
            if(!Arrays.equals(loaded,Arrays.copyOfRange(raw,offset,offset+size)))throw new IllegalStateException("Project differs from original bytes");
        }
        List<Object> results=new ArrayList<>(),functions=new ArrayList<>();
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary)){
            for(JsonElement value:inputs){
                JsonObject input=value.getAsJsonObject();e.resetRegisters();diagnostics=0;
                long entry=input.get("function").getAsLong(),item=input.get("item").getAsLong(),language=input.get("language").getAsLong();
                final long output=0x3ff95000L,sentinel=0x01020304L;boolean screen=input.get("kind").getAsString().equals("screen");
                if(screen){e.write(output,sentinel,4);e.register(10,0);e.register(11,item);e.register(12,output);e.register(13,language);}
                else {e.register(10,item);e.register(11,language);}
                e.invoke(entry);long pointer=screen?e.read(output,4):e.register(10);
                results.add(row("changed",pointer!=sentinel,"address",pointer,"text_base64",pointer==sentinel?"":Base64.getEncoder().encodeToString(string(e,pointer)),"diagnostics",diagnostics));
            }
            for(String address:e.functions.keySet()){
                Function function=currentProgram.getFunctionManager().getFunctionAt(toAddr(Long.parseLong(address.substring(2),16)));
                if(function==null)throw new IllegalStateException("Executed body lacks defined bounds");
                List<Object> ranges=new ArrayList<>();
                for(AddressRange range:function.getBody().getAddressRanges()){
                    int size=(int)range.getLength();byte[] bytes=new byte[size];currentProgram.getMemory().getBytes(range.getMinAddress(),bytes);
                    ranges.add(row("address",range.getMinAddress().getOffset(),"size",size,"sha256",sha(bytes)));
                }
                functions.add(row("address",address,"calls",e.functions.get(address),"ranges",ranges));
            }
            Map<String,Object> report=row("input_sha256",Z119Execution.IMAGE_SHA,"cases",results,"original_functions",functions,
                "modeled_boundaries",e.boundaries,"scope","Original lookup and screen switch/load/store instructions; diagnostic SDK calls and sequential windowed call ABI are explicit boundaries. No invalid-language screen pointer is dereferenced.",
                "hardware_tested",false,"full_cpu_emulation",false);
            Files.writeString(Path.of(args[2]),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+results.size()+" original UI catalog lookups");
    }
}
