// Original Z119 Head runtime and encoder-button event bodies.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;

public class EmulateHeadEvents extends GhidraScript {
    private static final long HEAD=0x3ff20000L, STATE=0x3ff70000L;
    private final List<List<Long>> effects=new ArrayList<>();
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
        case 0x400f33d4:case 0x400f077c:e.invoke(target);return;
        case 0x40081388:e.countBoundary("diagnostic_millis");e.register(10,1000);return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:e.countBoundary("diagnostic_output");return;
        case 0x400deb58:e.countBoundary("Head_alarm");effects.add(List.of(0L,e.register(11)));return;
        case 0x40106c48:e.countBoundary("running_screen");effects.add(List.of(1L,0L));return;
        case 0x40103998:e.countBoundary("notification_screen");effects.add(List.of(2L,e.register(11)));return;
        case 0x400fd064:
            if(e.register(10)!=HEAD+0x38a8)throw new IllegalStateException("Wrong ScreenLibrary instance");
            e.countBoundary("ScreenLibrary_button");effects.add(List.of(3L,0L));return;
        case 0x401070e8:e.countBoundary("Head_synchronize");effects.add(List.of(4L,0L));return;
        default:throw new IllegalStateException(String.format("Unreviewed Head event call %08x",target));
        }
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
                JsonObject input=value.getAsJsonObject();JsonArray w=input.getAsJsonArray("words");
                e.resetRegisters();e.clear(HEAD,0xa300);e.clear(STATE,0x1000);effects.clear();
                e.write(HEAD+0x387c,STATE,4);
                e.write(STATE+0x2f4,w.get(0).getAsLong(),4);e.write(STATE+0x36c,w.get(1).getAsLong(),4);
                e.write(STATE+0x2fc,w.get(2).getAsLong(),4);e.write(STATE+0x380,w.get(3).getAsLong(),4);
                e.write(HEAD+0xa1ec,w.get(4).getAsLong(),4);e.write(HEAD+0x9f98,w.get(5).getAsLong(),1);
                e.write(HEAD+0xa8,w.get(6).getAsLong(),4);e.write(HEAD+0xb4,w.get(7).getAsLong(),4);e.write(HEAD+0xb8,1,1);
                e.write(HEAD+0x115c,w.get(8).getAsLong(),1);e.write(HEAD+0x1164,1,1);
                boolean runtime=input.get("operation").getAsInt()==0;
                e.register(10,HEAD);e.invoke(runtime?0x40107984L:0x4010718cL);
                results.add(row("notification",e.read(HEAD+0xa1ec,4),"hijacked",e.read(HEAD+0x9f98,1),
                    "event_pending",e.read(HEAD+(runtime?0xb8:0x1164),1),"effects",new ArrayList<>(effects)));
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
                "modeled_boundaries",e.boundaries,"scope","Original Head runtime/button bodies and one-slot mailbox reads; alarm timers, screen presentation and diagnostics are explicit boundaries.",
                "hardware_tested",false,"full_cpu_emulation",false);
            Files.writeString(Path.of(args[2]),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+results.size()+" original Head runtime/button events");
    }
}
