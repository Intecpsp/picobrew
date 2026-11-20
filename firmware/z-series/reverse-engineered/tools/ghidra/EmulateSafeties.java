// Original Safeties::tick and nested timed-threshold monitor execution.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.io.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulateSafeties extends GhidraScript {
    private static final long OBJECT=0x3ff50000L, FLAGS=OBJECT+0x1000, SENSORS=OBJECT+0x2000, MSP=OBJECT+0x3000;
    private static final long[] DOUBLES={SENSORS+0x20,SENSORS+0x30,SENSORS+0x40,SENSORS+0x68,OBJECT+0x28,OBJECT+0x48,OBJECT+0x68};
    private static final long[] WORDS={OBJECT+0x10,FLAGS,FLAGS+1,FLAGS+2,FLAGS+3,FLAGS+4,FLAGS+5,FLAGS+6,
        OBJECT+0x78,OBJECT+0x79,OBJECT+0x7a,OBJECT+0x1c,OBJECT+0x20,OBJECT+0x3c,OBJECT+0x40,OBJECT+0x5c,OBJECT+0x60,MSP+9,MSP+11};
    private static final int[] SIZES={1,1,1,1,1,1,1,1,1,1,1,1,4,1,4,1,4,1,1};
    private long pin,now;private int reads;
    private void boundary(Z119Execution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        switch((int)target){
            case 0x40081388:e.countBoundary("clock_millis");e.register(10,now);return;
            case 0x4008c2b4:e.countBoundary("basename");return;
            case 0x400d21cc:e.countBoundary("diagnostic_log");return;
            case 0x40080b04:
                if(e.register(10)!=36)throw new IllegalStateException("Unexpected filter pin");
                e.countBoundary("digitalRead_36");reads++;e.register(10,pin);return;
            case 0x400d7f7c:case 0x400d7fd8:case 0x400d69c8:e.invoke(target);return;
            case 0x400db014:throw new IllegalStateException("Original safeties assertion fired");
            default:throw new IllegalStateException(String.format("Unmodeled safety call %08x",target));
        }
    }
    private void load(Z119Execution e,JsonObject input)throws Exception{
        e.resetRegisters();e.clear(OBJECT,0x4000);JsonArray values=input.getAsJsonArray("values"),words=input.getAsJsonArray("words");
        if(values.size()!=7||words.size()!=21)throw new IllegalArgumentException("Unexpected safety record shape");
        for(int i=0;i<DOUBLES.length;i++)e.writeDouble(DOUBLES[i],values.get(i).getAsDouble());
        for(int i=0;i<WORDS.length;i++)e.write(WORDS[i],words.get(i).getAsLong(),SIZES[i]);
        pin=words.get(19).getAsLong();now=words.get(20).getAsLong();reads=0;
        e.write(OBJECT+4,FLAGS,4);e.write(OBJECT+8,SENSORS,4);e.write(OBJECT+12,MSP,4);
        e.write(OBJECT+0x30,FLAGS+5,4);e.write(OBJECT+0x50,FLAGS+3,4);e.write(OBJECT+0x70,FLAGS+4,4);
    }
    private Map<String,Object> snapshot(Z119Execution e)throws Exception{
        List<Double> values=new ArrayList<>();for(long address:DOUBLES)values.add(e.readDouble(address));
        List<Long> words=new ArrayList<>();for(int i=0;i<WORDS.length;i++)words.add(e.read(WORDS[i],SIZES[i]));words.add(pin);words.add(now);
        Map<String,Object> result=new LinkedHashMap<>();result.put("values",values);result.put("words",words);result.put("pin_reads",reads);return result;
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input JSON and output JSONL");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();int cases=0,ticks=0;Gson json=new Gson();
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){
            for(JsonElement element:inputs){JsonObject input=element.getAsJsonObject();load(e,input);List<Map<String,Object>> snapshots=new ArrayList<>();
                for(JsonElement command:input.getAsJsonArray("ticks")){
                    JsonArray tick=command.getAsJsonArray();if(tick.size()!=8)throw new IllegalArgumentException("Unexpected tick shape");
                    now=tick.get(0).getAsLong();pin=tick.get(1).getAsLong();e.write(MSP+9,tick.get(2).getAsLong(),1);e.write(MSP+11,tick.get(3).getAsLong(),1);
                    for(int i=0;i<4;i++)e.writeDouble(DOUBLES[i],tick.get(4+i).getAsDouble());
                    e.register(10,OBJECT);e.register(11,now);e.invoke(0x400d9510L);snapshots.add(snapshot(e));ticks++;
                }
                Map<String,Object> record=new LinkedHashMap<>();record.put("id",input.get("id").getAsString());record.put("snapshots",snapshots);out.write(json.toJson(record)+"\n");cases++;
            }
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);report.put("cases",cases);report.put("ticks",ticks);
            report.put("scope","Original Safeties tick and nested monitor start/stop/update bodies; explicit GPIO read, clock, arithmetic and diagnostic boundaries; register windows adapted at ENTRY/RETW");
            report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);report.put("full_cpu_or_hardware_emulation",false);report.put("hardware_tested",false);
            Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+cases+" safety scenarios and "+ticks+" original ticks");
    }
}
