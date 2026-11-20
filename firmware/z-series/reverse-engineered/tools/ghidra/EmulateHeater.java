// Original Z119 heater/PID bodies and timer callbacks with modeled SDK/GPIO.
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

public class EmulateHeater extends GhidraScript {
    private static final long CORE = 0x3ff40000L, STACK = 0x3ff90000L;
    private static final long HEATER = CORE + 0x4940, PID = HEATER + 0x40;
    private static final long STATE = CORE + 0x9d8, SENSORS = CORE + 0xc58, SAFETY = CORE + 0xc50;
    private static final long[] NUMBERS = {
        PID, PID+8, PID+16, PID+24, PID+32, PID+40, PID+72, PID+80,
        PID+96, PID+104, PID+112, PID+120, PID+128,
        STATE+8, STATE+16, STATE+32, STATE+40,
        SENSORS+8, SENSORS+16, SENSORS+96, SENSORS+104, SENSORS+632,
        SENSORS+32, SENSORS+48, SENSORS+64, SENSORS+80
    };
    private final Map<String, Long> hooks = new TreeMap<>(), functions = new TreeMap<>();
    private List<List<Long>> events;
    private long now;
    private int depth;

    private long word(EmulatorHelper e, int register) { return e.readRegister("a" + register).longValue() & 0xffffffffL; }
    private double number(EmulatorHelper e, int register) { return Double.longBitsToDouble(word(e, register) | word(e, register+1) << 32); }
    private void pair(EmulatorHelper e, int register, double value) {
        long bits = Double.doubleToRawLongBits(value);
        e.writeRegister("a" + register, bits & 0xffffffffL); e.writeRegister("a" + (register+1), bits >>> 32);
    }
    private void put(EmulatorHelper e, long address, long value, int size) {
        byte[] bytes = new byte[size];
        for (int i=0; i<size; i++) bytes[i] = (byte) (value >>> (i*8));
        e.writeMemory(toAddr(address), bytes);
    }
    private long get(EmulatorHelper e, long address, int size) throws Exception {
        byte[] bytes = e.readMemory(toAddr(address), size); long result=0;
        for (int i=0; i<size; i++) result |= (bytes[i]&255L) << (i*8);
        return result;
    }
    private void putDouble(EmulatorHelper e, long address, double value) { put(e,address,Double.doubleToRawLongBits(value),8); }
    private double getDouble(EmulatorHelper e, long address) throws Exception { return Double.longBitsToDouble(get(e,address,8)); }
    private void count(String label) { hooks.merge(label,1L,Long::sum); }

    private void boundary(EmulatorHelper e, long target) throws Exception {
        double a,b;
        switch ((int)target) {
            case 0x4012cb84: count("__adddf3"); pair(e,10,number(e,10)+number(e,12)); return;
            case 0x4012ccd8: count("__subdf3"); pair(e,10,number(e,10)-number(e,12)); return;
            case 0x4012cf40: count("__muldf3"); pair(e,10,number(e,10)*number(e,12)); return;
            case 0x4012d144: count("__divdf3"); pair(e,10,number(e,10)/number(e,12)); return;
            case 0x401c01a4: count("__floatsidf"); pair(e,10,(int)word(e,10)); return;
            case 0x4012d3e0:
                count("__fixdfsi"); a=number(e,10);
                if (!Double.isFinite(a) || a<=Integer.MIN_VALUE || a>=Integer.MAX_VALUE) throw new IllegalStateException("Outside integer conversion test domain");
                e.writeRegister("a10",((long)(int)a)&0xffffffffL); return;
            case 0x4012d29c: case 0x4012d350: case 0x4012d2c4: case 0x4012d328:
                count(String.format("ordered_compare_%08x",target)); a=number(e,10); b=number(e,12);
                if (!Double.isFinite(a) || !Double.isFinite(b)) throw new IllegalStateException("Nonfinite comparison input");
                e.writeRegister("a10",(a<b?-1L:a>b?1L:0L)&0xffffffffL); return;
            case 0x40080a9c: count("digitalWrite"); events.add(Arrays.asList(1L,word(e,10),word(e,11))); return;
            case 0x40080904: count("pinMode"); events.add(Arrays.asList(0L,word(e,10),word(e,11))); return;
            case 0x40081388: count("clock_millis"); e.writeRegister("a10",now); return;
            case 0x4008c2b4: count("basename"); return;
            case 0x400d21cc: count("diagnostic_log"); e.writeRegister("a10",0L); return;
            case 0x400d6c00: count("heater_diagnostics"); return;
            case 0x400fc174: count("shell_registration"); return;
            case 0x401c00b4:
                count("__divsf3");
                float value=Float.intBitsToFloat((int)word(e,10))/Float.intBitsToFloat((int)word(e,11));
                e.writeRegister("a10",Float.floatToRawIntBits(value)&0xffffffffL); return;
            case 0x4012d530: count("__extendsfdf2"); pair(e,10,Float.intBitsToFloat((int)word(e,10))); return;
            case 0x400db014: throw new IllegalStateException("Original heater assertion fired");
            case 0x400d0050: case 0x400d0240: case 0x400d1a40: case 0x40080414:
                invoke(e,target); return;
            default: throw new IllegalStateException(String.format("Unmodeled call %08x",target));
        }
    }

