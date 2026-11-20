// Execute original Azure lifecycle, nested connect and backoff bodies.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.security.MessageDigest;
import java.io.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
public class EmulateAzure extends GhidraScript {
    private static final long OBJECT=0x3ff30000L,STATE=0x3ff31000L,SYSTEM=0x3ff32000L,NETWORK=0x3ff33000L;
    private long[] words;private List<Long> assertions,delays;private List<Object> effects;
    private final Set<Long> bodies=Set.of(0x400ed990L,0x400ed8acL,0x400dabd4L,0x400d782cL,0x400d77c4L,0x400d5b74L,0x401bf884L,0x400e1820L,0x400f3fa4L);
    private final Set<Long> diagnostics=Set.of(0x161L,0x165L,0xdcL,0xdfL,0xe2L,0x209L,0x289L);
    private void event(long type,long argument){effects.add(List.of(type,argument));}
    private void boundary(Z119Execution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        switch((int)target){
        case 0x40081388:e.countBoundary("clock");e.register(10,words[2]);return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:e.countBoundary("diagnostic_format");if(diagnostics.contains(e.register(15)))event(11,e.register(15));return;
        case 0x400db014:assertions.add(e.register(11));e.invoke(target);return;
        case 0x401289a0:e.countBoundary("assertion_delay_200ms");delays.add(e.register(10));words[2]=(words[2]+e.register(10))&0xffffffffL;return;
        case 0x401bff2c:e.countBoundary("MQTT_connected");event(1,0);e.register(10,words[14]);return;
        case 0x40126d48:e.countBoundary("MQTT_disconnect");event(2,0);return;
        case 0x400ed4d4:e.countBoundary("System_acquire_TLS_client");event(3,1);e.write(e.register(11),words[15]!=0?0x3ff34000L:0,4);e.write(e.register(12),3,4);e.register(10,1);return;
        case 0x401bff88:e.countBoundary("MQTT_setClient");event(4,0);return;
        case 0x401bff6c:e.countBoundary("MQTT_setServer");event(5,e.register(12));return;
        case 0x401bff7c:e.countBoundary("MQTT_setCallback");event(6,0);return;
        case 0x40126f4c:e.countBoundary("MQTT_connect");event(7,0);e.register(10,words[16]);return;
        case 0x40127200:e.countBoundary("MQTT_subscribe");event(8,0);e.register(10,1);return;
        case 0x40126f6c:e.countBoundary("MQTT_loop");event(9,0);e.register(10,1);return;
        case 0x400ef230:e.countBoundary("CoreCommand_envelope_constructor");e.write(e.register(10)+4,e.register(11),4);return;
        case 0x400f3d3c:e.countBoundary("CoreCommand_single_slot_mailbox");if(words[18]==0){words[18]=1;words[19]=e.read(e.register(11)+4,4);}return;
        default:if(bodies.contains(target)){e.invoke(target);return;}throw new IllegalStateException(String.format("Unreviewed Azure call %08x",target));
        }
    }
    private Map<String,Object> one(Z119Execution e,JsonObject input)throws Exception{
        e.resetRegisters();e.clear(OBJECT,0x1000);e.clear(STATE,4);e.clear(SYSTEM,0x100);e.clear(NETWORK,0x100);
        words=new long[22];for(int i=0;i<words.length;i++)words[i]=input.getAsJsonArray("words").get(i).getAsLong();assertions=new ArrayList<>();delays=new ArrayList<>();effects=new ArrayList<>();
        e.write(0x3ffc2e84L,words[3],4);e.write(0x3ffc4654L,0x5a5a5a5aL,4);e.write(OBJECT+4,SYSTEM,4);e.write(SYSTEM+0x8c,NETWORK,4);
        e.write(OBJECT+0x724,STATE,4);e.write(STATE,words[4],4);e.write(OBJECT+0x454,words[5],1);e.write(OBJECT+0x730,words[6],4);e.write(OBJECT+0x734,words[7],4);
        e.write(OBJECT+0x738+4,words[8],1);for(int i=9;i<=13;i++)e.write(OBJECT+0x738+8+4*(i-9),words[i],4);
        e.register(10,OBJECT);long target;
        if(words[0]==0){target=0x400f400cL;e.register(11,words[1]);}
        else if(words[0]==1){target=0x400ed990L;e.register(11,words[17]);}
        else if(words[0]==2){e.write(NETWORK,OBJECT,4);e.register(10,NETWORK);target=0x400edaccL;}
        else if(words[0]==3)target=0x400eda64L;
        else if(words[0]==4)target=0x400eda88L;
        else if(words[0]==5)target=0x400edaacL;
        else{e.write(OBJECT-0x15160+0x4800,words[21],4);e.register(10,OBJECT-0x15160);target=0x400edaecL;}
        e.invoke(target);words[3]=e.read(0x3ffc2e84L,4);words[4]=e.read(STATE,4);words[5]=e.read(OBJECT+0x454,1);words[6]=e.read(OBJECT+0x730,4);words[7]=e.read(OBJECT+0x734,4);
        words[8]=e.read(OBJECT+0x738+4,1);for(int i=9;i<=13;i++)words[i]=e.read(OBJECT+0x738+8+4*(i-9),4);
        Map<String,Object> result=new LinkedHashMap<>();result.put("id",input.get("id").getAsString());result.put("words",words);result.put("effects",effects);result.put("assertions",assertions);result.put("delays",delays);result.put("counter",words[3]);return result;
    }
    @Override public void run()throws Exception{String[] args=getScriptArgs();JsonArray cases=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson gson=new Gson();
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){int count=0;
            for(JsonElement value:cases){try{out.write(gson.toJson(one(e,value.getAsJsonObject()))+"\n");}catch(Exception failure){throw new IllegalStateException("Azure case "+count,failure);}++count;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);report.put("cases",count);report.put("input_file_sha256",HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(Path.of(args[0])))));
            report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");}
    }
}
