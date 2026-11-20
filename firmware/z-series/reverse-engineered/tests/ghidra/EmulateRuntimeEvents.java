// Original four-field RuntimeEvent mailbox copy and read.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;

public class EmulateRuntimeEvents extends GhidraScript {
    private static final long QUEUE=0x3ff20000L, EVENT=0x3ff30000L, FORMAT=0x3ffe0300L;
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
        case 0x40081388:e.countBoundary("diagnostic_millis");e.register(10,1000);return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:e.countBoundary("diagnostic_output");return;
        case 0x400d05f4:case (int)FORMAT:
            e.countBoundary("event_diagnostic_formatter");e.write(e.register(11),0,1);return;
        default:throw new IllegalStateException(String.format("Unreviewed runtime event call %08x",target));
        }
    }
    @Override public void run()throws Exception {
        String[] args=getScriptArgs();if(args.length!=3)throw new IllegalArgumentException("Expected cases, verified layout and output");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();
        JsonObject layout=JsonParser.parseString(Files.readString(Path.of(args[1]))).getAsJsonObject();
        byte[] raw=Files.readAllBytes(Path.of(layout.get("input_path").getAsString()));
        if(!sha(raw).equals(Z119Execution.IMAGE_SHA))throw new IllegalStateException("Wrong original image");
        for(JsonElement value:layout.getAsJsonArray("segments")){
            JsonObject s=value.getAsJsonObject();int size=s.get("size").getAsInt();if(size==0)continue;
            byte[] loaded=new byte[size];currentProgram.getMemory().getBytes(toAddr(s.get("address").getAsLong()),loaded);
            int offset=s.get("offset").getAsInt();
            if(!Arrays.equals(loaded,Arrays.copyOfRange(raw,offset,offset+size)))throw new IllegalStateException("Original bytes differ");
        }
        List<Object> results=new ArrayList<>(),functions=new ArrayList<>();
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary)){
            for(JsonElement value:inputs){
                JsonArray w=value.getAsJsonObject().getAsJsonArray("words");
                e.resetRegisters();e.clear(QUEUE,0x1000);e.clear(EVENT,0x1000);
                e.write(0x3ffc4654L,0x705391adL,4); // Stable stack-protector guard.
                e.write(EVENT,EVENT+0x100,4);e.write(EVENT+0x100,FORMAT,4);
                for(int i=0;i<4;i++){
                    e.write(EVENT+4+i*4,w.get(i).getAsLong(),4);
                    e.write(QUEUE+8+i*4,0x44440000L+i,4);
                }
                e.write(QUEUE+0x18,w.get(4).getAsLong(),1);
                e.register(10,QUEUE);e.register(11,EVENT);e.invoke(0x400f45b4L);
                long pending=e.read(QUEUE+0x18,1);
                e.register(10,QUEUE);e.invoke(0x400f33d4L);
                if(e.register(10)!=QUEUE+4)throw new IllegalStateException("Wrong event view");
                List<Long> fields=new ArrayList<>();for(int i=0;i<4;i++)fields.add(e.read(QUEUE+8+i*4,4));
                results.add(row("fields",fields,"pending_after_put",pending,"pending_after_take",e.read(QUEUE+0x18,1)));
            }
            for(String address:e.functions.keySet()){
                Function function=currentProgram.getFunctionManager().getFunctionAt(toAddr(Long.parseLong(address.substring(2),16)));
                if(function==null)throw new IllegalStateException("Missing original function bounds");
                List<Object> ranges=new ArrayList<>();
                for(AddressRange range:function.getBody().getAddressRanges()){
                    int size=(int)range.getLength();byte[] bytes=new byte[size];currentProgram.getMemory().getBytes(range.getMinAddress(),bytes);
                    ranges.add(row("address",range.getMinAddress().getOffset(),"size",size,"sha256",sha(bytes)));
                }
                functions.add(row("address",address,"calls",e.functions.get(address),"ranges",ranges));
            }
            Files.writeString(Path.of(args[2]),new GsonBuilder().setPrettyPrinting().create().toJson(row(
                "input_sha256",Z119Execution.IMAGE_SHA,"cases",results,"original_functions",functions,
                "modeled_boundaries",e.boundaries,"scope","Original RuntimeEventQ four-field copy/drop-new/read; diagnostic formatting and single-thread memory ordering are boundaries.",
                "hardware_tested",false,"full_cpu_emulation",false))+"\n");
        }
        println("Executed "+results.size()+" original RuntimeEvent mailbox cases");
    }
}