    private void invoke(EmulatorHelper e,long target) throws Exception {
        if (++depth>8) throw new IllegalStateException("Nested execution limit exceeded");
        functions.merge(String.format("0x%08x",target),1L,Long::sum);
        Instruction entry=currentProgram.getListing().getInstructionAt(toAddr(target));
        if (entry==null || !entry.getMnemonicString().equalsIgnoreCase("entry") || entry.getLength()!=3) throw new IllegalStateException("Unexpected prologue");
        byte[] bytes=entry.getBytes();
        int frame=(((bytes[0]&255)|((bytes[1]&255)<<8)|((bytes[2]&255)<<16))>>>12)<<3;
        long[] saved=new long[16]; for(int i=0;i<16;i++) saved[i]=word(e,i);
        long oldPC=e.getExecutionAddress().getOffset();
        for(int i=0;i<16;i++) e.writeRegister("a"+i,0L);
        for(int i=0;i<6;i++) e.writeRegister("a"+(i+2),saved[i+10]);
        e.writeRegister("a1",saved[1]-frame);
        e.getEmulator().setExecuteAddress(target+3);
        long[] returns=null;
        for(int step=0;step<5000;step++) {
            monitor.checkCancelled();
            Address pc=e.getExecutionAddress();
            Instruction instruction=currentProgram.getListing().getInstructionAt(pc);
            if(instruction==null) throw new IllegalStateException("Undecoded instruction "+pc);
            String op=instruction.getMnemonicString().toLowerCase(Locale.ROOT);
            if(op.startsWith("loop")) throw new IllegalStateException("Unmodeled Xtensa hardware loop");
            if(op.startsWith("retw")) {returns=new long[6];for(int i=0;i<6;i++)returns[i]=word(e,i+2);break;}
            if(op.equals("call8")||op.equals("callx8")) {
                long destination;
                if(op.equals("callx8")) {
                    Register register=instruction.getRegister(0);
                    if(register==null) throw new IllegalStateException("Missing indirect-call register");
                    destination=e.readRegister(register).longValue()&0xffffffffL;
                } else {
                    Address[] flows=instruction.getFlows();
                    if(flows.length!=1)throw new IllegalStateException("Ambiguous call");
                    destination=flows[0].getOffset();
                }
                boundary(e,destination);
                e.getEmulator().setExecuteAddress(pc.getOffset()+instruction.getLength());
            } else if(op.startsWith("call")) throw new IllegalStateException("Unexpected call form");
            else if(!e.step(monitor))throw new IllegalStateException("Execution failed at "+pc+": "+e.getLastError());
        }
        if(returns==null)throw new IllegalStateException("Instruction budget exceeded");
        for(int i=0;i<16;i++)e.writeRegister("a"+i,saved[i]);
        for(int i=0;i<6;i++)e.writeRegister("a"+(i+10),returns[i]);
        e.getEmulator().setExecuteAddress(oldPC); depth--;
    }

