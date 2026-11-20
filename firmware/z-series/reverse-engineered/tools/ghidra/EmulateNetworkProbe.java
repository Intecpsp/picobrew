// Execute the original network check through its reachability-probe body.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.security.MessageDigest;
import java.io.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
public class EmulateNetworkProbe extends GhidraScript {
    private static final long OBJECT=0x3ff30000L,CLIENT=0x3ff31000L,VTABLE=0x3ff32000L;
    private long[] words;private List<Long> effects;
    private void event(long id,long value){effects.add(id);effects.add(value&0xffffffffL);}
    private String string(Z119Execution e,long p)throws Exception {StringBuilder out=new StringBuilder();for(int i=0;i<256;i++){int b=(int)e.read(p+i,1);if(b==0)return out.toString();out.append((char)b);}throw new IllegalStateException("Unterminated text");}
    private void boundary(Z119Execution e,long target)throws Exception {
        if(e.arithmetic(target))return;
        switch((int)target){
        case 0x400ed728:e.invoke(target);return;
        case 0x40081388:e.countBoundary("millis");e.register(10,12345);return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d5a44:e.countBoundary("diagnostic_scope_begin");return;
        case 0x400d5a68:e.countBoundary("diagnostic_scope_end");return;
        case 0x400d21cc:e.countBoundary("diagnostic_output");event(5,e.register(15));return;
        case 0x40122e80:e.countBoundary("WiFi_status");event(1,words[1]);e.register(10,words[1]);return;
        case 0x400ed4d4:e.countBoundary("ClientFactory_acquire_plain_Ethernet");if(e.register(13)!=1)throw new IllegalStateException("Wrong original network selection");
            event(2,0);e.write(e.register(11),words[3]!=0?CLIENT:0,4);e.register(10,words[2]);return;
        case 0x40030000:e.countBoundary("Client_connect_host");if(!string(e,e.register(11)).equals("www.picobrewtest.com"))throw new IllegalStateException("Wrong original test host");
            event(3,e.register(12));e.register(10,words[4]);return;
        case 0x40030010:e.countBoundary("Client_stop");event(4,0);return;
        default:throw new IllegalStateException(String.format("Unreviewed network probe call %08x",target));
        }
    }
    private Map<String,Object> one(Z119Execution e,JsonObject input)throws Exception {
        e.resetRegisters();e.clear(OBJECT,0x3000);words=new long[7];JsonArray values=input.getAsJsonArray("words");for(int i=0;i<7;i++)words[i]=values.get(i).getAsLong();effects=new ArrayList<>();
        e.write(CLIENT,VTABLE,4);e.write(VTABLE+0x24,0x40030000L,4);e.write(VTABLE+0x2c,0x40030010L,4);
        e.register(10,OBJECT);e.register(11,words[0]);e.invoke(0x400ed7d8L);words[5]=e.register(10);words[6]=0;
        Map<String,Object> output=new LinkedHashMap<>();output.put("id",input.get("id").getAsString());output.put("words",words);output.put("effects",effects);return output;
    }
    @Override public void run()throws Exception {
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input and output JSONL");
        byte[] bytes=Files.readAllBytes(Path.of(args[0]));JsonArray inputs=JsonParser.parseString(new String(bytes,java.nio.charset.StandardCharsets.UTF_8)).getAsJsonArray();Gson gson=new Gson();int count=0;
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter output=Files.newBufferedWriter(Path.of(args[1]))){
            for(JsonElement value:inputs){try{output.write(gson.toJson(one(e,value.getAsJsonObject()))+"\n");}catch(Exception failure){throw new IllegalStateException("Network probe case "+count,failure);}++count;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);report.put("cases",count);
            report.put("input_file_sha256",HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes)));
            report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);report.put("hardware_tested",false);
            report.put("scope","Full original ZClient connectivity decision and Ethernet probe bodies. Client factory, SDK WiFi status, socket connect/stop and diagnostic formatting are explicit boundaries; compare returned connectivity and ordered calls. No live network or complete application claim.");
            Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+count+" original network probes");
    }
}
