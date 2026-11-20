// Execute original CoreHelpers controls and returning assertions.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.security.MessageDigest;
import java.io.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulateCoreHelpers extends GhidraScript {
    private static final long HELPER=0x3ff30000L,BACKEND=0x3ff31000L,
        CORE=0x3ff34000L,BACKSTATE=0x3ff38000L,EVENT=0x3ff39000L,ID=0x3ff3b000L;
    private long[] words;
    private List<Long> effects;
    private static String sha(byte[] bytes)throws Exception{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));}
    private void event(long kind,long value){effects.add(kind);effects.add(value&0xffffffffL);}
    private void boundary(Z119Execution e,long target)throws Exception {
        if(e.arithmetic(target))return;
        switch((int)target){
        case 0x40081388:e.countBoundary("millis");e.register(10,words[2]);return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:e.countBoundary("diagnostic_log");event(10,e.register(15));return;
        case 0x400d5a44:e.countBoundary("diagnostic_scope_begin");event(10,e.register(14));return;
        case 0x400d5a68:e.countBoundary("diagnostic_scope_end");return;
        case 0x400db014:event(11,e.register(11));e.invoke(target);return;
        case 0x401289a0:e.countBoundary("assertion_delay_200ms");event(12,e.register(10));words[2]=(words[2]+e.register(10))&0xffffffffL;return;
        case 0x400fb070:e.countBoundary("HttpBackend_registration_request");event(1,0);return;
        case 0x401bf484:e.invoke(target);return;
        case 0x400f02e4:e.countBoundary("CoreEvent_mailbox");event(2,e.read(e.register(11)+4,4));return;
        case 0x4010ca44:e.invoke(target);return;
        case 0x4010c7ec:e.countBoundary("HttpClient_configure");event(3,0);e.register(10,1);return;
        case 0x400f90d8:e.countBoundary("HttpClient_GET");event(4,0);e.register(10,1);return;
        case 0x400f04fc:e.countBoundary("ZBackend_canSync");event(5,0);e.register(10,words[16]);return;
        case 0x40119c44:e.countBoundary("ZBackend_create_session");event(6,0);return;
        case 0x4011c4f4:e.countBoundary("ZBackend_log_session");event(7,0);return;
        case 0x40119ce4:e.countBoundary("ZBackend_end_session");event(8,0);return;
        case 0x400db2c8:e.countBoundary("HttpClient_cancel");event(9,0);return;
        case 0x4011c7c8:e.invoke(target);return;
        default:throw new IllegalStateException(String.format("Unreviewed helper call %08x",target));
        }
    }
    private Map<String,Object> one(Z119Execution e,JsonObject input)throws Exception {
        e.resetRegisters();e.clear(HELPER,0x100);e.clear(BACKEND,0x2000);e.clear(CORE,0x1000);
        e.clear(BACKSTATE,0x400);e.clear(EVENT,0x1000);e.clear(ID,8);
        words=new long[18];JsonArray w=input.getAsJsonArray("words");for(int i=0;i<18;i++)words[i]=w.get(i).getAsLong();
        e.write(0x3ffc2e84L,words[3],4);effects=new ArrayList<>();
        long target;
        if(words[0]==0){
            e.write(HELPER,words[8],4);e.write(HELPER+4,BACKEND,4);e.write(HELPER+8,0x3ff3c000L,4);
            e.write(HELPER+12,EVENT,4);e.write(HELPER+16,words[5],1);e.write(HELPER+20,words[7],4);
            e.write(HELPER+24,words[6],1);e.write(HELPER+28,BACKSTATE,4);
            e.write(BACKSTATE+1,words[9],1);e.write(BACKSTATE+0x145,words[10],1);e.write(BACKSTATE+0x144,words[11],1);
            target=0x400fb100L;
        }else if(words[0]==1){
            e.write(HELPER+4,BACKEND,4);e.write(HELPER+12,words[5],1);e.write(HELPER+13,words[6],1);
            e.write(HELPER+16,BACKSTATE,4);e.write(BACKSTATE+1,words[9],1);
            e.write(BACKEND+0x105c,words[7],1);e.write(BACKEND+0x105d,words[8],1);
            target=0x4010ca58L;
        }else{
            e.write(HELPER+4,BACKEND,4);e.write(HELPER+8,words[8],1);e.write(HELPER+12,ID,4);e.write(ID,words[11],4);
            e.write(HELPER+16,CORE,4);e.write(HELPER+20,words[5],4);e.write(HELPER+24,words[6],4);
            e.write(HELPER+28,words[7],4);e.write(HELPER+32,words[9],1);e.write(HELPER+33,words[10],1);
            e.write(BACKEND+0x85c,words[12],4);e.write(BACKEND+0x1104,words[13],4);
            e.write(CORE+16,words[14],4);e.write(CORE+0x2a4,words[15],4);e.write(CORE+0x304,0x3ff3d000L,4);
            target=words[0]==2?0x4011c928L:words[0]==3?0x4011c7c8L:0x4011cadcL;
        }
        e.register(10,HELPER);e.register(11,words[0]==3?words[17]:words[1]);e.invoke(target);
        if(words[0]==0){words[5]=e.read(HELPER+16,1);words[6]=e.read(HELPER+24,1);words[7]=e.read(HELPER+20,4);}
        else if(words[0]==1){words[5]=e.read(HELPER+12,1);words[6]=e.read(HELPER+13,1);words[7]=e.read(BACKEND+0x105c,1);words[8]=e.read(BACKEND+0x105d,1);}
        else{words[5]=e.read(HELPER+20,4);words[6]=e.read(HELPER+24,4);words[7]=e.read(HELPER+28,4);
             words[8]=e.read(HELPER+8,1);words[9]=e.read(HELPER+32,1);words[10]=e.read(HELPER+33,1);words[11]=e.read(ID,4);}
        words[3]=e.read(0x3ffc2e84L,4);words[4]=0;
        Map<String,Object> output=new LinkedHashMap<>();output.put("id",input.get("id").getAsString());output.put("words",words);output.put("effects",effects);return output;
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected cases and JSONL output");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson gson=new Gson();int count=0;
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter output=Files.newBufferedWriter(Path.of(args[1]))){
            for(JsonElement input:inputs){try{output.write(gson.toJson(one(e,input.getAsJsonObject()))+"\n");}catch(Exception failure){throw new IllegalStateException("Helper case "+count,failure);}++count;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);report.put("cases",count);
            report.put("input_file_sha256",sha(Files.readAllBytes(Path.of(args[0]))));report.put("original_functions_executed",e.functions);
            report.put("modeled_boundaries",e.boundaries);report.put("hardware_tested",false);
            report.put("scope","Completed original registration/download helper ticks and SessionLogger begin/transition/tick, including CoreEvent reset, returning assertion helper, counter and 200 ms delay. HTTP/backend operations, mailbox storage and diagnostic formatting are explicit call boundaries. State words, ordered semantic calls, diagnostic source lines, counter and post-delay clock are compared. No original network/server execution or full application claim.");
            Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+count+" original Core helper cases");
    }
}