    private void load(EmulatorHelper e,JsonObject input)throws Exception {
        e.writeMemory(toAddr(CORE),new byte[0x5000]);e.writeMemory(toAddr(STACK),new byte[0x4000]);
        for(int i=0;i<16;i++){e.writeRegister("a"+i,0L);e.writeRegister("f"+i,0L);}
        e.writeRegister("SAR",0L);e.writeRegister("PS",0L);e.writeRegister("a1",STACK+0x2000);
        e.getEmulator().setExecuteAddress(0x40080414L);depth=0;
        JsonArray values=input.getAsJsonArray("values"),words=input.getAsJsonArray("words");
        if(values.size()!=26||words.size()!=17)throw new IllegalArgumentException("Unexpected snapshot shape");
        for(int i=0;i<NUMBERS.length;i++)putDouble(e,NUMBERS[i],values.get(i).getAsDouble());
        put(e,PID+0x3c,words.get(0).getAsLong(),1);put(e,PID+0x40,words.get(1).getAsLong(),4);put(e,PID+0x58,words.get(2).getAsLong(),4);
        put(e,STATE+0x18,words.get(3).getAsLong(),1);put(e,STATE+0x19,words.get(4).getAsLong(),1);
        put(e,HEATER+0xc8,words.get(5).getAsLong(),4);put(e,CORE+0x3cf0,words.get(6).getAsLong(),1);
        put(e,CORE+0x4820,words.get(7).getAsLong(),1);
        for(int i=0;i<3;i++)put(e,SAFETY+i,words.get(8+i).getAsLong(),1);
        for(int i=0;i<4;i++)put(e,SENSORS+0x28+i*16,words.get(11+i).getAsLong(),4);
        now=words.get(15).getAsLong();put(e,HEATER+0x38,words.get(16).getAsLong(),1);
        put(e,0x3ffc2e88L,CORE,4);put(e,CORE+0x4814,SAFETY,4);put(e,CORE+0x4818,SENSORS,4);
        put(e,HEATER+8,STATE,4);put(e,HEATER+12,SAFETY,4);put(e,HEATER+16,SENSORS,4);
        put(e,PID+48,SENSORS+48,4);put(e,PID+52,STATE+32,4);put(e,PID+56,STATE+8,4);
        events=new ArrayList<>();
    }

    private void action(EmulatorHelper e,JsonObject action)throws Exception {
        String kind=action.get("kind").getAsString();JsonArray args=action.getAsJsonArray("args");
        for(int i=10;i<16;i++)e.writeRegister("a"+i,0L);
        long target;
        switch(kind) {
            case "tunings": target=0x400d0094L;e.writeRegister("a10",PID);pair(e,12,args.get(0).getAsDouble());pair(e,14,args.get(1).getAsDouble());putDouble(e,word(e,1),args.get(2).getAsDouble());break;
            case "initialize_pid": target=0x400d0050L;e.writeRegister("a10",PID);break;
            case "mode": target=0x400d0240L;e.writeRegister("a10",PID);e.writeRegister("a11",args.get(0).getAsLong());break;
            case "limits": target=0x400d0180L;e.writeRegister("a10",PID);pair(e,12,args.get(0).getAsDouble());pair(e,14,args.get(1).getAsDouble());break;
            case "direction": target=0x400d0278L;e.writeRegister("a10",PID);e.writeRegister("a11",args.get(0).getAsLong());break;
            case "compute": target=0x40080414L;e.writeRegister("a10",PID);break;
            case "configure_boiler":
                e.writeRegister("a10",HEATER);e.writeRegister("a11",args.get(0).getAsLong());invoke(e,0x400de27cL);
                e.writeRegister("a10",PID);pair(e,12,getDouble(e,HEATER+32));pair(e,14,getDouble(e,HEATER+40));putDouble(e,word(e,1),getDouble(e,HEATER+48));target=0x400d0094L;break;
            case "initialize_outputs": target=0x400fc3e4L;e.writeRegister("a10",HEATER);break;
            case "enable": target=0x400d1a40L;e.writeRegister("a10",HEATER);e.writeRegister("a11",args.get(0).getAsLong());break;
            case "start": target=0x400d6d70L;e.writeRegister("a10",HEATER);break;
            case "stop": target=0x400d1a74L;e.writeRegister("a10",HEATER);break;
            case "target": target=0x400d6e7cL;e.writeRegister("a10",HEATER);pair(e,12,args.get(0).getAsDouble());break;
            case "tick20": target=0x40080540L;break;
            case "tick1": target=0x400805dcL;break;
            default:throw new IllegalArgumentException("Unknown action "+kind);
        }
        invoke(e,target);
    }

