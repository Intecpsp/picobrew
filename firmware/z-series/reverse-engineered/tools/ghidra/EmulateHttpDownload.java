// Execute original HttpUpdate/DownloadBinary receiver callbacks and assertions.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.io.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulateHttpDownload extends GhidraScript {
    private static final long OBJECT=0x3ff30000L,RESPONSE=0x3ff33000L,PAYLOAD=0x3ff35000L,MD5=0x3ff37000L;
    private long[] input;
    private long now;
    private List<String> events;
    private String string(Z119Execution e,long address)throws Exception {
        StringBuilder result=new StringBuilder();
        for(int i=0;i<1024;i++){int c=(int)e.read(address+i,1);if(c==0)return result.toString();result.append((char)c);}
        throw new IllegalStateException("Unterminated original string");
    }
    private void writeString(Z119Execution e,long address,String text){e.emulator.writeMemory(e.address(address),(text+"\0").getBytes(StandardCharsets.US_ASCII));}
    private long hash(Z119Execution e,long address,int length)throws Exception {
        long value=2166136261L;for(int i=0;i<length;i++)value=((value^e.read(address+i,1))*16777619L)&0xffffffffL;return value;
    }
    private void diagnostic(Z119Execution e)throws Exception {
        String format=string(e,e.register(10));
        if(format.contains("[ASSERT]")) {
            events.add("A|"+string(e,e.register(14))+"|"+e.register(15)+"|"+string(e,e.read(e.register(1),4))+"|"+
                e.register(12)+"|"+e.register(13)+"|"+e.read(0x3ffc2e84L,4));return;
        }
        if(format.startsWith("[IV]")) {
            if(format.contains("Ellapsed"))events.add("V|0|"+e.register(14)+"|");
            else if(format.contains("ERROR"))events.add("V|1|"+e.register(14)+"|");
            else if(format.contains("STATE"))events.add("V|2|0|"+string(e,e.register(14)));
            else if(format.contains("MD5"))events.add("V|3|0|"+string(e,e.register(14)));
            else throw new IllegalStateException("Unknown update value "+format);
            return;
        }
        String tag=format.contains("[DBIN]")?"DBIN":"OTA";
        int offset=format.indexOf("[%s:%u] ");
        if(offset<0)throw new IllegalStateException("Unknown update diagnostic "+format);
        String message=format.substring(offset+8).replace("\u001b[0m","").replace("\r\n","");
        events.add("L|"+tag+"|"+string(e,e.register(14))+"|"+e.register(15)+"|"+message);
    }
    private void boundary(Z119Execution e,long target)throws Exception {
        if(e.arithmetic(target))return;
        switch((int)target) {
        case 0x400db014:e.invoke(target);return;
        case 0x40081388:e.countBoundary("millis");e.register(10,now);return;
        case 0x4008c2b4:
            e.countBoundary("diagnostic_basename");String path=string(e,e.register(10));
            e.register(10,e.register(10)+Math.max(path.lastIndexOf('\\'),path.lastIndexOf('/'))+1);return;
        case 0x400d21cc:e.countBoundary("formatted_diagnostic");diagnostic(e);return;
        case 0x401289a0:
            e.countBoundary("delay");events.add("D|"+e.register(10)+"|"+e.read(0x3ffc2e84L,4));now=(now+e.register(10))&0xffffffffL;return;
        case 0x400d0e60:e.countBoundary("CString_exact_match");e.register(10,string(e,e.register(10)).equals(string(e,e.register(11)))?1:0);return;
        case 0x4000c2c8:
            e.countBoundary("ROM_memcpy");
            if(e.register(12)!=0)e.emulator.writeMemory(e.address(e.register(10)),e.emulator.readMemory(e.address(e.register(11)),(int)e.register(12)));return;
        case 0x40126704:
            e.countBoundary("Update_begin");events.add("P|0|"+e.register(11)+"|"+e.register(12)+"|"+e.register(13));e.register(10,input[8]);return;
        case 0x40126a38:
            e.countBoundary("Update_write");events.add("P|1|"+e.register(12)+"|"+hash(e,e.register(11),(int)e.register(12)));e.register(10,input[11]);return;
        case 0x40126960:e.countBoundary("Update_end");events.add("P|2|"+e.register(11));e.register(10,input[9]);return;
        case 0x4012a6e4:
            e.countBoundary("MD5Builder_toString");events.add("P|5");e.write(e.register(10),MD5,4);return;
        case 0x4012a994:e.countBoundary("Arduino_String_destructor");return;
        case 0x4012a288:e.countBoundary("ESP_restart");events.add("P|6");return;
        default:throw new IllegalStateException(String.format("Unreviewed download call %08x",target));
        }
    }
    private void invoke(Z119Execution e,long target)throws Exception {e.register(10,OBJECT);e.invoke(target);}
    private boolean headers(Z119Execution e,boolean binary)throws Exception {
        e.register(10,OBJECT);e.register(11,RESPONSE);e.invoke(binary?0x400da5f0L:0x400da534L);return e.register(10)!=0;
    }
    private void body(Z119Execution e,boolean binary,int size)throws Exception {
        e.register(10,OBJECT);e.register(11,PAYLOAD);e.register(12,size);e.invoke(binary?0x400e5c4cL:0x400d093cL);
    }
    private Map<String,Object> one(Z119Execution e,JsonObject item)throws Exception {
        input=new long[13];for(int i=0;i<input.length;i++)input[i]=item.getAsJsonArray("input").get(i).getAsLong();
        int mode=(int)input[0],size=item.get("size").getAsInt();boolean binary=(mode>=3&&mode<=5)||mode==7,accepted=false;
        now=input[6];events=new ArrayList<>();e.resetRegisters();e.clear(OBJECT,0x8000);
        e.write(0x3ffc2e84L,input[7],4);e.write(0x3ffc4654L,0x1195a5a5L,4);
        e.write(0x3ffc32c4L,input[12],1);e.write(0x3ffc32d0L,100,4);e.write(0x3ffc32e4L,input[10]!=0?100:99,4);
        writeString(e,MD5,"0123456789abcdef0123456789abcdef");
        for(int i=0;i<2048;i++)e.write(OBJECT+8+i,0xa5,1);
        if(binary) {
            e.write(OBJECT+0x808,input[1],4);e.write(OBJECT+0x1058,input[2],4);
            e.write(OBJECT+0x105c,input[4],1);e.write(OBJECT+0x105d,input[5],1);
        } else {
            e.write(OBJECT+0x858,input[1],4);e.write(OBJECT+0x85c,input[2],4);e.write(OBJECT+0x860,input[3],4);
            e.write(OBJECT+0x854,input[4],1);e.write(OBJECT+0x864,input[5],1);
        }
        e.write(RESPONSE+4,input[2],4);writeString(e,RESPONSE+8,item.get("type").getAsString());
        for(int i=0;i<size;i++)e.write(PAYLOAD+i,i*13+7,1);
        switch(mode) {
        case 0:case 3:accepted=headers(e,binary);break;
        case 1:case 4:body(e,binary,size);break;
        case 2:case 5:invoke(e,binary?0x400e5cfcL:0x400d8068L);break;
        case 6:case 7:
            accepted=headers(e,binary);if(accepted){body(e,binary,size);invoke(e,binary?0x400e5cfcL:0x400d8068L);}break;
        default:throw new IllegalStateException("Unknown mode");
        }
        long[] output={e.read(OBJECT+(binary?0x808:0x858),4),e.read(OBJECT+(binary?0x1058:0x85c),4),
            binary?input[3]:e.read(OBJECT+0x860,4),e.read(OBJECT+(binary?0x105c:0x854),1),e.read(OBJECT+(binary?0x105d:0x864),1),
            accepted?1:0,0,now,e.read(0x3ffc2e84L,4),hash(e,OBJECT+8,2048)};
        Map<String,Object> result=new LinkedHashMap<>();result.put("output",output);result.put("events",events);return result;
    }
    @Override public void run()throws Exception {
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Input and output paths required");
        byte[] inputBytes=Files.readAllBytes(Path.of(args[0]));JsonArray cases=JsonParser.parseString(new String(inputBytes,StandardCharsets.UTF_8)).getAsJsonArray();
        Gson gson=new Gson();
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter output=Files.newBufferedWriter(Path.of(args[1]))) {
            int index=0;for(JsonElement item:cases){try{output.write(gson.toJson(one(e,item.getAsJsonObject()))+"\n");}
                catch(Exception failure){throw new IllegalStateException("HTTP download case "+index,failure);}index++;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);
            report.put("input_file_sha256",HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(inputBytes)));
            report.put("cases",index);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original HttpUpdate and DownloadBinary headers/body/completion callbacks through return, including real returning assertion helper. Arduino Update begin/write/end and its error/progress fields, MD5 string lifetime, restart, ROM memcpy/CString comparison, formatted diagnostics, float helpers and time/delay are explicit SDK boundaries. Compares ordered update actions/logs, acceptance, sticky errors, complete flags, received/expected lengths, full binary buffer hash, assertion counter and timestamp bits. Does not flash, restart or validate a partition layout. Null spans and already-corrupt binary indices above 2048 are explicit reconstruction guards outside the equivalence domain.");
            Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+cases.size()+" original HTTP download cases");
    }
}
