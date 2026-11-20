// Original ScreenLibrary periodic dispatch and nested Still transient gate.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;

public class EmulateUiScreenTick extends GhidraScript {
    private static final long UI=0x3ff20000L;
    private final List<List<Long>> effects=new ArrayList<>();
    private static String sha(byte[] bytes)throws Exception{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));}
    private static Map<String,Object> row(Object... values){Map<String,Object> r=new LinkedHashMap<>();for(int i=0;i<values.length;i+=2)r.put((String)values[i],values[i+1]);return r;}
    private void effect(long type,long a,long b){effects.add(List.of(type,a&0xffffffffL,b&0xffffffffL));}
    private void check(Z119Execution e,long expected){if(e.register(10)!=expected)throw new IllegalStateException("Wrong widget instance");}
    private void boundary(Z119Execution e,long target)throws Exception{
        switch((int)target){
        case 0x400f181c:e.invoke(target);return;
        case 0x400f96f4:check(e,UI+0x265c);effect(0,0,0);e.countBoundary("runtime_state_draw");return;
        case 0x400f1774:check(e,UI+0x267c);effect(1,0,0);e.countBoundary("still_transient_draw");return;
        case 0x400e79f8:
            if(e.register(10)!=UI+0x12000&&e.register(10)!=UI+0x13000)throw new IllegalStateException("Wrong text scroll");
            effect(2,e.register(11),e.register(10)==UI+0x12000?1:0);e.countBoundary("menu_text_scroll_tick");return;
        case 0x400fe960:check(e,UI+0x10c8);effect(3,e.register(11),0);e.countBoundary("slideshow_tick");return;
        case 0x40102788:check(e,UI+0x2510);effect(4,e.register(11),0);e.countBoundary("pause_draw");return;
        case 0x400e89c4:check(e,UI+0x1054);effect(5,0,0);e.countBoundary("runtime_transient_draw");return;
        case 0x400f0ab4:check(e,UI+0x1080);effect(6,0,0);e.countBoundary("notification_draw");return;
        case 0x400e8b44:check(e,UI+0x2598);effect(7,0,0);e.countBoundary("waiting_draw");return;
        case 0x400e905c:check(e,UI+0x2770);effect(8,0,0);e.countBoundary("level_draw");return;
        case 0x400e95bc:
            if(e.register(10)!=UI+0x10000&&e.register(10)!=UI+0x21a0)throw new IllegalStateException("Wrong paragraph caret");
            effect(9,e.register(10)==UI+0x10000?1:0,0);e.countBoundary("scroll_caret_draw");return;
        default:throw new IllegalStateException(String.format("Unreviewed screen tick call %08x",target));
        }
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=3)throw new IllegalArgumentException("Expected cases, verified image layout and output JSON");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();
        JsonObject layout=JsonParser.parseString(Files.readString(Path.of(args[1]))).getAsJsonObject();
        byte[] raw=Files.readAllBytes(Path.of(layout.get("input_path").getAsString()));
        if(!sha(raw).equals(Z119Execution.IMAGE_SHA))throw new IllegalStateException("Wrong original image");
        for(JsonElement value:layout.getAsJsonArray("segments")){
            JsonObject s=value.getAsJsonObject();int size=s.get("size").getAsInt();if(size==0)continue;
            byte[] loaded=new byte[size];currentProgram.getMemory().getBytes(toAddr(s.get("address").getAsLong()),loaded);
            int offset=s.get("offset").getAsInt();if(!Arrays.equals(loaded,Arrays.copyOfRange(raw,offset,offset+size)))throw new IllegalStateException("Project differs from original bytes");
        }
        List<Object> results=new ArrayList<>(),functions=new ArrayList<>();
        JsonArray fields=layout.getAsJsonArray("fields");
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary)){
            for(JsonElement value:inputs){
                JsonObject input=value.getAsJsonObject();JsonArray state=input.getAsJsonArray("state");
                e.resetRegisters();e.clear(UI,0x2800);effects.clear();
                int i=0;for(JsonElement field:fields){JsonArray f=field.getAsJsonArray();e.write(UI+f.get(1).getAsLong(),state.get(i++).getAsLong(),f.get(2).getAsInt());}
                e.write(UI+0x2274,UI+0x10000,4);e.write(UI+0x2270,UI+0x11000,4);e.write(UI+0x1100c,UI+0x12000,4);e.write(UI+0x221c,UI+0x13000,4);
                e.register(10,UI);e.register(11,input.get("now").getAsLong());e.invoke(0x40103e44L);
                List<Long> out=new ArrayList<>();for(JsonElement field:fields){JsonArray f=field.getAsJsonArray();out.add(e.read(UI+f.get(1).getAsLong(),f.get(2).getAsInt()));}
                results.add(row("state",out,"effects",new ArrayList<>(effects)));
            }
            for(String address:e.functions.keySet()){
                Function function=currentProgram.getFunctionManager().getFunctionAt(toAddr(Long.parseLong(address.substring(2),16)));
                if(function==null)throw new IllegalStateException("Executed body lacks defined bounds");
                List<Object> ranges=new ArrayList<>();for(AddressRange range:function.getBody().getAddressRanges()){
                    byte[] bytes=new byte[(int)range.getLength()];currentProgram.getMemory().getBytes(range.getMinAddress(),bytes);
                    ranges.add(row("address",range.getMinAddress().getOffset(),"size",bytes.length,"sha256",sha(bytes)));
                }
                functions.add(row("address",address,"calls",e.functions.get(address),"ranges",ranges));
            }
            Map<String,Object> report=row("input_sha256",Z119Execution.IMAGE_SHA,"cases",results,"original_functions",functions,
                "modeled_boundaries",e.boundaries,"scope","Original ScreenLibrary tick and nested Still transient timer gate; drawing, menu text scroll and slideshow behavior are required service boundaries.",
                "hardware_tested",false,"full_cpu_emulation",false);
            Files.writeString(Path.of(args[2]),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+results.size()+" original ScreenLibrary tick cases");
    }
}