    private Map<String,Object> snapshot(EmulatorHelper e)throws Exception {
        List<Double> values=new ArrayList<>();for(long address:NUMBERS)values.add(getDouble(e,address));
        List<Long> words=new ArrayList<>(Arrays.asList(get(e,PID+0x3c,1),get(e,PID+0x40,4),get(e,PID+0x58,4),
            get(e,STATE+0x18,1),get(e,STATE+0x19,1),get(e,HEATER+0xc8,4),get(e,CORE+0x3cf0,1),get(e,CORE+0x4820,1),
            get(e,SAFETY,1),get(e,SAFETY+1,1),get(e,SAFETY+2,1)));
        for(int i=0;i<4;i++)words.add(get(e,SENSORS+0x28+i*16,4));words.add(now);words.add(get(e,HEATER+0x38,1));
        Map<String,Object> record=new LinkedHashMap<>();record.put("values",values);record.put("words",words);record.put("gpio",new ArrayList<>(events));return record;
    }

    @Override public void run()throws Exception {
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input JSON and output JSONL");
        String expected="d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27";
        if(!expected.equals(currentProgram.getExecutableSHA256()))throw new IllegalStateException("Original program identity mismatch");
        for(long entry:new long[]{0x40080540L,0x400805dcL}) {
            if(!disassemble(toAddr(entry)))throw new IllegalStateException("Cannot decode timer callback");
            if(getFunctionAt(toAddr(entry))==null&&createFunction(toAddr(entry),"heater_callback_"+Long.toHexString(entry))==null)throw new IllegalStateException("Cannot define callback");
        }
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();
        Gson json=new GsonBuilder().create();EmulatorHelper e=new EmulatorHelper(currentProgram);int cases=0,steps=0;
        try(BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))) {
            for(JsonElement element:inputs) {
                JsonObject input=element.getAsJsonObject();load(e,input);List<Map<String,Object>> snapshots=new ArrayList<>();
                for(JsonElement command:input.getAsJsonArray("actions")){action(e,command.getAsJsonObject());snapshots.add(snapshot(e));steps++;}
                Map<String,Object> record=new LinkedHashMap<>();record.put("id",input.get("id").getAsString());record.put("snapshots",snapshots);
                out.write(json.toJson(record)+"\n");cases++;
            }
        }finally{e.dispose();}
        Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",expected);report.put("cases",cases);report.put("actions",steps);
        report.put("scope","Original heater/PID/timer bodies; explicit SDK arithmetic/GPIO/clock/diagnostic hooks; nested register windows adapted at function ENTRY/RETW boundaries");
        report.put("original_functions_executed",functions);report.put("modeled_boundaries",hooks);report.put("full_cpu_or_hardware_emulation",false);report.put("hardware_tested",false);
        Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        println("Executed "+cases+" heater cases and "+steps+" original control actions");
    }
}
