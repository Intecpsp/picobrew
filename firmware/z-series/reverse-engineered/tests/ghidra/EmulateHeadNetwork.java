// Original Head Wi-Fi/Azure event routes and group-session menu body.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;

public class EmulateHeadNetwork extends GhidraScript {
    private static final long HEAD=0x3ff20000L,CORE=0x3ff60000L,LANG=0x3ff70000L,SINK=0x7ff10000L;
    private final List<Object> effects=new ArrayList<>();private long assertionLine;
    private static String sha(byte[] bytes)throws Exception{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));}
    private static Map<String,Object> row(Object... values){Map<String,Object> r=new LinkedHashMap<>();for(int i=0;i<values.length;i+=2)r.put((String)values[i],values[i+1]);return r;}
    private List<Long> string(Z119Execution e,long p)throws Exception{
        List<Long> out=new ArrayList<>();if(p==0)return out;
        for(int i=0;i<256;i++){long b=e.read(p+i,1);if(b==0)return out;out.add(b);}throw new IllegalStateException("Unbounded original text");
    }
    private void action(Z119Execution e,long kind,long first,long second,long third,long title,long text)throws Exception{
        effects.add(row("kind",kind,"first",first,"second",second,"third",third,"title",string(e,title),"text",string(e,text)));
    }
    private void effect(Z119Execution e,long kind,long first)throws Exception{action(e,kind,first,0,0,0,0);}
    private void boundary(Z119Execution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        switch((int)target){
        case 0x40081388:e.countBoundary("diagnostic_millis");e.register(10,1000);return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:e.countBoundary("diagnostic_output");return;
        case 0x400db014:
            assertionLine=e.register(11);if(assertionLine!=0x7d)throw new IllegalStateException("Unexpected original assertion "+assertionLine);
            e.invoke(target);return;
        case 0x401289a0:
            if(e.register(10)!=200)throw new IllegalStateException("Unexpected assertion delay");
            e.countBoundary("assertion_delay_200ms_prescribed_clock");return;
        case 0x401367a0:throw new IllegalStateException("Original stack guard failed");
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
        case 0x400d1e60:effect(e,0,0);e.countBoundary("Head_screen_reset");return;
        case 0x40104104:action(e,1,0,0,0,0,e.register(11));e.countBoundary("screen_append_line");return;
        case 0x40102d30:{
            long p=e.register(11),scroll=e.read(p+0x10,4);
            if(e.read(p+8,4)!=0||scroll==0||e.read(scroll+4,4)!=0||e.read(scroll+8,4)!=0||e.read(scroll+12,1)!=0)throw new IllegalStateException("Unexpected Wi-Fi error layout");
            action(e,2,e.read(p+4,4),e.read(scroll,4),0,e.read(p,4),0);e.countBoundary("Wifi_error_screen");return;
        }
        case 0x40103b1c:action(e,3,e.register(12),e.register(13),0,e.register(11),0);e.countBoundary("Wifi_scanning_message");return;
        case 0x40104c20:{long p=e.register(11);action(e,4,e.read(p+4,4),e.read(p+8,4),e.read(p+12,4),0,0);e.countBoundary("Wifi_scan_results");return;}
        case 0x400f9d5c:effect(e,5,e.register(11));e.countBoundary("screen_append_command");return;
        case 0x40104154:{
            long p=e.register(12);if(e.read(p+12,4)!=1||e.read(p+4,1)!=1)throw new IllegalStateException("Unexpected group message shape");
            action(e,6,e.register(13),e.read(p+4,1),e.read(p+8,4),e.register(11),e.read(e.read(p,4),4));e.countBoundary("group_session_screen");return;
        }
        case 0x40103858:effect(e,7,0);e.countBoundary("Head_home_screen");return;
        case 0x400f3d3c:
            if(e.register(10)!=SINK)throw new IllegalStateException("Wrong Core command sink");
            effect(e,8,e.read(e.register(11)+4,4));e.countBoundary("Core_command_post");return;
        case 0x400ef230:case 0x400ef214:case 0x400ef180:case 0x400ee998:case 0x400eefb4:case 0x400d0f80:
        case 0x400d1040:case 0x400d0ffc:case 0x400eed44:case 0x400eed28:case 0x400f4424:case 0x400d1c20:
        case 0x400d283c:case 0x400d27ac:case 0x40105e50:case 0x400defa4:case 0x400def90:case 0x400dee30:case 0x401bf40c:case 0x401045b8:e.invoke(target);return;
        default:throw new IllegalStateException(String.format("Unreviewed Head network call %08x",target));
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
                JsonObject input=value.getAsJsonObject();JsonArray state=input.getAsJsonArray("state"),payload=input.getAsJsonArray("payload");
                e.resetRegisters();e.clear(HEAD,0xa300);effects.clear();assertionLine=0;e.write(0x3ffc4654L,0x5a61c0deL,4);
                e.write(HEAD+0x3868,SINK,4);e.write(HEAD+0x387c,CORE,4);e.write(CORE+4,input.get("core").getAsLong(),4);
                e.write(HEAD+0x3880,LANG,4);e.write(LANG,input.get("language").getAsLong(),4);parameters(e,HEAD+0xa028,state,0);
                boolean wifi=input.get("operation").getAsInt()==0;long pending=HEAD+(wifi?0x150:0x10c8);
                if(wifi){
                    e.write(HEAD+0x144,input.get("event").getAsLong(),4);e.write(HEAD+0x148,input.get("argument").getAsLong(),4);e.write(HEAD+0x14c,input.get("second").getAsLong(),4);
                }else{
                    e.write(HEAD+0x1064,input.get("event").getAsLong(),4);e.write(HEAD+0x1068,input.get("argument").getAsLong(),4);parameters(e,HEAD+0x1070,payload,0);
                }
                e.write(pending,1,1);e.register(10,HEAD);e.invoke(wifi?0x401094c8L:0x40105fbcL);
                List<Long> out=new ArrayList<>();parametersOut(e,HEAD+0xa028,out);
                results.add(row("state",out,"assertion_line",assertionLine,"effects",new ArrayList<>(effects),"event_pending",e.read(pending,1)));
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
                "modeled_boundaries",e.boundaries,"scope","Original Head Wi-Fi/Azure handlers, group-session menu body, quantity normalization, title lookup and event/CoreCommand copying. Screen reset/append/presentation, home, scan results, Core posting, compiler arithmetic and diagnostics are explicit boundaries.",
                "hardware_tested",false,"full_cpu_emulation",false);
            Files.writeString(Path.of(args[2]),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+results.size()+" original Head network events");
    }
}
