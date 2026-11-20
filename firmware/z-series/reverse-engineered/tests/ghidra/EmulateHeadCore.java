// Original Core-event, state presentation and Still mailbox continuations.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;

public class EmulateHeadCore extends GhidraScript {
    private static final long HEAD=0x3ff20000L,CORE=0x3ff60000L,OWNER=0x3ff64000L,LANG=0x3ff70000L,DISPLAY=0x3ff74000L,SINK=0x7ff10000L;
    private final List<Object> effects=new ArrayList<>();private long assertionLine,clock,rinse;
    private static String sha(byte[] bytes)throws Exception{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));}
    private static Map<String,Object> row(Object... values){Map<String,Object> r=new LinkedHashMap<>();for(int i=0;i<values.length;i+=2)r.put((String)values[i],values[i+1]);return r;}
    private List<Long> string(Z119Execution e,long p)throws Exception{List<Long> out=new ArrayList<>();if(p==0)return out;for(int i=0;i<256;i++){long b=e.read(p+i,1);if(b==0)return out;out.add(b);}throw new IllegalStateException("Unbounded original text");}
    private void action(Z119Execution e,long kind,long first,long second,long third,long title,long text,long event)throws Exception{
        String digest="";if(event!=0){byte[] bytes=new byte[0xdec];for(int i=0;i<bytes.length;i++)bytes[i]=(byte)e.read(event+4+i,1);digest=sha(bytes);}
        effects.add(row("kind",kind,"first",first,"second",second,"third",third,"title",string(e,title),"text",string(e,text),"event",digest));
    }
    private void effect(Z119Execution e,long kind,long first)throws Exception{action(e,kind,first,0,0,0,0,0);}
    private void boundary(Z119Execution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        switch((int)target){
        case 0x40081388:e.countBoundary("prescribed_millis");e.register(10,clock);return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:e.countBoundary("diagnostic_output");return;
        case 0x400d5a44:case 0x400d5a68:e.countBoundary("diagnostic_scope");return;
        case 0x400db014:assertionLine=e.register(11);e.invoke(target);return;
        case 0x401289a0:if(e.register(10)!=200)throw new IllegalStateException("Unexpected assertion delay");e.countBoundary("assertion_delay_200ms_prescribed_clock");return;
        case 0x401367a0:throw new IllegalStateException("Original stack guard failed");
        case 0x4000c2c8:{long d=e.register(10),s=e.register(11),size=e.register(12);if(size>4096)throw new IllegalStateException("Unbounded ROM memcpy");byte[] bytes=new byte[(int)size];for(int i=0;i<size;i++)bytes[i]=(byte)e.read(s+i,1);for(int i=0;i<size;i++)e.write(d+i,bytes[i]&255L,1);e.countBoundary("ROM_memcpy");return;}
        case 0x4000c44c:{long d=e.register(10),v=e.register(11),size=e.register(12);if(size>4096)throw new IllegalStateException("Unbounded ROM memset");for(int i=0;i<size;i++)e.write(d+i,v,1);e.countBoundary("ROM_memset");return;}
        case 0x400d1e60:effect(e,0,0);e.countBoundary("Head_screen_reset");return;
        case 0x400f9d5c:effect(e,1,e.register(11));e.countBoundary("screen_append_command");return;
        case 0x400fa064:action(e,2,e.register(11),0,0,0,e.register(12),0);e.countBoundary("screen_append_command_text");return;
        case 0x40104238:action(e,3,e.register(11),e.register(12),0,0,0,0);e.countBoundary("instruction_menu");return;
        case 0x401074f8:effect(e,4,e.register(11));e.countBoundary("dispatch_Core_command");return;
        case 0x400e7b90:effect(e,5,0);e.countBoundary("boot_logo");return;
        case 0x40103b1c:action(e,6,e.register(13),e.register(14),0,e.register(11),e.register(12),0);e.countBoundary("waiting_message");return;
        case 0x40103aa4:action(e,7,0,0,0,0,e.register(11),0);e.countBoundary("registration_screen");return;
        case 0x401045dc:effect(e,8,0);e.countBoundary("network_configuration");return;
        case 0x400ef2a4:effect(e,10,0);e.countBoundary("reset_UI");return;
        case 0x400d1b6c:effect(e,11,0);e.countBoundary("clear_display");return;
        case 0x40103858:effect(e,12,0);e.countBoundary("home_screen");return;
        case 0x40104294:effect(e,13,0);e.countBoundary("session_start_screen");return;
        case 0x40106c48:effect(e,14,0);e.countBoundary("running_screen");return;
        case 0x40106314:effect(e,15,0);e.countBoundary("system_error_screen");return;
        case 0x40106480:effect(e,16,0);e.countBoundary("loading_error_screen");return;
        case 0x40106508:effect(e,17,0);e.countBoundary("network_error_screen");return;
        case 0x401053f8:action(e,18,0,0,0,0,0,e.register(11));e.countBoundary("recipe_results");return;
        case 0x4010476c:action(e,19,0,0,0,0,0,e.register(11));e.countBoundary("program_loaded");return;
        case 0x40105964:action(e,20,0,0,0,0,0,e.register(11));e.countBoundary("boiler_results");return;
        case 0x40104104:action(e,22,0,0,0,0,e.register(11),0);e.countBoundary("append_line");return;
        case 0x7ff10000:effect(e,24,e.register(11));e.register(10,rinse);e.countBoundary("maintenance_rinse_query");return;
        case 0x40102d30:{
            long p=e.register(11),layout=e.read(p+4,4);
            if(layout==15){action(e,9,e.read(p+32,2),e.read(p+35,1),0,e.read(p,4),0,0);}
            else if(layout==21){long text=e.read(p+20,4);if(text==0||e.read(text+8,4)!=1||e.read(text+12,4)!=1)throw new IllegalStateException("Expired-registration text differs");action(e,21,layout,e.read(p+8,4),e.read(text+4,1),0,0,0);}
            else if(layout==6){long scroll=e.read(p+16,4);if(scroll==0||e.read(scroll+4,4)!=0||e.read(scroll+8,4)!=0||e.read(scroll+12,1)!=0)throw new IllegalStateException("Core error scroll differs");action(e,23,layout,e.read(scroll,4),0,e.read(p,4),0,0);}
            else throw new IllegalStateException("Unexpected original layout "+layout);
            e.countBoundary("UI_screen_composition");return;
        }
        case 0x40106e84:case 0x4010426c:case 0x400deb2c:case 0x400d1cbc:case 0x400d1c20:
        case 0x400d283c:case 0x400d26d8:case 0x400d27ac:case 0x400d2648:e.invoke(target);return;
        default:throw new IllegalStateException(String.format("Unreviewed Head Core call %08x",target));
        }
    }
    private void putString(Z119Execution e,long p,String text){byte[] bytes=text.getBytes(java.nio.charset.StandardCharsets.UTF_8);for(int i=0;i<bytes.length;i++)e.write(p+i,bytes[i]&255L,1);e.write(p+bytes.length,0,1);}
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=3)throw new IllegalArgumentException("Expected cases, verified image layout and output JSON");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();JsonObject layout=JsonParser.parseString(Files.readString(Path.of(args[1]))).getAsJsonObject();byte[] raw=Files.readAllBytes(Path.of(layout.get("input_path").getAsString()));
        if(!sha(raw).equals(Z119Execution.IMAGE_SHA))throw new IllegalStateException("Wrong original image");
        for(JsonElement value:layout.getAsJsonArray("segments")){JsonObject s=value.getAsJsonObject();int size=s.get("size").getAsInt();if(size==0)continue;byte[] loaded=new byte[size];currentProgram.getMemory().getBytes(toAddr(s.get("address").getAsLong()),loaded);int offset=s.get("offset").getAsInt();if(!Arrays.equals(loaded,Arrays.copyOfRange(raw,offset,offset+size)))throw new IllegalStateException("Project differs from original bytes");}
        List<Object> results=new ArrayList<>(),functions=new ArrayList<>();int caseIndex=0;
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary)){
            for(JsonElement value:inputs){
                JsonObject input=value.getAsJsonObject();JsonArray state=input.getAsJsonArray("state");e.resetRegisters();e.clear(HEAD,0xa300);e.clear(CORE,0x1200);e.clear(OWNER,0x900);e.clear(DISPLAY,0x2100);effects.clear();assertionLine=0;clock=state.get(14).getAsLong()&0xffffffffL;rinse=state.get(13).getAsLong();e.write(0x3ffc4654L,0x5a61c0deL,4);e.write(0x3ffc2e84L,state.get(15).getAsLong(),4);
                e.write(HEAD+0x387c,CORE,4);e.write(HEAD+0x3878,OWNER,4);e.write(HEAD+0x3880,LANG,4);e.write(LANG,input.get("language").getAsLong(),4);e.write(HEAD+0x38b0,DISPLAY,4);e.write(HEAD+0x17f4,LANG+0x100,4);e.write(LANG+0x100,LANG+0x200,4);e.write(LANG+0x208,SINK,4);
                e.write(CORE+0x2f4,state.get(0).getAsLong(),4);e.write(OWNER+0x858,state.get(1).getAsLong(),4);e.write(OWNER+0x85c,state.get(2).getAsLong(),4);e.write(CORE+0x2a4,state.get(3).getAsLong(),4);e.write(CORE+0x29c,state.get(4).getAsLong(),4);e.write(CORE+0x100,state.get(5).getAsLong(),1);e.write(CORE+0x2a0,state.get(6).getAsLong(),4);
                e.write(HEAD+0x9f80,state.get(7).getAsLong(),1);e.write(HEAD+0x9f84,state.get(8).getAsLong(),4);e.write(HEAD+0x9f88,state.get(9).getAsLong(),4);e.write(HEAD+0x17ec,state.get(10).getAsLong(),4);e.write(HEAD+0xa021,state.get(11).getAsLong(),1);e.write(DISPLAY+0x205c,state.get(12).getAsLong(),1);putString(e,CORE+0x224,input.get("token").getAsString());putString(e,CORE+0x814,input.get("network").getAsString());
                byte[] event=HexFormat.of().parseHex(input.get("event").getAsString());long pending;List<Long> still=new ArrayList<>(Arrays.asList(0L,0L));
                try{
                    if(input.get("operation").getAsInt()==2){
                        e.write(HEAD+8,((event[4]&255L)|((event[5]&255L)<<8)|((event[6]&255L)<<16)|((event[7]&255L)<<24)),4);e.write(HEAD+12,((event[8]&255L)|((event[9]&255L)<<8)|((event[10]&255L)<<16)|((event[11]&255L)<<24)),4);pending=HEAD+0x10;e.write(pending,input.get("pending").getAsLong(),1);e.write(HEAD+0x91,input.get("debug").getAsLong(),1);e.register(10,HEAD);e.invoke(0x400f41ecL);long retained=e.register(10);still.set(0,e.read(retained+4,4));still.set(1,e.read(retained+8,4));
                    }else{for(int i=0;i<event.length;i++)e.write(HEAD+0x1e0+i,event[i]&255L,1);pending=HEAD+0xfd0;e.write(pending,input.get("pending").getAsLong(),1);e.write(HEAD+0x1051,input.get("debug").getAsLong(),1);e.register(10,HEAD);e.invoke(0x401095b4L);}
                }catch(Exception ex){throw new IllegalStateException("Case "+caseIndex+" failed (operation "+input.get("operation")+")",ex);}
                results.add(row("state",Arrays.asList(e.read(HEAD+0x9f80,1),e.read(HEAD+0x9f84,4),e.read(HEAD+0x9f88,4),e.read(HEAD+0xa021,1),e.read(DISPLAY+0x205c,1)),"assertion_line",assertionLine,"assertion_counter",e.read(0x3ffc2e84L,4),"effects",new ArrayList<>(effects),"event_pending",e.read(pending,1),"still",still));caseIndex++;
            }
            for(String address:e.functions.keySet()){Function function=currentProgram.getFunctionManager().getFunctionAt(toAddr(Long.parseLong(address.substring(2),16)));if(function==null)throw new IllegalStateException("Executed body lacks defined bounds");List<Object> ranges=new ArrayList<>();for(AddressRange range:function.getBody().getAddressRanges()){byte[] bytes=new byte[(int)range.getLength()];currentProgram.getMemory().getBytes(range.getMinAddress(),bytes);ranges.add(row("address",range.getMinAddress().getOffset(),"size",bytes.length,"sha256",sha(bytes)));}functions.add(row("address",address,"calls",e.functions.get(address),"ranges",ranges));}
            Map<String,Object> report=row("input_sha256",Z119Execution.IMAGE_SHA,"cases",results,"original_functions",functions,"modeled_boundaries",e.boundaries,"scope","Original Core-event consumer, state presentation, Still mailbox read, instruction-menu routing, title lookup, timers and returning assertion body. Presentation/helper bodies, Core dispatch, maintenance query, compiler arithmetic, ROM copying, clock and diagnostic output are explicit boundaries.","hardware_tested",false,"full_cpu_emulation",false);
            Files.writeString(Path.of(args[2]),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+results.size()+" original Head Core/Still cases");
    }
}
