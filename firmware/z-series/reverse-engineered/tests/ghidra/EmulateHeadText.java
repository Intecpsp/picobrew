// Original Head text event routing and original fixed-buffer copies.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;

public class EmulateHeadText extends GhidraScript {
    private static final long HEAD=0x3ff20000L,SINK=0x7ff10000L;
    private final List<List<Long>> effects=new ArrayList<>();
    private JsonObject input;
    private static String sha(byte[] bytes)throws Exception{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));}
    private static Map<String,Object> row(Object... values){Map<String,Object> r=new LinkedHashMap<>();for(int i=0;i<values.length;i+=2)r.put((String)values[i],values[i+1]);return r;}
    private List<Long> zeros(){List<Long> r=new ArrayList<>();for(int i=0;i<120;i++)r.add(0L);return r;}
    private long length(Z119Execution e,long pointer)throws Exception{for(int i=0;i<65;i++)if(e.read(pointer+i,1)==0)return i;throw new IllegalStateException("Unbounded original strlen");}
    private void boundary(Z119Execution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        switch((int)target){
        case 0x40081388:e.countBoundary("diagnostic_millis");e.register(10,1000);return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:e.countBoundary("diagnostic_output");return;
        case 0x400db014:throw new IllegalStateException("Unexpected original assertion "+e.register(11));
        case 0x401367a0:throw new IllegalStateException("Original stack guard failed");
        case 0x400014c0:{long n=length(e,e.register(10));e.register(10,n);e.countBoundary("ROM_strlen");return;}
        case 0x400566c4:{
            long pointer=e.register(10),n=length(e,pointer);JsonArray text=input.getAsJsonArray("text");
            for(int i=0;i<=n;i++)if(e.read(pointer+i,1)!=text.get(i).getAsLong())throw new IllegalStateException("Wrong atoi input");
            e.register(10,input.get("atoi").getAsLong());e.countBoundary("ROM_atoi_representable_signed32");return;
        }
        case 0x4000c2c8:{
            long dest=e.register(10),source=e.register(11),size=e.register(12);
            if(size>4096)throw new IllegalStateException("Unbounded ROM memcpy");
            byte[] bytes=new byte[(int)size];for(int i=0;i<size;i++)bytes[i]=(byte)e.read(source+i,1);
            for(int i=0;i<size;i++)e.write(dest+i,bytes[i]&255L,1);e.countBoundary("ROM_memcpy");return;
        }
        case 0x4000c44c:{
            long dest=e.register(10),value=e.register(11),size=e.register(12);
            if(size>4096)throw new IllegalStateException("Unbounded ROM memset");
            for(int i=0;i<size;i++)e.write(dest+i,value,1);e.countBoundary("ROM_memset");return;
        }
        case 0x400f3d3c:{
            if(e.register(10)!=SINK)throw new IllegalStateException("Wrong Core command sink");
            long p=e.register(11),kit=e.read(p+0xa8,4);List<Long> result=zeros();result.set(1,e.read(p+4,4));result.set(2,e.read(p+0x9c,4));result.set(3,kit==0?0L:1L);
            for(int i=0;i<33;i++)result.set(4+i,e.read(p+0x18+i,1));
            for(int i=0;i<65;i++)result.set(37+i,e.read(p+0x48+i,1));result.set(102,e.read(p+0x98,1));
            if(kit!=0){if(kit!=HEAD+0xa1cc)throw new IllegalStateException("Wrong kit-code storage");for(int i=0;i<17;i++)result.set(103+i,e.read(kit+i,1));}
            effects.add(result);e.countBoundary("Core_command_mailbox_put");return;
        }
        case 0x401070e8:{List<Long> result=zeros();result.set(0,1L);effects.add(result);e.countBoundary("Head_synchronize");return;}
        case 0x400ef230:case 0x400ef214:case 0x400ef180:case 0x400ee998:case 0x400eefb4:case 0x400d0f80:
        case 0x400d1040:case 0x400d0ffc:case 0x400eed44:case 0x400eed28:case 0x40100954:case 0x40100938:
        case 0x40100830:case 0x401007f0:case 0x400f82e8:case 0x400ef160:case 0x400f82cc:case 0x400f81c4:case 0x400f8190:e.invoke(target);return;
        default:throw new IllegalStateException(String.format("Unreviewed Head text call %08x",target));
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
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary)){
            for(JsonElement value:inputs){
                input=value.getAsJsonObject();JsonArray state=input.getAsJsonArray("state"),text=input.getAsJsonArray("text");
                e.resetRegisters();e.clear(HEAD,0xa300);effects.clear();e.write(0x3ffc4654L,0x5a61c0deL,4);e.write(HEAD+0x3868,SINK,4);
                int i=0;e.write(HEAD+0xa020,state.get(i++).getAsLong(),1);
                for(int j=0;j<33;j++)e.write(HEAD+0x9f9c+j,state.get(i++).getAsLong(),1);
                for(int j=0;j<65;j++)e.write(HEAD+0x9fcc+j,state.get(i++).getAsLong(),1);
                e.write(HEAD+0xa01c,state.get(i++).getAsLong(),1);
                for(int j=0;j<17;j++)e.write(HEAD+0xa1cc+j,state.get(i++).getAsLong(),1);
                long kitLength=state.get(i++).getAsLong();e.write(HEAD+0xa1e0,kitLength,4);e.write(HEAD+0xa1e4,16-kitLength,4);e.write(HEAD+0xa1e8,state.get(i++).getAsLong(),1);
                for(int j=0;j<65;j++)e.write(HEAD+0x1428+j,text.get(j).getAsLong(),1);
                e.write(HEAD+0x1478,input.get("command").getAsLong(),4);e.write(HEAD+0x147c,1,1);
                e.register(10,HEAD);e.invoke(0x40107578L);
                List<Long> out=new ArrayList<>();out.add(e.read(HEAD+0xa020,1));
                for(int j=0;j<33;j++)out.add(e.read(HEAD+0x9f9c+j,1));for(int j=0;j<65;j++)out.add(e.read(HEAD+0x9fcc+j,1));out.add(e.read(HEAD+0xa01c,1));
                for(int j=0;j<17;j++)out.add(e.read(HEAD+0xa1cc+j,1));out.add(e.read(HEAD+0xa1e0,4));out.add(e.read(HEAD+0xa1e8,1));
                results.add(row("state",out,"effects",new ArrayList<>(effects),"event_pending",e.read(HEAD+0x147c,1)));
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
                "modeled_boundaries",e.boundaries,"scope","Original Head text event body, CoreCommand constructors, Buffer16/Buffer64 clear/append operations; ROM memory/strlen/atoi, command delivery, diagnostics and synchronization are explicit boundaries. ROM atoi fixtures are limited to representable signed32 results.",
                "hardware_tested",false,"full_cpu_emulation",false);
            Files.writeString(Path.of(args[2]),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+results.size()+" original Head text inputs");
    }
}
