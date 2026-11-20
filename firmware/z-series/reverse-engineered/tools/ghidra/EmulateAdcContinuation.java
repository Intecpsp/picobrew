// Full original ADC/thermistor/smoother/assertion execution within Core::tick.
// Non-sensor Core services remain explicit, ordered subsystem boundaries.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.io.*;
import java.util.*;
import java.security.MessageDigest;
import com.google.gson.*;
import ghidra.program.model.listing.Function;
import ghidra.program.model.address.AddressRange;

public class EmulateAdcContinuation extends EmulateCoreFlow {
    private static final long ADC=CORE+0x48b4,SENSORS=0x3ff30000L,WIRE=SENSORS+0x1000,VTABLE=WIRE+0x1000,READ=0x3ffe0300L;
    private JsonArray bytes;private int position;private long returned,tickNow;
    private String assertionFile,assertionExpression;private long assertionLine;
    private long hash(String value){long r=2166136261L;for(char c:value.toCharArray())r=((r^c)*16777619L)&0xffffffffL;return r;}
    private String sha(byte[] bytes)throws Exception{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));}
    private Map<String,Object> row(Object... values){Map<String,Object> r=new LinkedHashMap<>();for(int i=0;i<values.length;i+=2)r.put((String)values[i],values[i+1]);return r;}
    @Override protected void event(long... values){List<Long> r=new ArrayList<>();for(long x:values)r.add((long)(int)x);while(r.size()<4)r.add(0L);events.add(r);}
    @Override protected void tick(int stage){active.countBoundary("Core_tick_stage_"+stage);event(100,stage,tickNow);}
    @Override protected void boundary(Z119Execution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        switch((int)target){
            case 0x40081388:e.countBoundary("clock_millis");event(9,now);e.register(10,now);return;
            case 0x4008c2b4:{e.countBoundary("basename");String s=string(e,e.register(10));e.register(10,e.register(10)+Math.max(s.lastIndexOf('/'),s.lastIndexOf('\\'))+1);return;}
            case 0x400d21cc:
                e.countBoundary("diagnostic_log");
                if(assertionFile!=null){event(6,assertionLine,hash(assertionFile),hash(assertionExpression));return;}
                String format=string(e,e.register(10)),message,tag;int level;
                if(format.contains("reqCount != readCount")){message="reqCount != readCount. Expected "+(int)e.read(e.register(1),4)+", got "+(int)e.read(e.register(1)+4,4);tag="MAX11613";level=2;}
                else if(format.contains("Data expired, resetting")){message="Data expired, resetting ("+(int)e.read(e.register(1),4)+")";tag="NSS";level=1;}
                else throw new IllegalStateException("Unexpected diagnostic "+format);
                event(8,e.register(15),hash(message),level);event(10,hash(tag),hash(string(e,e.register(14))));return;
            case 0x400db014:
                assertionFile=string(e,e.register(10));assertionLine=e.register(11);assertionExpression=string(e,e.register(12));
                e.invoke(target);assertionFile=null;return;
            case 0x401289a0:
                e.countBoundary("SDK_delay");long delay=e.register(10);now=(now+delay)&0xffffffffL;event(7,delay,e.read(0x3ffc2e84L,4),now);return;
            case 0x40125ab4:e.countBoundary("Wire_requestFrom");event(4,e.register(11),e.register(12),1);e.register(10,returned);return;
            case (int)READ:
                e.countBoundary("Wire_read");if(position>=bytes.size())throw new IllegalStateException("Read beyond supplied bytes");
                int value=bytes.get(position++).getAsInt();event(5,value);e.register(10,value);return;
            case 0x400dd908:tick(1);e.invoke(target);return;
            case 0x400dd738:case 0x400dd7c8:case 0x400dd770:case 0x400d6ac0:case 0x40110990:case 0x40110924:case 0x400ee1f0:
                e.invoke(target);return;
            case 0x4013b288:e.countBoundary("SDK_log");e.number(10,Math.log(e.number(10)));return;
            default:super.boundary(e,target);return;
        }
    }
    private void initialize(Z119Execution e,JsonObject item)throws Exception{
        e.resetRegisters();e.clear(CORE,0x1a000);e.clear(SENSORS,0x3000);e.write(0x3ffc4654L,0x119adcL,4);
        JsonArray w=item.getAsJsonArray("words"),v=item.getAsJsonArray("values");events=new ArrayList<>();position=0;bytes=item.getAsJsonArray("bytes");
        now=w.get(3).getAsLong();tickNow=w.get(4).getAsLong();returned=w.get(2).getAsLong();e.write(0x3ffc2e84L,w.get(5).getAsLong(),4);assertionFile=null;
        e.write(CORE+0x734,11,4);e.write(ADC,WIRE,4);e.write(ADC+4,w.get(0).getAsLong(),1);e.write(ADC+8,SENSORS,4);e.write(ADC+12,w.get(1).getAsLong(),1);
        e.write(WIRE,VTABLE,4);e.write(VTABLE+20,READ,4);
        e.write(CORE+0x5d58,STILL,4);e.write(STILL,STILL_VTABLE,4);e.write(STILL_VTABLE+16,STILL_TICK,4);
        for(int i=0;i<4;i++){
            e.write(ADC+32+i*4,w.get(8+i).getAsLong(),4);e.write(ADC+16+i*4,w.get(12+i).getAsLong(),4);
            e.writeDouble(SENSORS+0x20+i*16,v.get(i).getAsDouble());e.write(SENSORS+0x28+i*16,w.get(16+i).getAsLong(),4);
            long s=SENSORS+0x70+i*0x78;int vi=4+i*12,wi=20+i*4;
            for(int j=0;j<10;j++)e.writeDouble(s+j*8,v.get(vi+j).getAsDouble());
            e.writeDouble(s+0x58,v.get(vi+10).getAsDouble());e.writeDouble(s+0x68,v.get(vi+11).getAsDouble());
            int[] offsets={0x50,0x54,0x60,0x70};for(int j=0;j<4;j++)e.write(s+offsets[j],w.get(wi+j).getAsLong(),4);
        }
    }
    private Map<String,Object> result(Z119Execution e,JsonObject item)throws Exception{
        List<Long> w=new ArrayList<>();for(JsonElement value:item.getAsJsonArray("words"))w.add(value.getAsLong());
        List<Double> v=new ArrayList<>();for(JsonElement value:item.getAsJsonArray("values"))v.add(value.getAsDouble());
        w.set(3,now);w.set(5,e.read(0x3ffc2e84L,4));
        for(int i=0;i<4;i++){
            w.set(8+i,e.read(ADC+32+i*4,4));w.set(16+i,e.read(SENSORS+0x28+i*16,4));v.set(i,e.readDouble(SENSORS+0x20+i*16));
            long s=SENSORS+0x70+i*0x78;int vi=4+i*12,wi=20+i*4;
            for(int j=0;j<10;j++)v.set(vi+j,e.readDouble(s+j*8));v.set(vi+10,e.readDouble(s+0x58));v.set(vi+11,e.readDouble(s+0x68));
            int[] offsets={0x50,0x54,0x60,0x70};for(int j=0;j<4;j++)w.set(wi+j,e.read(s+offsets[j],4));
        }
        return row("id",item.get("id").getAsString(),"words",w,"values",v,"events",events);
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input/output paths");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson json=new Gson();int cases=0;
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){
            active=e;
            for(JsonElement element:inputs){JsonObject item=element.getAsJsonObject();initialize(e,item);boolean tick=item.get("kind").getAsString().equals("tick");
                e.register(10,tick?CORE:ADC);e.register(11,tickNow);e.invoke(tick?0x40120120L:0x400dd908L);
                out.write(json.toJson(result(e,item))+"\n");cases++;
            }
            List<Object> functions=new ArrayList<>();for(String address:e.functions.keySet()){
                Function f=getFunctionAt(toAddr(Long.parseLong(address.substring(2),16)));if(f==null)throw new IllegalStateException("Missing function "+address);
                List<Object> ranges=new ArrayList<>();for(AddressRange range:f.getBody().getAddressRanges()){
                    byte[] bytes=new byte[(int)range.getLength()];currentProgram.getMemory().getBytes(range.getMinAddress(),bytes);
                    ranges.add(row("address",range.getMinAddress().getOffset(),"size",bytes.length,"sha256",sha(bytes)));}
                functions.add(row("address",address,"calls",e.functions.get(address),"ranges",ranges));
            }
            Map<String,Object> report=row("input_sha256",Z119Execution.IMAGE_SHA,"cases",cases,"input_file_sha256",sha(Files.readAllBytes(Path.of(args[0]))),
                "scope","Complete original ADC poll and Core tick with actual thermistor, smoother and returning assertion callees; other Core services are ordered explicit boundaries. SDK Wire, printf, clock, delay, compiler arithmetic and log are modeled. Compare original continuation, counter, timing, raw data and all published/smoother fields.",
                "original_functions",functions,"original_functions_executed",e.functions,"modeled_boundaries",e.boundaries,"hardware_tested",false,"full_cpu_or_hardware_emulation",false);
            Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+cases+" original ADC/Core continuation cases");
    }
}
