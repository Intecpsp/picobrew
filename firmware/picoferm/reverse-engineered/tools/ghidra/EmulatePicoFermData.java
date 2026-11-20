import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.util.*;
import java.util.zip.*;
import com.google.gson.*;
public class EmulatePicoFermData extends EmulatePicoFermHardware {
    protected static final long BASE=0x3ff02000L,DATA=BASE+0x38,RECORD=BASE+0xc0,OUTPUT=BASE+0x400,FIRST=BASE+0x600,SECOND=BASE+0xa00,RAW=BASE+0xe00;
    protected void text(PicoFermExecution e,long pointer,String value){e.emulator.writeMemory(e.address(pointer),(value+"\0").getBytes(StandardCharsets.ISO_8859_1));}
    protected void unhex(PicoFermExecution e,long pointer,String value){for(int i=0;i<value.length()/2;i++)e.write(pointer+i,Integer.parseInt(value.substring(2*i,2*i+2),16),1);}
    protected void dataPointers(PicoFermExecution e,long pointer){for(int[] offset:new int[][]{{0x70,0},{0x74,0x10},{0x78,0x30},{0x7c,0x40},{0x80,0x60}})e.write(pointer+offset[0],pointer+offset[1],4);}
    @Override protected void boundary(PicoFermExecution e,long target)throws Exception{
        switch((int)target){
        case 0x40202f50:{e.countBoundary("application_diagnostic_text_with_mask");String value=e.string(e.register(4));if((e.register(5)&255)!=0)value="*".repeat(value.length());events.add("text:"+e.string(e.register(2))+":"+e.string(e.register(3))+":"+value);return;}
        case 0x4020f5b8:e.countBoundary("libc_strlen");e.register(2,e.string(e.register(2)).length());return;
        case 0x40213f70:case 0x4020f64c:{e.countBoundary(target==0x40213f70L?"libc_memcpy":"libc_strncpy");long length=e.register(4);if(length>8192)throw new IllegalStateException("Invalid memory copy");if(length!=0)e.emulator.writeMemory(e.address(e.register(2)),e.emulator.readMemory(e.address(e.register(3)),(int)length));return;}
        case 0x4020f534:e.countBoundary("libc_strcpy");text(e,e.register(2),e.string(e.register(3)));return;
        case 0x4000ccb8:e.countBoundary("ROM_fixdfsi");e.register(2,(int)doubleArg(e,2));return;
        case 0x4020f3d4:{e.countBoundary("libc_sprintf_reviewed_format");String format=e.string(e.register(3)),value;
            if(format.equals("[%d %s %s]"))value="["+(int)e.register(4)+" "+e.string(e.register(5))+" "+e.string(e.register(6))+"]";
            else if(format.equals("%s -> %s"))value=e.string(e.register(4))+" -> "+e.string(e.register(5));
            else throw new IllegalStateException("Unexpected sprintf "+format);text(e,e.register(2),value);e.register(2,value.length());return;}
        case 0x4020e2b0:{e.countBoundary("libc_itoa");if(e.register(4)!=10)throw new IllegalStateException("Nondecimal itoa");long pointer=e.register(3);text(e,pointer,Integer.toString((int)e.register(2)));e.register(2,pointer);return;}
        default:super.boundary(e,target);
        }
    }
    protected void addDataFunctions(){nested.addAll(Arrays.asList(0x4020348cL,0x402043c4L,0x40204480L,0x40203b60L,0x40201050L,0x40214288L,0x40105f48L,0x40105f6cL));}
    private Map<String,Object> data(PicoFermExecution e,JsonObject input)throws Exception{
        e.resetRegisters();e.clear(BASE,8192);events=new ArrayList<>();unhex(e,DATA,input.get("data").getAsString());unhex(e,RECORD,input.get("record").getAsString());dataPointers(e,DATA);
        for(int i=0;i<128;i++)e.write(OUTPUT+i,0xa5,1);text(e,FIRST,input.get("first").getAsString());text(e,SECOND,input.get("second").getAsString());unhex(e,RAW,input.get("raw").getAsString());
        long capacity=input.get("capacity").getAsLong(),length=input.get("raw").getAsString().length()/2;
        switch(input.get("op").getAsInt()){
        case 0:call(e,0x4020348cL,OUTPUT,capacity,FIRST);break;case 1:call(e,0x402043c4L,OUTPUT,capacity,RAW,length);break;
        case 2:call(e,0x40204578L,DATA,FIRST,SECOND);break;case 3:call(e,0x40204440L,DATA,RAW,length);break;case 4:call(e,0x40204480L,DATA,FIRST,SECOND);break;
        case 5:call(e,0x402044e8L,BASE,OUTPUT,capacity,FIRST,SECOND);break;case 6:call(e,0x40203bbcL,RECORD);break;default:throw new IllegalArgumentException("Unknown Data case");}
        Map<String,Object> out=new LinkedHashMap<>();out.put("id",input.get("id").getAsString());out.put("data",hex(e,DATA,112));out.put("record",hex(e,RECORD,236));out.put("output",hex(e,OUTPUT,128));out.put("changed",e.read(BASE+0x30,1));out.put("events",events);return out;
    }
    @Override public void run()throws Exception{
        addDataFunctions();String[] args=getScriptArgs();JsonArray inputs;try(GZIPInputStream in=new GZIPInputStream(Files.newInputStream(Path.of(args[0])))){inputs=JsonParser.parseReader(new InputStreamReader(in,StandardCharsets.UTF_8)).getAsJsonArray();}
        List<Map<String,Object>> cases=new ArrayList<>();Map<String,Object> report=new LinkedHashMap<>();
        try(PicoFermExecution e=new PicoFermExecution(currentProgram,monitor,this::boundary)){
            for(JsonElement input:inputs){try{cases.add(data(e,input.getAsJsonObject()));}catch(Exception ex){throw new IllegalStateException(input.getAsJsonObject().get("id").getAsString(),ex);}}
            report.put("input_sha256",PicoFermExecution.IMAGE_SHA);report.put("cases_input_gzip_sha256",sha(Path.of(args[0])));report.put("cases",cases);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original Data initialization, MAC/UID/APSSID, XOR token, credential mutation, bounded copy/hex and storage report. Actual SDK dtostrf executes with ROM arithmetic boundaries. Returning assertions execute; invalid null/zero-capacity/oversized hex and extreme diagnostic overflow inputs are outside returning comparison domain.");report.put("hardware_tested",false);
        }
        try(GZIPOutputStream out=new GZIPOutputStream(Files.newOutputStream(Path.of(args[1])))){out.write((new Gson().toJson(report)+"\n").getBytes(StandardCharsets.UTF_8));}println("Executed "+cases.size()+" original PicoFerm Data cases");
    }
}
