// Execute the complete original /state field selection and JSON serialization.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulateLocalHttpState extends GhidraScript {
    private static final long SERVER=0x3ff30000L,CLIENT=0x3ff32000L,VTABLE=0x3ff33000L,
        CORE=0x3ff40000L,NAME=0x3ff42000L,WRITE=0x7ff31000L;
    private static final Set<Long> BODIES=Set.of(
        0x400d1530L,0x400d154cL,0x400d1574L,0x400d1590L,0x400d15a8L,0x400f0240L,0x400f0268L,
        0x400f02a4L,0x400f50d0L,0x400f50f4L,0x40111adcL,0x40112ca8L,0x40112cd8L,0x4011368cL,
        0x40113724L,0x40119384L,0x401193b0L,0x40119404L,0x4011941cL,0x401194b4L,0x4011961cL,
        0x40119890L,0x4011997cL,0x40119998L,0x4011ae84L,0x4011d8ccL,0x4012a76cL,0x4012a734L,
        0x401bf254L,0x401bf62cL,0x401bf678L,0x401bf68cL,0x401bf6b4L,0x401bf6d8L,
        0x401bf6ecL,0x401bf724L,0x401bf79cL,0x401bf7acL,0x401bf7c0L,0x401bfab4L);
    private byte[] output;
    private long status,length;
    private static String sha(byte[] bytes)throws Exception{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));}
    private byte[] bytes(DisplayExecution e,long address)throws Exception{
        ByteArrayOutputStream out=new ByteArrayOutputStream();
        for(int i=0;i<8192;i++){int next=(int)e.read(address+i,1);if(next==0)return out.toByteArray();out.write(next);}
        throw new IllegalStateException("Unterminated original string");
    }
    private String text(DisplayExecution e,long address)throws Exception{return new String(bytes(e,address),StandardCharsets.UTF_8);}
    private void boundary(DisplayExecution e,long target)throws Exception{
        if(BODIES.contains(target)){e.invoke(target);return;}
        switch((int)target){
        case 0x4012d268:e.countBoundary("IEEE_double_equality");e.register(10,e.number(10)==e.number(12)?0:1);return;
        case 0x4012d42c:
            e.countBoundary("double_to_unsigned_defined_domain");double value=e.number(10);
            if(!Double.isFinite(value)||value<0||value>=4294967296.0)throw new IllegalStateException("Unreviewed unsigned conversion");
            e.register(10,(long)value);return;
        case 0x4012fecc:e.countBoundary("CXX_static_guard_acquire_single_thread");e.register(10,e.read(e.register(10),1)==0?1:0);return;
        case 0x4012ff60:e.countBoundary("CXX_static_guard_release_single_thread");e.write(e.register(10),1,1);return;
        case 0x4000c44c:{
            e.countBoundary("ROM_memset");byte[] b=new byte[(int)e.register(12)];Arrays.fill(b,(byte)e.register(11));
            e.emulator.writeMemory(e.address(e.register(10)),b);return;
        }
        case 0x4000c2c8:{
            e.countBoundary("ROM_memcpy");if(e.register(12)>0)e.emulator.writeMemory(e.address(e.register(10)),e.emulator.readMemory(e.address(e.register(11)),(int)e.register(12)));return;
        }
        case 0x400014c0:e.countBoundary("ROM_strlen");e.register(10,bytes(e,e.register(10)).length);return;
        case 0x40001274:e.countBoundary("ROM_strcmp");e.register(10,text(e,e.register(10)).compareTo(text(e,e.register(11))));return;
        case 0x40081388:e.countBoundary("diagnostic_millis");e.register(10,1000);return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:e.countBoundary("diagnostic_log");return;
        case 0x400db014:throw new IllegalStateException("Unexpected assertion line "+e.register(11));
        case 0x401367a0:throw new IllegalStateException("Original stack guard failed");
        case 0x400d97c4:
            e.countBoundary("verified_response_header_formatter");status=e.register(12);length=e.register(13);
            if(e.register(10)!=SERVER||e.register(11)!=CLIENT||!text(e,e.register(14)).equals("application/json"))throw new IllegalStateException("Wrong state response headers");return;
        case 0x7ff31000:
            e.countBoundary("Print_virtual_write");if(e.register(10)!=CLIENT||e.register(12)>2048)throw new IllegalStateException("Invalid state output");
            output=e.emulator.readMemory(e.address(e.register(11)),(int)e.register(12));e.register(10,output.length);return;
        default:
            if(e.arithmetic(target))return;
            throw new IllegalStateException(String.format("Unreviewed state call %08x",target));
        }
    }
    private Map<String,Object> one(DisplayExecution e,JsonObject input)throws Exception{
        e.resetRegisters();e.clear(SERVER,0x1000);e.clear(CLIENT,32);e.clear(VTABLE,32);e.clear(CORE,0x1000);e.clear(NAME,8192);
        e.write(0x3ffc4654L,0x4a6d8917L,4);e.write(SERVER+0x10,CORE,4);e.write(CLIENT,VTABLE,4);e.write(VTABLE+12,WRITE,4);
        int[] locations={0x548,0x558,0x578,0x568,0x2b0};JsonArray numbers=input.getAsJsonArray("numbers");
        for(int i=0;i<5;i++)e.write(CORE+locations[i],Long.parseUnsignedLong(numbers.get(i).getAsString(),16),8);
        JsonArray values=input.getAsJsonArray("values");int[] offsets={0x2e3,0x2e1,0x2fc,0x80c,0x368,0x3a0,0x3b8};
        for(int i=0;i<7;i++)e.write(CORE+offsets[i],values.get(i).getAsLong(),i<2?1:4);
        JsonElement step=input.get("step");
        if(!step.isJsonNull()){
            byte[] name=step.getAsString().getBytes(StandardCharsets.UTF_8);e.emulator.writeMemory(e.address(NAME),name);e.write(CORE+0x304,NAME,4);
        }
        e.emulator.writeMemory(e.address(SERVER+0x5c),"earlier\0".getBytes(StandardCharsets.US_ASCII));e.write(SERVER+0x860,7,4);
        output=null;status=length=0;e.register(10,SERVER);e.register(11,CLIENT);e.invoke(0x4011ae84L);
        if(output==null)throw new IllegalStateException("Missing original response");
        Map<String,Object> out=new LinkedHashMap<>();out.put("id",input.get("id").getAsString());
        out.put("response",List.of(status,length,e.read(SERVER+0x860,4),e.read(SERVER+0x869,1)));
        out.put("output_hex",HexFormat.of().formatHex(output));return out;
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input JSON and output JSONL");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson gson=new Gson();int count=0;
        try(DisplayExecution e=new DisplayExecution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){
            e.write(0x3ffc1010L,0,1);e.write(0x3ffc1018L,0,1);e.write(0x3ffc1068L,0,1);
            e.countBoundary("application_zeroed_static_guards");
            for(JsonElement item:inputs){try{out.write(gson.toJson(one(e,item.getAsJsonObject()))+"\n");}catch(Exception error){throw new IllegalStateException("Case "+count+" "+item,error);}count++;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",DisplayExecution.IMAGE_SHA);report.put("cases",count);
            report.put("input_file_sha256",sha(Files.readAllBytes(Path.of(args[0]))));report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Complete original /state field selection, JSON tree construction and serialization instructions, including original double decimal rounding and bounded writer. Explicit boundaries are scalar compiler arithmetic, ROM memory/string operations, single-thread C++ guards, verified header formatter and final virtual socket output. No physical machine or concurrent SDK execution.");
            report.put("hardware_tested",false);Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+count+" original /state cases");
    }
}
