// Execute original Z119 Button, physical Encoder and interrupt producers.
import java.nio.file.*;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulatePhysicalInputs extends GhidraScript {
    private static final long CORE=0x3ff00000L, ENCODER=CORE+0x158d0, BUTTON=ENCODER+0xb0, QUEUE=ENCODER+0x14;
    private static final long SINK=0x7ff01000L, VTABLE=SINK+0x100, BUTTON_SINK=SINK+0x200, ROTATION_SINK=SINK+0x204, CLOSURE=SINK+0x300;
    private static final long[] NESTED={0x400d0364L,0x400f06a4L,0x400ee2fcL,0x400e5f08L,0x400f0670L,
        0x400f0568L,0x400f077cL,0x400fb8a4L,0x40080808L,0x40080828L,0x4008c210L,0x400f0804L,0x400d198cL,
        0x400f068cL,0x400d19acL,0x400d19bcL,0x400d19ccL,0x400d19dcL,0x400d19ecL,0x400d19fcL,0x400d1a0cL,0x400d1a1cL};
    private final Set<Long> nested=new HashSet<>();
    private List<List<Long>> events;
    private JsonArray reads;
    private int position;
    private long clock,buttonAssertion,encoderAssertion;
    private void record(long... words){List<Long> event=new ArrayList<>();for(long value:words)event.add(value&0xffffffffL);events.add(event);}
    private long value(JsonArray values,int i){return values.get(i).getAsLong()&0xffffffffL;}
    private void boundary(Z119Execution e,long target)throws Exception{
        if(nested.contains(target)){e.invoke(target);return;}
        if(e.arithmetic(target))return;
        switch((int)target){
        case 0x40081388:{
            long pc=e.emulator.getExecutionAddress().getOffset();
            // EventQ diagnostic timestamps are an explicit logging boundary;
            // they do not advance the independent functional clock fixture.
            if((pc>=0x400f0568L&&pc<0x400f0670L)||(pc>=0x400f077cL&&pc<0x400f0804L)){
                e.countBoundary("diagnostic_clock");e.register(10,clock);return;
            }
            e.countBoundary("functional_millis");long result=clock;clock=(clock+1)&0xffffffffL;record(3,result);e.register(10,result);return;
        }
        case 0x40080904:e.countBoundary("Arduino_pinMode");record(0,e.register(10),e.register(11));return;
        case 0x40080b04:{
            e.countBoundary("Arduino_digitalRead");long result=position<reads.size()?value(reads,position++):1;
            record(1,e.register(10),result);e.register(10,result);return;
        }
        case 0x40127300:{
            e.countBoundary("Arduino_attachInterrupt");long callback=e.register(11);
            if(callback!=0x40080828L&&callback!=0x4008c210L)throw new IllegalStateException("Unknown interrupt callback");
            record(2,e.register(10),callback==0x40080828L?0:1,e.register(12));return;
        }
        case 0x400fb664:e.countBoundary("encoder_console_registry");if(e.register(10)!=ENCODER)throw new IllegalStateException("Wrong registry owner");record(6);return;
        case (int)BUTTON_SINK:{
            e.countBoundary("typed_button_sink");if(e.register(10)!=SINK)throw new IllegalStateException("Wrong button sink");long event=e.register(11);
            record(4,e.read(event+4,4),e.read(event+8,1),e.read(event+12,4));return;
        }
        case (int)ROTATION_SINK:{
            e.countBoundary("typed_rotation_sink");if(e.register(10)!=SINK)throw new IllegalStateException("Wrong rotation sink");
            record(5,e.read(e.register(11)+4,4));return;
        }
        case 0x400d0398:e.countBoundary("button_event_diagnostic_format");e.write(e.register(11),'B',1);e.write(e.register(11)+1,0,1);return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:e.countBoundary("diagnostic_log");return;
        case 0x400db014:e.countBoundary("returning_assertion");if(e.register(11)==0x9b)encoderAssertion=e.register(11);else buttonAssertion=e.register(11);return;
        case 0x401367a0:throw new IllegalStateException("Original stack guard failed");
        default:throw new IllegalStateException(String.format("Unmodeled physical input call %08x",target));
        }
    }
    private void load(Z119Execution e,JsonObject input)throws Exception{
        e.resetRegisters();e.clear(CORE,0x1b000);e.clear(SINK,0x400);
        e.write(0x3ffc4654L,0x1191beefL,4);e.write(0x3ffc2e88L,CORE,4);
        e.write(SINK,VTABLE,4);e.write(VTABLE,BUTTON_SINK,4);e.write(VTABLE+4,ROTATION_SINK,4);e.write(CLOSURE,ENCODER,4);
        e.write(ENCODER+0xac,SINK,4);e.write(BUTTON+4,QUEUE,4);
        e.write(QUEUE+4,0x3f41fb38L,4);e.write(QUEUE+0x95,0,1);
        e.emulator.writeMemory(e.address(ENCODER+8),e.emulator.readMemory(e.address(0x3f41f03cL),9));
        JsonArray w=input.getAsJsonArray("words");
        e.write(BUTTON+0x10,value(w,0),4);e.write(BUTTON+0xc,value(w,1),4);e.write(BUTTON+0x14,value(w,2),1);
        e.write(BUTTON+0x18,value(w,3),4);e.write(BUTTON+0x1c,value(w,4),4);e.write(BUTTON+0x20,value(w,5),4);
        e.write(QUEUE+0x14,value(w,6),1);e.write(QUEUE+8,value(w,7),4);e.write(QUEUE+12,value(w,8),1);e.write(QUEUE+16,value(w,9),4);
        e.write(ENCODER+0xd4,value(w,10),1);e.write(ENCODER+0xd5,value(w,11),1);e.write(ENCODER+0xd6,value(w,12),1);
        e.write(ENCODER+0xd8,value(w,13),4);e.write(ENCODER+0xdc,value(w,14),1);e.write(ENCODER+0xdd,value(w,15),1);
        e.write(ENCODER+0x1de,value(w,16),2);e.write(ENCODER+0x1e0,value(w,17),2);e.write(ENCODER+0x1e4,value(w,18),4);
        buttonAssertion=value(w,19);encoderAssertion=value(w,20);clock=value(w,21);
        JsonArray ring=input.getAsJsonArray("ring");for(int i=0;i<256;i++)e.write(ENCODER+0xde+i,value(ring,i),1);
        reads=input.getAsJsonArray("reads");position=0;events=new ArrayList<>();
    }
    private Map<String,Object> runCase(Z119Execution e,JsonObject input)throws Exception{
        load(e,input);List<Long> results=new ArrayList<>();
        for(JsonElement element:input.getAsJsonArray("steps")){
            JsonArray step=element.getAsJsonArray();int op=step.get(0).getAsInt();long a=value(step,1),b=value(step,2),target;
            e.register(10,op<5?BUTTON:ENCODER);e.register(11,a);e.register(12,b);
            switch(op){
            case 0:target=0x400d0364L;break;case 1:target=0x400f06a4L;break;case 2:target=0x400ee2fcL;break;
            case 3:target=0x400e5f08L;break;case 4:target=0x400f0670L;break;case 5:target=0x400fb8a4L;break;
            case 6:target=0x40080808L;break;case 7:target=0x40080828L;break;case 8:target=0x400f0804L;break;
            case 9:target=0x400d198cL;break;case 10:target=0x400f068cL;e.register(10,CLOSURE);break;
            case 11:{int delta=(int)a;if(delta==0||Math.abs(delta)>4)throw new IllegalArgumentException("Unsupported console delta");
                target=delta<0?0x400d19acL+(-delta-1)*16:0x400d19ecL+(delta-1)*16;e.register(10,CLOSURE);break;}
            case 12:target=0x4008c210L;break;
            default:throw new IllegalArgumentException("Unknown operation");
            }
            e.invoke(target);results.add(op==6?e.register(10)&255:0L);
        }
        List<Long> words=new ArrayList<>(Arrays.asList(e.read(BUTTON+0x10,4),e.read(BUTTON+0xc,4),e.read(BUTTON+0x14,1),
            e.read(BUTTON+0x18,4),e.read(BUTTON+0x1c,4),e.read(BUTTON+0x20,4),e.read(QUEUE+0x14,1),e.read(QUEUE+8,4),
            e.read(QUEUE+12,1),e.read(QUEUE+16,4),e.read(ENCODER+0xd4,1),e.read(ENCODER+0xd5,1),e.read(ENCODER+0xd6,1),
            e.read(ENCODER+0xd8,4),e.read(ENCODER+0xdc,1),e.read(ENCODER+0xdd,1),e.read(ENCODER+0x1de,2),e.read(ENCODER+0x1e0,2),
            e.read(ENCODER+0x1e4,4),buttonAssertion,encoderAssertion,clock,(long)position));
        List<Long> ring=new ArrayList<>();for(int i=0;i<256;i++)ring.add(e.read(ENCODER+0xde+i,1));
        Map<String,Object> output=new LinkedHashMap<>();output.put("id",input.get("id").getAsString());output.put("words",words);
        output.put("ring",ring);output.put("results",results);output.put("events",events);return output;
    }
    private String sha(Path path)throws Exception{
        byte[] bytes=MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(path));StringBuilder out=new StringBuilder();
        for(byte value:bytes)out.append(String.format("%02x",value&255));return out.toString();
    }
    @Override public void run()throws Exception{
        for(long address:NESTED)nested.add(address);
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input JSON and output JSONL");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson json=new Gson();int count=0;
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);
            BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){
            for(JsonElement item:inputs){out.write(json.toJson(runCase(e,item.getAsJsonObject()))+"\n");count++;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);
            report.put("input_file_sha256",sha(Path.of(args[0])));report.put("cases",count);
            report.put("scope","Original Button setup/edges/state/timing/event construction and one-slot publication; physical Encoder GPIO/setup/ISR/ring decoder/tick/forwarding/output and console callbacks. GPIO/interrupt SDK, console registry allocation, event sinks and diagnostics are explicit boundaries. Logging clock reads do not advance the functional fixture clock; MEMW is single-thread only. No concurrent interrupt or hardware qualification.");
            report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("hardware_tested",false);report.put("full_cpu_emulation",false);
            Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+count+" original physical input cases");
    }
}
