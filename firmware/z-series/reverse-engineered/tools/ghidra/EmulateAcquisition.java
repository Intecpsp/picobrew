// Original Z119 ADC bus transactions and noise-smoother instruction execution.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.io.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.emulator.EmulatorHelper;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Instruction;

public class EmulateAcquisition extends GhidraScript {
    private static final long OBJECT=0x3ff50000L, WIRE=OBJECT+0x1000, VTABLE=WIRE+0x1000, STACK=0x3ff90000L;
    private static final long WRITE=0x3ffe0100L, READ=0x3ffe0104L, INPUT=OBJECT+0x200;
    private final Map<String,Long> hooks=new TreeMap<>(), functions=new TreeMap<>();
    private List<List<Long>> events;
    private JsonArray bytes;
    private int readIndex, depth;
    private long now, requestCount;
    private long word(EmulatorHelper e,int r){return e.readRegister("a"+r).longValue()&0xffffffffL;}
    private double number(EmulatorHelper e,int r){return Double.longBitsToDouble(word(e,r)|word(e,r+1)<<32);}
    private void pair(EmulatorHelper e,int r,double value){long bits=Double.doubleToRawLongBits(value);e.writeRegister("a"+r,bits&0xffffffffL);e.writeRegister("a"+(r+1),bits>>>32);}
    private void put(EmulatorHelper e,long address,long value,int size){byte[] out=new byte[size];for(int i=0;i<size;i++)out[i]=(byte)(value>>>(8*i));e.writeMemory(toAddr(address),out);}
    private long get(EmulatorHelper e,long address,int size)throws Exception{byte[] raw=e.readMemory(toAddr(address),size);long value=0;for(int i=0;i<size;i++)value|=(raw[i]&255L)<<(8*i);return value;}
    private void putDouble(EmulatorHelper e,long address,double value){put(e,address,Double.doubleToRawLongBits(value),8);}
    private double getDouble(EmulatorHelper e,long address)throws Exception{return Double.longBitsToDouble(get(e,address,8));}
    private void count(String label){hooks.merge(label,1L,Long::sum);}
    private void boundary(EmulatorHelper e,long target)throws Exception{
        double a,b;
        switch((int)target){
            case 0x4012cb84:count("__adddf3");pair(e,10,number(e,10)+number(e,12));return;
            case 0x4012ccd8:count("__subdf3");pair(e,10,number(e,10)-number(e,12));return;
            case 0x4012d144:count("__divdf3");pair(e,10,number(e,10)/number(e,12));return;
            case 0x401c0198:count("__floatunsidf");pair(e,10,word(e,10));return;
            case 0x401c00b4:count("__divsf3");e.writeRegister("a10",Float.floatToRawIntBits(Float.intBitsToFloat((int)word(e,10))/Float.intBitsToFloat((int)word(e,11)))&0xffffffffL);return;
            case 0x4012d530:count("__extendsfdf2");pair(e,10,Float.intBitsToFloat((int)word(e,10)));return;
            case 0x40081388:count("clock_millis");e.writeRegister("a10",now);return;
            case 0x4008c2b4:count("basename");return;
            case 0x400d21cc:count("diagnostic_log");return;
            case 0x400d5a44:case 0x400d5a68:count("diagnostic_scope");return;
            case 0x400fabe0:count("diagnostic_command_registration");return;
            case 0x400db014:throw new IllegalStateException("Original acquisition assertion fired");
            case 0x401bfe3c:count("beginTransmission");events.add(Arrays.asList(0L,word(e,11)));return;
            case (int)WRITE:count("write");events.add(Arrays.asList(1L,word(e,11)&255));e.writeRegister("a10",1L);return;
            case 0x40125ac8:count("endTransmission_default_stop");events.add(Arrays.asList(2L,1L));e.writeRegister("a10",0L);return;
            case 0x401289a0:count("delay_ms");events.add(Arrays.asList(3L,word(e,10)));return;
            case 0x40125ab4:count("requestFrom_default_stop");events.add(Arrays.asList(4L,word(e,11),word(e,12),1L));e.writeRegister("a10",requestCount);return;
            case (int)READ:
                count("read");if(readIndex>=bytes.size())throw new IllegalStateException("Original read exceeded supplied bytes");
                long value=bytes.get(readIndex++).getAsLong();events.add(Arrays.asList(5L,value));e.writeRegister("a10",value&0xffffffffL);return;
            case 0x400dd68c:case 0x400dd708:case 0x400ee1f0:case 0x40110924:invoke(e,target);return;
            default:throw new IllegalStateException(String.format("Unmodeled acquisition call %08x",target));
        }
    }
    private void invoke(EmulatorHelper e,long target)throws Exception{
        if(++depth>8)throw new IllegalStateException("Nested execution limit exceeded");
        functions.merge(String.format("0x%08x",target),1L,Long::sum);
        Instruction entry=currentProgram.getListing().getInstructionAt(toAddr(target));
        if(entry==null||!entry.getMnemonicString().equalsIgnoreCase("entry")||entry.getLength()!=3)throw new IllegalStateException("Unexpected prologue");
        byte[] raw=entry.getBytes();int frame=(((raw[0]&255)|((raw[1]&255)<<8)|((raw[2]&255)<<16))>>>12)<<3;
        long[] saved=new long[16];for(int i=0;i<16;i++)saved[i]=word(e,i);
        long oldPC=e.getExecutionAddress().getOffset();
        for(int i=0;i<16;i++)e.writeRegister("a"+i,0L);
        for(int i=0;i<6;i++)e.writeRegister("a"+(i+2),saved[i+10]);
        e.writeRegister("a1",saved[1]-frame);e.getEmulator().setExecuteAddress(target+3);long[] returns=null;
        for(int step=0;step<4000;step++){
            monitor.checkCancelled();Address pc=e.getExecutionAddress();
            Instruction instruction=currentProgram.getListing().getInstructionAt(pc);
            if(instruction==null)throw new IllegalStateException("Undecoded instruction "+pc);
            String op=instruction.getMnemonicString().toLowerCase(Locale.ROOT);
            if(op.startsWith("loop"))throw new IllegalStateException("Unmodeled Xtensa hardware loop");
            if(op.startsWith("retw")){returns=new long[6];for(int i=0;i<6;i++)returns[i]=word(e,i+2);break;}
            if(op.equals("call8")||op.equals("callx8")){
                long destination;
                if(op.equals("callx8")){Register r=instruction.getRegister(0);if(r==null)throw new IllegalStateException("Missing call register");destination=e.readRegister(r).longValue()&0xffffffffL;}
                else{Address[] flows=instruction.getFlows();if(flows.length!=1)throw new IllegalStateException("Ambiguous call");destination=flows[0].getOffset();}
                boundary(e,destination);e.getEmulator().setExecuteAddress(pc.getOffset()+instruction.getLength());
            }else if(op.startsWith("call"))throw new IllegalStateException("Unexpected call form");
            else if(!e.step(monitor))throw new IllegalStateException("Execution failed at "+pc+": "+e.getLastError());
        }
        if(returns==null)throw new IllegalStateException("Instruction budget exceeded");
        for(int i=0;i<16;i++)e.writeRegister("a"+i,saved[i]);
        for(int i=0;i<6;i++)e.writeRegister("a"+(i+10),returns[i]);
        e.getEmulator().setExecuteAddress(oldPC);depth--;
    }
    private void reset(EmulatorHelper e)throws Exception{
        e.writeMemory(toAddr(OBJECT),new byte[0x3000]);e.writeMemory(toAddr(STACK),new byte[0x4000]);
        for(int i=0;i<16;i++){e.writeRegister("a"+i,0L);e.writeRegister("f"+i,0L);}
        e.writeRegister("SAR",0L);e.writeRegister("PS",0L);e.writeRegister("a1",STACK+0x2000);
        e.getEmulator().setExecuteAddress(0x400dd908L);depth=0;readIndex=0;events=new ArrayList<>();now=5000;
    }
    private Map<String,Object> adc(EmulatorHelper e,JsonObject input)throws Exception{
        put(e,OBJECT,WIRE,4);put(e,OBJECT+4,input.get("address").getAsLong(),1);put(e,WIRE,VTABLE,4);
        put(e,VTABLE+8,WRITE,4);put(e,VTABLE+0x14,READ,4);
        put(e,OBJECT+12,input.get("enabled").getAsLong(),1);
        for(int i=0;i<4;i++){put(e,OBJECT+16+i*4,input.getAsJsonArray("types").get(i).getAsLong(),4);put(e,OBJECT+32+i*4,input.getAsJsonArray("raw").get(i).getAsLong(),4);}
        e.writeRegister("a10",OBJECT);
        if(input.get("kind").getAsString().equals("setup")){e.writeRegister("a11",input.get("boiler").getAsLong());invoke(e,0x400fad30L);}
        else{requestCount=input.get("request_count").getAsLong();bytes=input.getAsJsonArray("bytes");invoke(e,0x400dd908L);}
        List<Long> types=new ArrayList<>(),raw=new ArrayList<>();
        for(int i=0;i<4;i++){types.add(get(e,OBJECT+16+i*4,4));raw.add((long)(int)get(e,OBJECT+32+i*4,4));}
        Map<String,Object> result=new LinkedHashMap<>();result.put("types",types);result.put("raw",raw);
        result.put("enabled",get(e,OBJECT+12,1));result.put("events",events);return result;
    }
    private Map<String,Object> smoother(EmulatorHelper e,JsonObject input)throws Exception{
        JsonArray values=input.getAsJsonArray("values"),words=input.getAsJsonArray("words");
        if(values.size()!=12||words.size()!=4)throw new IllegalArgumentException("Bad smoother input shape");
        for(int i=0;i<10;i++)putDouble(e,OBJECT+i*8,values.get(i).getAsDouble());
        putDouble(e,OBJECT+0x58,values.get(10).getAsDouble());putDouble(e,OBJECT+0x68,values.get(11).getAsDouble());
        int[] offsets={0x50,0x54,0x60,0x70};for(int i=0;i<4;i++)put(e,OBJECT+offsets[i],words.get(i).getAsLong(),4);
        List<Map<String,Object>> snapshots=new ArrayList<>();
        for(JsonElement item:input.getAsJsonArray("updates")){
            JsonArray update=item.getAsJsonArray();now=update.get(0).getAsLong();putDouble(e,INPUT,update.get(1).getAsDouble());
            e.writeRegister("a10",OBJECT);e.writeRegister("a11",INPUT);invoke(e,0x40110990L);
            List<Double> numbers=new ArrayList<>();for(int i=0;i<10;i++)numbers.add(getDouble(e,OBJECT+i*8));
            numbers.add(getDouble(e,OBJECT+0x58));numbers.add(getDouble(e,OBJECT+0x68));
            List<Long> integers=new ArrayList<>();for(int offset:offsets)integers.add(get(e,OBJECT+offset,4));
            Map<String,Object> snapshot=new LinkedHashMap<>();snapshot.put("values",numbers);snapshot.put("words",integers);snapshots.add(snapshot);
        }
        Map<String,Object> result=new LinkedHashMap<>();result.put("snapshots",snapshots);return result;
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input JSON and output JSONL");
        String expected="d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27";
        if(!expected.equals(currentProgram.getExecutableSHA256()))throw new IllegalStateException("Original program identity mismatch");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();
        Gson json=new Gson();EmulatorHelper emulator=new EmulatorHelper(currentProgram);int cases=0,updates=0;
        try(BufferedWriter output=Files.newBufferedWriter(Path.of(args[1]))){
            for(JsonElement element:inputs){JsonObject input=element.getAsJsonObject();reset(emulator);
                boolean smooth=input.get("kind").getAsString().equals("smooth");
                Map<String,Object> record=smooth?smoother(emulator,input):adc(emulator,input);
                record.put("id",input.get("id").getAsString());output.write(json.toJson(record)+"\n");cases++;
                if(smooth)updates+=input.getAsJsonArray("updates").size();
            }
        }finally{emulator.dispose();}
        Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",expected);report.put("cases",cases);report.put("smoother_updates",updates);
        report.put("scope","Original ADC setup/poll and noise-smoother instructions; explicit TwoWire, clock, compiler arithmetic, and diagnostic boundaries; register windows adapted at ENTRY/RETW");
        report.put("original_functions_executed",functions);report.put("modeled_boundaries",hooks);report.put("hardware_tested",false);report.put("full_cpu_or_hardware_emulation",false);
        Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        println("Executed "+cases+" acquisition cases and "+updates+" smoother updates");
    }
}
