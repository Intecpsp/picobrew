// Original Head encoder selection body and integer selector helper.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;

public class EmulateHeadEncoder extends GhidraScript {
    private static final long HEAD=0x3ff20000L,COMBINED_PARAGRAPH=0x7ff10000L,COMBINED_MENU=0x7ff20000L;
    private final List<List<Long>> effects=new ArrayList<>();
    private long assertionLine,clock;private boolean timerCall;
    private static class OriginalAssertion extends RuntimeException {}
    private static String sha(byte[] bytes)throws Exception{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));}
    private static Map<String,Object> row(Object... values){Map<String,Object> r=new LinkedHashMap<>();for(int i=0;i<values.length;i+=2)r.put((String)values[i],values[i+1]);return r;}
    private void effect(long type,long a,long b){effects.add(List.of(type,a&0xffffffffL,b&0xffffffffL));}
    private void boundary(Z119Execution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        switch((int)target){
        case 0x40081388:
            if(timerCall){effect(11,clock,0);e.register(10,clock);clock=(clock+17)&0xffffffffL;e.countBoundary("timer_millis");}
            else {e.register(10,777);e.countBoundary("diagnostic_millis");}return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:e.countBoundary("diagnostic_output");return;
        case 0x400db014:assertionLine=e.register(11);e.countBoundary("fatal_assertion");throw new OriginalAssertion();
        case 0x400d1cbc:timerCall=true;e.invoke(target);timerCall=false;return;
        case 0x400f1eb4:e.invoke(target);return;
        case 0x400eb07c:effect(0,0,0);e.countBoundary("menu_draw");return;
        case 0x400fdb54:effect(1,e.register(11),0);e.countBoundary("slideshow_select");return;
        case 0x400e9814:effect(2,0,0);e.countBoundary("paragraph_draw");return;
        case 0x400edd28:effect(3,0,0);e.countBoundary("character_draw");return;
        case 0x400e8aac:effect(4,0,0);e.countBoundary("temperature_draw");return;
        case 0x400eb49c:effect(5,0,0);e.countBoundary("duration_draw");return;
        case 0x400e9cac:effect(6,0,0);e.countBoundary("strength_draw");return;
        case 0x400f1e9c:{
            long widget=e.register(10)-HEAD-0x20;
            if(widget!=0x5cd0&&widget!=0x5d78)throw new IllegalStateException("Wrong quantity selector");
            effect(7,widget==0x5cd0?0:1,e.read(HEAD+widget+4,4));e.countBoundary("quantity_changed");return;
        }
        case 0x400ef3e4:
            if(e.register(10)!=HEAD+0x38a8||e.register(11)!=HEAD+0x9f78)throw new IllegalStateException("Wrong diagnostic paragraph");
            effect(8,e.register(12),0);e.countBoundary("diagnostic_paragraph");return;
        case 0x400e99d4:
            if(e.register(10)!=COMBINED_PARAGRAPH)throw new IllegalStateException("Wrong combined paragraph");
            effect(9,e.register(11),0);e.countBoundary("combined_paragraph_select");return;
        case 0x400eb0d4:
            if(e.register(10)!=COMBINED_MENU)throw new IllegalStateException("Wrong combined menu");
            effect(10,e.register(11),0);e.countBoundary("combined_menu_select");return;
        default:throw new IllegalStateException(String.format("Unreviewed Head encoder call %08x",target));
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
                e.resetRegisters();e.clear(HEAD,0xa200);effects.clear();assertionLine=0;timerCall=false;clock=input.get("clock").getAsLong();
                int i=0;for(JsonElement field:fields){JsonArray f=field.getAsJsonArray();e.write(HEAD+f.get(1).getAsLong(),state.get(i++).getAsLong(),f.get(2).getAsInt());}
                e.write(HEAD+0x5ac0,HEAD+0x10000,4);e.write(HEAD+0x5a50,HEAD+0x17000,4);
                for(int off:new int[]{0x5abc,0x5b28,0x5bd8,0x5c60})e.write(HEAD+off,HEAD+0x20000,4);
                e.write(HEAD+0x5b18,COMBINED_MENU,4);e.write(HEAD+0x5b1c,COMBINED_PARAGRAPH,4);
                e.write(HEAD+0x11f0,input.get("delta").getAsLong(),4);e.write(HEAD+0x11f4,1,1);
                e.register(10,HEAD);try{e.invoke(0x400fdd0cL);}catch(OriginalAssertion expected){}
                List<Long> out=new ArrayList<>();for(JsonElement field:fields){JsonArray f=field.getAsJsonArray();out.add(e.read(HEAD+f.get(1).getAsLong(),f.get(2).getAsInt()));}
                results.add(row("state",out,"assertion_line",assertionLine,"effects",new ArrayList<>(effects),"event_pending",e.read(HEAD+0x11f4,1)));
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
                "modeled_boundaries",e.boundaries,"scope","Original Head encoder body, quantity selection helper and timer reset; drawing, child slideshow/combined selection and diagnostic paragraph presentation are explicit boundaries.",
                "hardware_tested",false,"full_cpu_emulation",false);
            Files.writeString(Path.of(args[2]),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+results.size()+" original Head encoder cases");
    }
}
