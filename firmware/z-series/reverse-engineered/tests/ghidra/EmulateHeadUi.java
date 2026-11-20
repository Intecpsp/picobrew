// Original 176-ID Head UI dispatcher, payload getters and returning assertions.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;

public class EmulateHeadUi extends GhidraScript {
    private static final long HEAD=0x3ff20000L,CORE=0x3ff60000L,LANG=0x3ff70000L,EEPROM=0x3ff71000L,SINK=0x7ff10000L;
    private final List<Object> effects=new ArrayList<>();private long assertionLine,clock;
    private static String sha(byte[] bytes)throws Exception{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));}
    private static Map<String,Object> row(Object... values){Map<String,Object> r=new LinkedHashMap<>();for(int i=0;i<values.length;i+=2)r.put((String)values[i],values[i+1]);return r;}
    private byte[] bytes(Z119Execution e,long p,int count)throws Exception{byte[] bytes=new byte[count];for(int i=0;i<count;i++)bytes[i]=(byte)e.read(p+i,1);return bytes;}
    private List<Long> string(Z119Execution e,long p)throws Exception{List<Long> out=new ArrayList<>();if(p==0)return out;for(int i=0;i<256;i++){long b=e.read(p+i,1);if(b==0)return out;out.add(b);}throw new IllegalStateException("Unbounded original text");}
    private void action(Z119Execution e,long kind,long first,long second,long third,long title,long text,List<Long> screen,List<Long> command,List<Long> search,long recipe,long event)throws Exception{
        effects.add(row("kind",kind,"first",first,"second",second,"third",third,"title",string(e,title),"text",string(e,text),"screen",screen,"command",command,"search",search,"recipe",recipe==0?"":sha(bytes(e,recipe,176)),"event",event==0?"":sha(bytes(e,event+4,276))));
    }
    private void basic(Z119Execution e,long kind,long first,long second,long third,long title,long text)throws Exception{action(e,kind,first,second,third,title,text,List.of(),List.of(),List.of(),0,0);}
    private void effect(Z119Execution e,long kind,long first)throws Exception{basic(e,kind,first,0,0,0,0);}
    private void eventEffect(Z119Execution e,long kind,long event)throws Exception{action(e,kind,0,0,0,0,0,List.of(),List.of(),List.of(),0,event);}
    private void boundary(Z119Execution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        switch((int)target){
        case 0x40081388:e.countBoundary("prescribed_millis");e.register(10,clock);return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:e.countBoundary("diagnostic_output");return;
        case 0x400db014:assertionLine=e.register(11);e.invoke(target);return;
        case 0x401289a0:if(e.register(10)!=200)throw new IllegalStateException("Unexpected assertion delay");e.countBoundary("assertion_delay_200ms_prescribed_clock");return;
        case 0x401367a0:throw new IllegalStateException("Original stack guard failed");
        case 0x4000c2c8:{long d=e.register(10),s=e.register(11),size=e.register(12);if(size>4096)throw new IllegalStateException("Unbounded ROM memcpy");byte[] b=bytes(e,s,(int)size);for(int i=0;i<size;i++)e.write(d+i,b[i]&255L,1);e.countBoundary("ROM_memcpy");return;}
        case 0x4000c44c:{long d=e.register(10),v=e.register(11),size=e.register(12);if(size>20000)throw new IllegalStateException("Unbounded ROM memset");if(d==HEAD+0x6058&&size==0x3f10&&v==0)effect(e,6,0);for(int i=0;i<size;i++)e.write(d+i,v,1);e.countBoundary("ROM_memset");return;}
        case 0x400deb58:effect(e,0,e.register(11));e.countBoundary("Head_alarm");return;
        case 0x401074f8:effect(e,1,e.register(11));e.countBoundary("Head_Core_dispatch");return;
        case 0x400f3d3c:{
            if(e.register(10)!=SINK)throw new IllegalStateException("Wrong Core sink");long p=e.register(11),id=e.read(p+4,4),data=p+0x1e0,flags=e.read(data,1),search=e.read(p+0xa8,4);
            List<Long> command=new ArrayList<>(Arrays.asList(id,e.read(p+8,4),e.read(p+16,4),e.read(p+0xa0,4),e.read(p+0xa4,4),flags,search==0?0L:1L));
            if(id==54)manualOut(e,p+0x1a0,command);if((flags&8)!=0)parametersOut(e,data+0xb8,command);
            action(e,2,0,0,0,0,0,List.of(),command,string(e,search),(flags&4)!=0?data+8:0,0);e.countBoundary("Core_command_post");return;
        }
        case 0x401070e8:effect(e,3,0);e.countBoundary("Head_synchronize");return;
        case 0x40103b1c:basic(e,4,e.register(13),e.register(14),0,e.register(11),e.register(12));e.countBoundary("waiting_message");return;
        case 0x4010775c:effect(e,5,0);e.countBoundary("Head_back");return;
        case 0x400d1e60:effect(e,6,0);e.countBoundary("Head_screen_reset");return;
        case 0x400f9d5c:effect(e,7,e.register(11));e.countBoundary("append_command");return;
        case 0x400fa064:basic(e,8,e.register(11),0,0,0,e.register(12));e.countBoundary("append_command_text");return;
        case 0x401036ac:basic(e,9,e.register(11),e.register(13),0,e.register(12),0);e.countBoundary("menu_screen");return;
        case 0x40104238:basic(e,10,e.register(11),e.register(12),0,0,0);e.countBoundary("instruction_menu");return;
        case 0x40102d30:{
            long p=e.register(11),input=e.read(p+24,4);List<Long> info=new ArrayList<>(Arrays.asList(e.read(p+4,4),e.read(p+8,4),e.read(p+64,4),e.read(p+12,4),e.read(p+32,2),e.read(p+36,4),e.read(p+40,4)));quantityOut(e,p+48,info);info.add(input==0?0L:e.read(input,4));info.add(input==0?0L:e.read(input+4,1));
            action(e,11,0,0,0,e.read(p,4),0,info,List.of(),List.of(),0,0);e.countBoundary("UI_screen_composition");return;
        }
        case 0x401073bc:effect(e,12,0);e.countBoundary("wifi_connect");return;
        case 0x4010377c:effect(e,13,0);e.countBoundary("coffee_menu");return;
        case 0x40107e20:effect(e,14,0);e.countBoundary("about_screen");return;
        case 0x401045dc:effect(e,15,0);e.countBoundary("network_configuration");return;
        case 0x401043fc:effect(e,16,0);e.countBoundary("ethernet_configuration");return;
        case 0x401048a4:effect(e,17,0);e.countBoundary("slideshow_cycle_configuration");return;
        case 0x401065b4:effect(e,18,0);e.countBoundary("wifi_details");return;
        case 0x40103bdc:eventEffect(e,19,e.register(11));e.countBoundary("wifi_key");return;
        case 0x40105ccc:effect(e,20,e.register(11));e.countBoundary("coffee_tweak");return;
        case 0x40106a90:effect(e,21,0);e.countBoundary("manual_step_screen");return;
        case 0x40104910:effect(e,22,0);e.countBoundary("units_configuration");return;
        case 0x40107414:effect(e,23,e.register(11));e.countBoundary("set_imperial");return;
        case 0x40107438:effect(e,24,e.register(11));e.countBoundary("set_static_cycle");return;
        case 0x4010745c:effect(e,25,e.register(11));e.countBoundary("set_language");return;
        case 0x40107480:effect(e,26,e.register(11));e.countBoundary("set_slideshow_detail");return;
        case 0x40105d84:effect(e,27,0);e.countBoundary("still_logging_configuration");return;
        case 0x401074a4:effect(e,28,e.register(11));e.countBoundary("set_still_logging");return;
        case 0x40104970:effect(e,29,0);e.countBoundary("azure_configuration");return;
        case 0x401074c8:effect(e,30,e.register(11));e.countBoundary("set_azure_mode");return;
        case 0x40104524:effect(e,31,0);e.countBoundary("boiler_configuration");return;
        case 0x401051ec:action(e,32,0,0,0,0,0,List.of(),List.of(),List.of(),e.register(11),0);e.countBoundary("recipe_strength_screen");return;
        case 0x40103728:effect(e,33,0);e.countBoundary("tests_screen");return;
        case 0x40103754:effect(e,34,0);e.countBoundary("developer_tests_screen");return;
        case 0x400ea5a4:effect(e,35,0);e.countBoundary("display_test");return;
        case 0x401037d4:effect(e,36,0);e.countBoundary("menu_test");return;
        case 0x40103fd0:effect(e,37,0);e.countBoundary("pause_test");return;
        case 0x40104a18:eventEffect(e,38,e.register(11));e.countBoundary("instructions_test");return;
        case 0x40104aac:eventEffect(e,39,e.register(11));e.countBoundary("titles_test");return;
        case 0x40106880:effect(e,40,0);e.countBoundary("scroll_test");return;
        case 0x400ef3e4:basic(e,41,e.register(12),0,0,0,e.register(11));e.countBoundary("paragraph_test");return;
        case 0x401069cc:effect(e,42,0);e.countBoundary("ascii_font_test");return;
        case 0x40103d10:effect(e,43,e.register(11));e.countBoundary("slideshow_test");return;
        case 0x40103d30:effect(e,44,0);e.countBoundary("progress_test");return;
        case 0x40107f6c:effect(e,45,0);e.countBoundary("eeprom_screen");return;
        case 0x40103680:effect(e,46,0);e.countBoundary("hardware_screen");return;
        case 0x400f9d78:effect(e,47,0);e.countBoundary("reset_runtime_menu");return;
        case 0x400f9d04:effect(e,48,e.register(11));e.countBoundary("append_command_front");return;
        case 0x40104b18:effect(e,49,e.register(11));e.countBoundary("confirm_stop");return;
        case 0x400ef230:case 0x400ef214:case 0x400ef180:case 0x400ee998:case 0x400eefb4:
        case 0x400d0f80:case 0x400d1040:case 0x400d0ffc:case 0x400eed44:case 0x400eed28:
        case 0x400d283c:case 0x400d27ac:case 0x400d1c20:case 0x400e2b9c:case 0x400ef160:
        case 0x401bf444:case 0x400d115c:case 0x400e128c:case 0x400d0fb8:case 0x400dae70:
        case 0x400deb2c:case 0x400d1cbc:case 0x400d2428:case 0x400d2398:case 0x40103de4:
        case 0x40103700:case 0x400fa0c8:case 0x401bf3d4:case 0x40103b78:case 0x400e60b8:
        case 0x400f40e4:case 0x400f3f04:case 0x400e1250:e.invoke(target);return;
        default:throw new IllegalStateException(String.format("Unreviewed Head UI call %08x",target));
        }
    }
    private int quantity(Z119Execution e,long p,JsonArray values,int index){for(int j=0;j<3;j++)e.write(p+j*4,values.get(index++).getAsLong(),4);return index;}
    private int parameters(Z119Execution e,long p,JsonArray values,int index){for(int j=0;j<3;j++)e.write(p+j*4,values.get(index++).getAsLong(),4);for(int off:new int[]{16,32,48,64})index=quantity(e,p+off,values,index);e.write(p+80,values.get(index++).getAsLong(),1);return index;}
    private void manual(Z119Execution e,long p,JsonArray values){int i=0;e.write(p,values.get(i++).getAsLong(),1);e.write(p+4,values.get(i++).getAsLong(),4);for(int off:new int[]{8,24,40})i=quantity(e,p+off,values,i);for(int off=56;off<60;off++)e.write(p+off,values.get(i++).getAsLong(),1);}
    private void quantityOut(Z119Execution e,long p,List<Long> values)throws Exception{for(int j=0;j<3;j++)values.add(e.read(p+j*4,4));}
    private void parametersOut(Z119Execution e,long p,List<Long> values)throws Exception{for(int j=0;j<3;j++)values.add(e.read(p+j*4,4));for(int off:new int[]{16,32,48,64})quantityOut(e,p+off,values);values.add(e.read(p+80,1));}
    private void manualOut(Z119Execution e,long p,List<Long> values)throws Exception{values.add(e.read(p,1));values.add(e.read(p+4,4));for(int off:new int[]{8,24,40})quantityOut(e,p+off,values);for(int off=56;off<60;off++)values.add(e.read(p+off,1));}
    private void putString(Z119Execution e,long p,String value){byte[] bytes=value.getBytes(java.nio.charset.StandardCharsets.UTF_8);for(int i=0;i<bytes.length;i++)e.write(p+i,bytes[i]&255L,1);e.write(p+bytes.length,0,1);}
    private void putHex(Z119Execution e,long p,String value){byte[] bytes=HexFormat.of().parseHex(value);for(int i=0;i<bytes.length;i++)e.write(p+i,bytes[i]&255L,1);}
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=3)throw new IllegalArgumentException("Expected cases, verified image layout and output JSON");JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();JsonObject layout=JsonParser.parseString(Files.readString(Path.of(args[1]))).getAsJsonObject();byte[] raw=Files.readAllBytes(Path.of(layout.get("input_path").getAsString()));
        if(!sha(raw).equals(Z119Execution.IMAGE_SHA))throw new IllegalStateException("Wrong original image");for(JsonElement value:layout.getAsJsonArray("segments")){JsonObject s=value.getAsJsonObject();int size=s.get("size").getAsInt();if(size==0)continue;byte[] loaded=new byte[size];currentProgram.getMemory().getBytes(toAddr(s.get("address").getAsLong()),loaded);int offset=s.get("offset").getAsInt();if(!Arrays.equals(loaded,Arrays.copyOfRange(raw,offset,offset+size)))throw new IllegalStateException("Project differs from original bytes");}
        List<Object> results=new ArrayList<>(),functions=new ArrayList<>();int caseIndex=0;
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary)){
            for(JsonElement value:inputs){
                JsonObject input=value.getAsJsonObject();JsonArray state=input.getAsJsonArray("state"),params=input.getAsJsonArray("parameters");e.resetRegisters();e.clear(HEAD,0xa300);e.clear(CORE,0x1200);effects.clear();assertionLine=0;clock=state.get(13).getAsLong()&0xffffffffL;e.write(0x3ffc4654L,0x5a61c0deL,4);e.write(0x3ffc2e84L,state.get(14).getAsLong(),4);
                e.write(HEAD+0x3868,SINK,4);e.write(HEAD+0x387c,CORE,4);e.write(HEAD+0x3870,EEPROM,4);e.write(EEPROM+0x10,input.get("language").getAsLong(),4);e.write(HEAD+0x3880,LANG,4);e.write(LANG,input.get("language").getAsLong(),4);
                e.write(CORE+4,state.get(0).getAsLong(),4);e.write(CORE+0x14,state.get(1).getAsLong(),4);e.write(CORE+0x68,state.get(2).getAsLong(),4);putString(e,CORE+0x24,input.get("name").getAsString());e.write(CORE+0xc,state.get(3).getAsLong(),1);e.write(CORE+0x4cc,state.get(4).getAsLong(),1);e.write(CORE+0x4e4,state.get(5).getAsLong(),4);
                e.write(HEAD+0x9f80,state.get(6).getAsLong(),1);e.write(HEAD+0x9f84,state.get(7).getAsLong(),4);e.write(HEAD+0x9f88,state.get(8).getAsLong(),4);e.write(HEAD+0x17e8,state.get(9).getAsLong(),4);e.write(HEAD+0x9f98,state.get(10).getAsLong(),1);e.write(HEAD+0xa1c8,state.get(11).getAsLong(),1);e.write(HEAD+0xa1c9,state.get(12).getAsLong(),1);
                putString(e,HEAD+0xa1cc,input.get("search").getAsString());e.write(HEAD+0xa1e0,state.get(15).getAsLong(),4);e.write(HEAD+0xa1e4,16-state.get(15).getAsLong(),4);e.write(HEAD+0xa1e8,state.get(16).getAsLong(),1);parameters(e,HEAD+0xa028,params,0);manual(e,HEAD+0xa0d8,input.getAsJsonArray("manual"));putHex(e,HEAD+0xa118,input.get("selected").getAsString());
                e.write(HEAD+0x1284,input.get("command").getAsLong(),4);e.write(HEAD+0x1288,input.get("flags").getAsLong(),1);e.write(HEAD+0x128c,input.get("index").getAsLong(),4);putHex(e,HEAD+0x1290,input.get("payload").getAsString());parameters(e,HEAD+0x1340,params,16);e.write(HEAD+0x1398,input.get("pending").getAsLong(),1);e.write(HEAD+0x1419,input.get("debug").getAsLong(),1);
                e.register(10,HEAD);try{e.invoke(0x401080d4L);}catch(Exception ex){throw new IllegalStateException("Case "+caseIndex+" (UiCommand "+input.get("command")+") failed",ex);}
                List<Long> out=new ArrayList<>(Arrays.asList(e.read(HEAD+0x9f80,1),e.read(HEAD+0x9f84,4),e.read(HEAD+0x9f88,4),e.read(HEAD+0x9f98,1),e.read(HEAD+0xa1c8,1),e.read(HEAD+0xa1c9,1),assertionLine,e.read(0x3ffc2e84L,4),e.read(HEAD+0x1398,1),e.read(HEAD+0xa1e0,4),e.read(HEAD+0xa1e8,1)));
                List<Long> parameters=new ArrayList<>(),manual=new ArrayList<>(),search=new ArrayList<>();parametersOut(e,HEAD+0xa028,parameters);manualOut(e,HEAD+0xa0d8,manual);for(int i=0;i<17;i++)search.add(e.read(HEAD+0xa1cc+i,1));
                results.add(row("state",out,"parameters",parameters,"manual",manual,"search",search,"recipe",sha(bytes(e,HEAD+0xa118,176)),"effects",new ArrayList<>(effects)));caseIndex++;
            }
            for(String address:e.functions.keySet()){Function function=currentProgram.getFunctionManager().getFunctionAt(toAddr(Long.parseLong(address.substring(2),16)));if(function==null)throw new IllegalStateException("Executed body lacks defined bounds");List<Object> ranges=new ArrayList<>();for(AddressRange range:function.getBody().getAddressRanges()){byte[] bytes=new byte[(int)range.getLength()];currentProgram.getMemory().getBytes(range.getMinAddress(),bytes);ranges.add(row("address",range.getMinAddress().getOffset(),"size",bytes.length,"sha256",sha(bytes)));}functions.add(row("address",address,"calls",e.functions.get(address),"ranges",ranges));}
            Map<String,Object> report=row("input_sha256",Z119Execution.IMAGE_SHA,"cases",results,"original_functions",functions,"modeled_boundaries",e.boundaries,"scope","Original complete UI dispatcher, static menu population, Core command construction, recipe helpers, numeric recipe-input screen construction, program start, payload getters, diagnostic-display suspension, localized title selection, timers and returning assertions. Named screen/service bodies, Head Core dispatch, alarm, synchronization, clock, delay, ROM copying and compiler arithmetic remain explicit boundaries.","hardware_tested",false,"full_cpu_emulation",false);Files.writeString(Path.of(args[2]),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+results.size()+" original Head UI cases");
    }
}
