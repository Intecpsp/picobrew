// Execute original Pinger policy and real returning diagnostic assertions.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import java.io.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
public class EmulatePinger extends GhidraScript {
    private static final long OBJECT=0x3ff30000L,TEXT=0x3ff31000L,STATS=0x3ff32000L;
    private long[] words;private List<Long> effects;private boolean assertion;
    private void event(long id,long a,long b){effects.add(id);effects.add(a&0xffffffffL);effects.add(b&0xffffffffL);}
    private void boundary(Z119Execution e,long target)throws Exception {
        if(e.arithmetic(target))return;
        switch((int)target){
        case 0x400e14c4:case 0x401bf3d4:e.invoke(target);return;
        case 0x400db014:{boolean saved=assertion;assertion=true;e.invoke(target);assertion=saved;return;}
        case 0x40081388:e.countBoundary("millis");e.register(10,12345);return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:e.countBoundary("diagnostic_format_output");
            if(assertion)event(7,e.register(15),e.read(0x3ffc2e84L,4));else event(6,e.register(15),0);return;
        case 0x401289a0:e.countBoundary("assertion_delay");event(8,e.register(10),e.read(0x3ffc2e84L,4));return;
        case 0x40122e80:e.countBoundary("WiFi_status");event(1,words[3],0);e.register(10,words[3]);return;
        case 0x401675c4:e.countBoundary("esp_ping_set_target");event(2,e.register(10),e.read(e.register(11),4));e.register(10,0);return;
        case 0x40167c24:e.countBoundary("ping_init");event(3,words[4],0);e.register(10,words[4]);return;
        case 0x401226c8:e.countBoundary("WiFi_hostByName");event(4,words[5],words[6]);e.write(e.register(11)+4,words[6],4);e.register(10,words[5]);return;
        case 0x4012a5c8:e.countBoundary("IPAddress_default_constructor");e.write(e.register(10),0x3f44a8b8L,4);e.write(e.register(10)+4,0,4);return;
        case 0x401c0018:e.countBoundary("IPAddress_fromString");event(5,words[7],0);e.write(e.register(10)+4,words[7],4);e.register(10,1);return;
        case 0x400f708c:e.countBoundary("console_registry");event(9,0,0);return;
        default:throw new IllegalStateException(String.format("Unreviewed Pinger call %08x",target));
        }
    }
    private Map<String,Object> one(Z119Execution e,JsonObject input)throws Exception {
        words=new long[25];JsonArray raw=input.getAsJsonArray("words");for(int i=0;i<words.length;i++)words[i]=raw.get(i).getAsLong();
        e.resetRegisters();e.clear(OBJECT,0x4000);e.write(0x3ffc2e84L,words[9],4);e.write(0x3ffc2ea0L,0xffffffffL,4);e.write(0x3ffc2e80L,0x3ff28000L,4);
        e.write(OBJECT+0x10,words[12],4);e.write(OBJECT+0x14,words[2],4);
        byte[] name="example.test\0".getBytes(java.nio.charset.StandardCharsets.UTF_8);e.emulator.writeMemory(e.address(TEXT),name);
        long text=words[8]==1?0:TEXT;if(words[8]==2)e.write(TEXT,0,1);
        for(int i=0;i<10;i++)e.write(STATS+4*i,words[15+i],4);
        assertion=false;effects=new ArrayList<>();e.register(10,OBJECT);long function=0;
        switch((int)words[0]){
        case 0:e.write(TEXT+128,0x3f44a8b8L,4);e.write(TEXT+132,words[1],4);e.register(11,TEXT+128);function=0x400e14c4L;break;
        case 1:e.register(11,text);function=0x400e15f8L;break;
        case 2:e.register(11,text);function=0x400e1738L;break;
        case 3:e.register(11,55);e.register(12,STATS);function=0x400db05cL;break;
        case 4:function=0x400f71b0L;break;
        default:throw new IllegalArgumentException("op");
        }
        e.invoke(function);words[10]=words[0]<3?e.register(10):0;words[2]=e.read(OBJECT+0x14,4);words[12]=e.read(OBJECT+0x10,4);words[9]=e.read(0x3ffc2e84L,4);words[13]=0;
        Map<String,Object> result=new LinkedHashMap<>();result.put("id",input.get("id").getAsString());result.put("words",words);result.put("effects",effects);return result;
    }
    @Override public void run()throws Exception {
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected inputs and output JSONL");byte[] data=Files.readAllBytes(Path.of(args[0]));
        JsonArray inputs=JsonParser.parseString(new String(data,java.nio.charset.StandardCharsets.UTF_8)).getAsJsonArray();Gson gson=new Gson();int count=0;
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){
            for(JsonElement input:inputs){try{out.write(gson.toJson(one(e,input.getAsJsonObject()))+"\n");}catch(Exception failure){throw new IllegalStateException("Pinger case "+count,failure);}count++;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("schema_version",1);report.put("input_sha256",Z119Execution.IMAGE_SHA);report.put("cases",count);
            report.put("input_file_sha256",HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(data)));report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);report.put("hardware_tested",false);
            report.put("scope","Complete original Pinger setup, numeric/hostname/text-IP requests and result callback, including actual assertion counter update and delay continuation. Compare target/remaining/return, assertion count, diagnostics and ordered SDK operations. DNS, SDK IP parsing, WiFi status, lwIP transport, console registry and printf formatting are explicit boundaries; no live network or RTOS concurrency claim.");
            Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+count+" original Pinger cases");
    }
}
