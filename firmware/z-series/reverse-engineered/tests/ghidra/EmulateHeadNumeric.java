// Original Head temperature/time/valve/quantity/strength input bodies.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;

public class EmulateHeadNumeric extends GhidraScript {
    private static final long HEAD=0x3ff20000L,SINK=0x7ff10000L;
    private static final long[] ENTRIES={0x4010722cL,0x4010730cL,0x40107aa0L,0x40107bb8L,0x401077f8L};
    private static final int[] PENDING={0x150c,0x159c,0x16bc,0x1760,0x162c};
    private final List<List<Long>> effects=new ArrayList<>();
    private long assertionLine;
    private static class OriginalAssertion extends RuntimeException {}
    private static String sha(byte[] bytes)throws Exception{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));}
    private static Map<String,Object> row(Object... values){Map<String,Object> r=new LinkedHashMap<>();for(int i=0;i<values.length;i+=2)r.put((String)values[i],values[i+1]);return r;}
    private void screen(long type){effects.add(List.of(type,0L,0L,0L,0L,0L));}
    private void boundary(Z119Execution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        switch((int)target){
        case 0x40081388:e.countBoundary("diagnostic_millis");e.register(10,1000);return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:e.countBoundary("diagnostic_output");return;
        case 0x400d10a0:e.countBoundary("diagnostic_recipe_summary");e.write(e.register(11),0,1);return;
        case 0x400db014:assertionLine=e.register(11);e.countBoundary("fatal_assertion");throw new OriginalAssertion();
        case 0x401367a0:throw new IllegalStateException("Original stack guard failed");
        case 0x4000c2c8:{
            long dest=e.register(10),source=e.register(11),size=e.register(12);
            if(size>4096)throw new IllegalStateException("Unbounded ROM memcpy");
            byte[] bytes=new byte[(int)size];for(int i=0;i<size;i++)bytes[i]=(byte)e.read(source+i,1);
            for(int i=0;i<size;i++)e.write(dest+i,bytes[i]&255L,1);
            e.countBoundary("ROM_memcpy");return;
        }
        case 0x4000c44c:{
            long dest=e.register(10),value=e.register(11),size=e.register(12);
            if(size>4096)throw new IllegalStateException("Unbounded ROM memset");
            for(int i=0;i<size;i++)e.write(dest+i,value,1);e.countBoundary("ROM_memset");return;
        }
        case 0x400f3d3c:{
            if(e.register(10)!=SINK)throw new IllegalStateException("Wrong Core command sink");
            long p=e.register(11);effects.add(List.of(0L,e.read(p+4,4),e.read(p+8,4),e.read(p+12,4),e.read(p+16,4),e.read(p+20,4)));
            e.countBoundary("Core_command_mailbox_put");return;
        }
        case 0x401070e8:e.countBoundary("Head_synchronize");screen(1);return;
        case 0x40106a90:e.countBoundary("manual_step_screen");screen(2);return;
        case 0x40105a4c:e.countBoundary("coffee_screen");screen(3);return;
        case 0x401051ec:
            if(e.register(11)!=HEAD+0xa118)throw new IllegalStateException("Wrong recipe reference");
            e.countBoundary("recipe_strength_screen");screen(4);return;
        case 0x400f409c:case 0x401bf444:case 0x400ef230:case 0x400ef214:case 0x400ef180:case 0x400ee998:
        case 0x400eefb4:case 0x400d0f80:case 0x400d1040:case 0x400d0ffc:case 0x400eed44:case 0x400eed28:e.invoke(target);return;
        default:throw new IllegalStateException(String.format("Unreviewed Head numeric call %08x",target));
        }
    }
    private int quantity(Z119Execution e,long p,JsonArray values,int index){for(int j=0;j<3;j++)e.write(p+j*4,values.get(index++).getAsLong(),4);return index;}
    private int parameters(Z119Execution e,long p,JsonArray values,int index){
        for(int j=0;j<3;j++)e.write(p+j*4,values.get(index++).getAsLong(),4);
        for(int off:new int[]{0x10,0x20,0x30,0x40})index=quantity(e,p+off,values,index);
        e.write(p+0x50,values.get(index++).getAsLong(),1);return index;
    }
    private void quantityOut(Z119Execution e,long p,List<Long> values)throws Exception{for(int j=0;j<3;j++)values.add(e.read(p+j*4,4));}
    private void parametersOut(Z119Execution e,long p,List<Long> values)throws Exception{
        for(int j=0;j<3;j++)values.add(e.read(p+j*4,4));
        for(int off:new int[]{0x10,0x20,0x30,0x40})quantityOut(e,p+off,values);
        values.add(e.read(p+0x50,1));
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
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary)){
            for(JsonElement value:inputs){
                JsonObject input=value.getAsJsonObject();JsonArray state=input.getAsJsonArray("state"),payload=input.getAsJsonArray("value");
                int op=input.get("operation").getAsInt(),i=0;e.resetRegisters();e.clear(HEAD,0xa300);effects.clear();assertionLine=0;
                e.write(0x3ffc4654L,0x5a61c0deL,4);e.write(HEAD+0x3868,SINK,4);
                e.write(HEAD+0xa0d8,state.get(i++).getAsLong(),1);e.write(HEAD+0xa0dc,state.get(i++).getAsLong(),4);
                for(int off:new int[]{0xa0e0,0xa0f0,0xa100})i=quantity(e,HEAD+off,state,i);
                for(int off=0xa110;off<0xa114;off++)e.write(HEAD+off,state.get(i++).getAsLong(),1);
                i=parameters(e,HEAD+0xa028,state,i);i=parameters(e,HEAD+0xa080,state,i);
                e.write(HEAD+0xa1c8,state.get(i++).getAsLong(),1);e.write(HEAD+0xa1c9,state.get(i++).getAsLong(),1);
                e.write(HEAD+PENDING[op],1,1);
                if(op==3){e.write(HEAD+0x174c,payload.get(0).getAsLong(),4);quantity(e,HEAD+0x1750,payload,1);}
                else {int[] offsets={0x1508,0x1598,0x16b8,0,0x1628};e.write(HEAD+offsets[op],payload.get(0).getAsLong(),4);}
                e.register(10,HEAD);try{e.invoke(ENTRIES[op]);}catch(OriginalAssertion expected){}
                List<Long> out=new ArrayList<>();out.add(e.read(HEAD+0xa0d8,1));out.add(e.read(HEAD+0xa0dc,4));
                for(int off:new int[]{0xa0e0,0xa0f0,0xa100})quantityOut(e,HEAD+off,out);
                for(int off=0xa110;off<0xa114;off++)out.add(e.read(HEAD+off,1));
                parametersOut(e,HEAD+0xa028,out);parametersOut(e,HEAD+0xa080,out);
                out.add(e.read(HEAD+0xa1c8,1));out.add(e.read(HEAD+0xa1c9,1));
                results.add(row("state",out,"assertion_line",assertionLine,"effects",new ArrayList<>(effects),"event_pending",e.read(HEAD+PENDING[op],1)));
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
                "modeled_boundaries",e.boundaries,"scope","Original Head numeric event bodies, CoreCommand construction, duration post helper and coffee-program check; ROM memory functions, command queue, diagnostics and screen presentation are explicit boundaries.",
                "hardware_tested",false,"full_cpu_emulation",false);
            Files.writeString(Path.of(args[2]),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+results.size()+" original Head numeric inputs");
    }
}
