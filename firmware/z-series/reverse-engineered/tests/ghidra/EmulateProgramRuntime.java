// Original Z119 runtime instructions with explicit SDK and device boundaries.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.emulator.EmulatorHelper;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.scalar.Scalar;

/** Bounded function execution; no peripheral or full-CPU emulation claim. */
public class EmulateProgramRuntime extends GhidraScript {
    private static final long STACK=0x3ff80000L, OBJECT=0x3ff84000L, STATE=0x3ff85000L,
        CORE=0x3ff86000L, PROGRAM=0x3ff88000L, STRING_PTRS=0x3ff88400L,
        STRINGS=0x3ff88500L, INSTR=0x3ff89000L, NAMES=0x3ff89800L,
        STILL=0x3ff89a00L, VTABLE=0x3ff89b00L, MSP=0x3ff89c00L, HEATER=0x3ff89d00L,
        REPOSITORY=0x3ff89e00L, REPOSITORY_STATE=0x3ff89f00L, PROGRAM_CODE=0x3ff8a000L;
    private static final Set<Long> ALLOWED=Set.of(0x400d12b0L,0x400e0374L,0x400e0464L,
        0x400dff64L,0x400dff0cL,0x400e6bf8L,0x400e6240L,0x400e62d8L,0x400e6364L,0x400e64d0L,
        0x400e6428L,0x400e660cL,0x400e6124L,0x400f46e8L,0x400ef038L,0x400d0ffcL,
        0x400d2084L,0x400dc504L,0x4010a48cL,0x400e03a8L,0x400e06fcL,0x400e0fb4L,
        0x400e0fe4L,0x401bf64cL,0x401bf4acL,0x401bf4c8L,0x401bf29cL,0x400ee998L,
        0x40100954L,0x40100938L,0x40100830L,0x401007f0L,0x4010a168L,
        0x401bf40cL,0x400dee30L,0x400def90L,0x400daf04L,0x400d742cL,0x400d20acL,
        0x400e05c8L,0x400e04a0L,0x400e02f4L,0x400e01d8L,0x400e0348L,0x401bf3d4L,
        0x401bf4e8L,0x400ec360L,0x400ec5fcL,0x400d106cL,0x400d1040L,0x400e0ce8L,0x4010b200L);
    private final Map<String,Long> boundaries=new TreeMap<>();
    private final Set<Long> bodies=new TreeSet<>();
    private final List<Object> events=new ArrayList<>();
    private EmulatorHelper emu;
    private JsonArray wordFields,doubleFields;
    private long clock;
    private boolean ready;
    private int budget;
    private static class OriginalAssertion extends Exception {
        final int line;
        OriginalAssertion(int value) { super("Original assertion line "+value); line=value; }
    }
    private long reg(int n) { return emu.readRegister("a"+n).longValue() & 0xffffffffL; }
    private void put(int n,long v) { emu.writeRegister("a"+n,v & 0xffffffffL); }
    private void word(long a,long v) throws Exception {
        emu.writeMemory(toAddr(a),new byte[]{(byte)v,(byte)(v>>>8),(byte)(v>>>16),(byte)(v>>>24)});
    }
    private long word(long a) throws Exception {
        byte[] b=emu.readMemory(toAddr(a),4);
        return (b[0]&255L)|((b[1]&255L)<<8)|((b[2]&255L)<<16)|((b[3]&255L)<<24);
    }
    private void real(long a,double v) throws Exception { long b=Double.doubleToRawLongBits(v); word(a,b);word(a+4,b>>>32); }
    private double real(long a) throws Exception { return Double.longBitsToDouble(word(a)|(word(a+4)<<32)); }
    private double fp(int n) { return Double.longBitsToDouble(reg(n)|(reg(n+1)<<32)); }
    private void fpResult(double v) { long b=Double.doubleToRawLongBits(v);put(10,b);put(11,b>>>32); }
    private void text(long a,String value) throws Exception {
        byte[] bytes=(value+"\0").getBytes(StandardCharsets.UTF_8); emu.writeMemory(toAddr(a),bytes);
    }
    private String text(long a) throws Exception {
        if(a==0) return "<null>";
        var b=new java.io.ByteArrayOutputStream();
        for(int i=0;i<4096;i++){int c=emu.readMemoryByte(toAddr(a+i))&255;if(c==0)return b.toString(StandardCharsets.UTF_8);b.write(c);}
        throw new IllegalStateException("Unterminated fixture string");
    }
    private static String hex(byte[] b) { return HexFormat.of().formatHex(b); }
    private void boundary(String name) { boundaries.merge(name,1L,Long::sum); }
    private void event(long kind,long a,long b,long c) { events.add(List.of(kind,a&0xffffffffL,b&0xffffffffL,c&0xffffffffL)); }
    private static Map<String,Object> row(Object... v) {
        Map<String,Object> r=new LinkedHashMap<>(); for(int i=0;i<v.length;i+=2)r.put((String)v[i],v[i+1]);return r;
    }
    private long address(JsonObject field) {
        String region=field.get("region").getAsString();
        long base=region.equals("state")?STATE:region.equals("core")||region.equals("error")?CORE:OBJECT;
        return base+field.get("offset").getAsLong();
    }
    private void setup(JsonObject input,JsonArray strings) throws Exception {
        for(int i=0;i<16;i++){put(i,0);emu.writeRegister("f"+i,0L);}
        for(String n:List.of("SAR","PS","LCOUNT","LBEG","LEND"))emu.writeRegister(n,0L);
        emu.writeMemory(toAddr(STACK-0x4000),new byte[0xf000]);
        word(OBJECT+0x100,MSP);word(OBJECT+0x104,HEATER);word(OBJECT+0x108,STILL);
        word(OBJECT+0x10c,STATE);word(OBJECT+0x110,CORE);word(OBJECT+0x924,PROGRAM);
        word(OBJECT+0xfc,REPOSITORY);word(REPOSITORY,REPOSITORY_STATE);real(REPOSITORY_STATE+0x78,1.625);
        word(OBJECT+0x940,STATE+0x1d8);word(OBJECT+0x944,CORE+0x958);
        word(OBJECT+0xf8,0x3ff89080L);word(CORE+0xa00,1);word(CORE+0xab0,1);
        word(MSP+20,CORE+0x2d8);word(CORE+0x2dc,2);
        word(STILL,VTABLE);word(VTABLE+8,0x7ff00008L);word(VTABLE+0x14,0x7ff00014L);word(VTABLE+0x18,0x7ff00018L);
        word(PROGRAM+12,STRING_PTRS);word(PROGRAM+16,strings.size());
        text(NAMES+64,"Fixture error");text(NAMES+96,"");text(NAMES+128,"Fixture program");
        word(PROGRAM+8,NAMES+128);word(PROGRAM+20,PROGRAM_CODE);word(PROGRAM+24,3);
        long[][] code={{1,0},{13,30},{9,0}};for(int i=0;i<code.length;i++){word(PROGRAM_CODE+8*i,code[i][0]);word(PROGRAM_CODE+8*i+4,code[i][1]);}
        for(int i=0;i<strings.size();i++){word(STRING_PTRS+4*i,STRINGS+128*i);text(STRINGS+128*i,strings.get(i).getAsString());}
        text(NAMES,"initial");text(NAMES+32,"initial sub");word(STATE+0x14,NAMES);word(STATE+0x6c,NAMES+32);
        text(STATE+0x18,"buffer");word(STATE+0x18+0x44,6);word(STATE+0x18+0x48,58);
        JsonObject words=input.getAsJsonObject("words"),doubles=input.getAsJsonObject("doubles");
        for(JsonElement e:wordFields){JsonObject f=e.getAsJsonObject();String n=f.get("name").getAsString();
            long v=(words.has(n)?words.get(n):f.get("default")).getAsLong();
            if(f.get("region").getAsString().equals("map"))v=v==0?0x3f425568L:0x3f42556fL;
            if(f.get("region").getAsString().equals("error"))v=v==0?0:NAMES+64;
            if(f.get("width").getAsInt()==1)emu.writeMemory(toAddr(address(f)),new byte[]{(byte)v});else word(address(f),v);
        }
        for(JsonElement e:doubleFields){JsonObject f=e.getAsJsonObject();String n=f.get("name").getAsString();
            real(address(f),(doubles.has(n)?doubles.get(n):f.get("default")).getAsDouble());}
        emu.writeMemory(toAddr(OBJECT+0x70),new byte[]{(byte)(word(OBJECT+0x14)==0?0:1)});
        word(0x3ffc4654L,0x5a5a5a5aL);
        JsonArray stack=input.getAsJsonArray("stack");word(OBJECT+0x918,stack.size());
        for(int i=0;i<stack.size();i++){JsonArray pair=stack.get(i).getAsJsonArray();word(OBJECT+0x118+8*i,pair.get(0).getAsLong());word(OBJECT+0x11c+8*i,pair.get(1).getAsLong());}
        clock=input.get("now").getAsLong();ready=input.get("ready").getAsInt()!=0;
        events.clear();budget=200000;put(1,STACK);
    }
    private boolean sdk(long target) throws Exception {
        switch((int)target){
        case 0x40081388:boundary("millis");put(10,clock);return true;
        case 0x4008c2b4:boundary("basename_for_diagnostics");return true;
        case 0x401c00b4:boundary("__divsf3_for_diagnostics");put(10,0);return true;
        case 0x4012d530:boundary("__extendsfdf2_for_diagnostics");fpResult(0);return true;
        case 0x400d21cc:case 0x400e0044:case 0x400d7264:case 0x400d5a44:case 0x400d5a68:
        case 0x400de56c:boundary("diagnostic_output");return true;
        case 0x400db014:boundary("OEM_assertion");throw new OriginalAssertion((int)reg(11));
        case 0x401c0198:boundary("__floatunsidf");fpResult((double)reg(10));return true;
        case 0x401c01a4:boundary("__floatsidf");fpResult((double)(int)reg(10));return true;
        case 0x4012cb84:boundary("__adddf3");fpResult(fp(10)+fp(12));return true;
        case 0x4012ccd8:boundary("__subdf3");fpResult(fp(10)-fp(12));return true;
        case 0x4012cf40:boundary("__muldf3");fpResult(fp(10)*fp(12));return true;
        case 0x4012d144:boundary("__divdf3");fpResult(fp(10)/fp(12));return true;
        case 0x4012d29c:case 0x4012d328:
            boundary(target==0x4012d29cL?"__gtdf2":"__gedf2");
            put(10,Double.isNaN(fp(10))||Double.isNaN(fp(12))?-1:fp(10)>fp(12)?1:fp(10)<fp(12)?-1:0);return true;
        case 0x4012d350:case 0x4012d2c4:
            boundary(target==0x4012d350L?"__ltdf2":"__ledf2");
            put(10,Double.isNaN(fp(10))||Double.isNaN(fp(12))?1:fp(10)>fp(12)?1:fp(10)<fp(12)?-1:0);return true;
        case 0x4012d42c:boundary("__fixunsdfsi_finite_fixture_domain");
            double d=fp(10);if(!Double.isFinite(d)||d<0||d>0xffffffffL)throw new IllegalStateException("Unreviewed unsigned conversion");
            put(10,(long)d);return true;
        case 0x4012d3e0:boundary("__fixdfsi_finite_fixture_domain");put(10,(int)fp(10));return true;
        case 0x400d1a40:boundary("Heater.enable");event(1,reg(11),0,0);return true;
        case 0x400d6d70:boundary("Heater.startPID");event(2,0,0,0);return true;
        case 0x400d1a74:boundary("Heater.stopPID");event(3,0,0,0);return true;
        case 0x400d6e7c:boundary("Heater.setTarget");event(4,reg(12),reg(13),0);return true;
        case 0x7ff00014:boundary("PicoStill.command");event(5,reg(11),reg(12),0);return true;
        case 0x7ff00008:boundary("PicoStill.isReady");event(6,0,0,0);put(10,ready?1:0);return true;
        case 0x7ff00018:boundary("PicoStill.cancel");event(7,0,0,0);return true;
        case 0x400f45b4:boundary("EventQueue.put");long type=word(reg(11)+4);
            event(9,type,type==0?word(reg(11)+8):type==2?word(reg(11)+16):0,type==0?word(reg(11)+12):0);return true;
        case 0x400f4900:boundary("RuntimeCommandQueue.requeue");event(11,word(reg(11)+4),word(reg(11)+0x18),word(reg(11)+0x1c));return true;
        case 0x400d7df8:boundary("ProgramRepository.find_fixture");event(12,reg(11),0,0);put(10,reg(11)==0xffffffffL?0:PROGRAM);return true;
        case 0x400dc2f8:boundary("instruction_validated_MSP_sendByte");event(10,reg(11),reg(12),1);return true;
        case 0x400dc0a4:boundary("instruction_validated_MSP_send");event(10,reg(11),0,0);return true;
        case 0x400dc984:boundary("instruction_validated_MSP_moveValve");event(10,0x14+reg(11),0,0);
            emu.writeMemory(toAddr(CORE+0x2ec),new byte[]{1});return true;
        case 0x4000c44c:boundary("ROM_memset");byte[] fill=new byte[(int)reg(12)];Arrays.fill(fill,(byte)reg(11));emu.writeMemory(toAddr(reg(10)),fill);return true;
        case 0x4000c2c8:boundary("ROM_memcpy");emu.writeMemory(toAddr(reg(10)),emu.readMemory(toAddr(reg(11)),(int)reg(12)));return true;
        case 0x400014c0:boundary("ROM_strlen");put(10,text(reg(10)).getBytes(StandardCharsets.UTF_8).length);return true;
        case 0x400db5c4:
            boundary("CString_vsnprintf_known_Step_percent_d");
            if(!text(reg(12)).equals("Step %d")||reg(15)!=8)throw new IllegalStateException("Unreviewed variadic format");
            String rendered="Step "+(int)word(reg(14)+reg(15));
            long destination=reg(10);int cap=(int)reg(11);text(destination,rendered.substring(0,Math.min(rendered.length(),cap-1)));
            put(10,rendered.length());return true;
        default:return false;
        }
    }
    private long[] execute(long entry,long... args) throws Exception {
        if(!ALLOWED.contains(entry))throw new IllegalStateException(String.format("Unreviewed function %08x",entry));
        bodies.add(entry);Instruction prologue=currentProgram.getListing().getInstructionAt(toAddr(entry));
        if(prologue==null||!prologue.getMnemonicString().equalsIgnoreCase("entry"))throw new IllegalStateException("No original ENTRY");
        Scalar frame=prologue.getScalar(1);if(frame==null)throw new IllegalStateException("No frame size");
        long[] caller=new long[16];for(int i=0;i<16;i++){caller[i]=reg(i);put(i,0);}put(1,caller[1]-frame.getUnsignedValue());
        for(int i=0;i<args.length;i++)put(i+2,args[i]);
        emu.getEmulator().setExecuteAddress(entry+prologue.getLength());
        while(true){
            if(--budget<0)throw new IllegalStateException("Original instruction budget exceeded");
            if((budget&255)==0)monitor.checkCancelled();Address pc=emu.getExecutionAddress();
            Instruction instruction=currentProgram.getListing().getInstructionAt(pc);
            if(instruction==null)throw new IllegalStateException("No original instruction at "+pc);
            String m=instruction.getMnemonicString().toLowerCase(Locale.ROOT);
            if(m.startsWith("retw")){long[] result=new long[6];for(int i=0;i<6;i++)result[i]=reg(i+2);for(int i=0;i<16;i++)put(i,caller[i]);return result;}
            if(m.startsWith("loop"))throw new IllegalStateException("Unreviewed hardware LOOP at "+pc);
            long next=pc.getOffset()+instruction.getLength();
            if(m.equals("memw")){
                if(entry!=0x4010b200L)throw new IllegalStateException("Unreviewed memory barrier at "+pc);
                // Memory operations are already sequential in this single-thread
                // function fixture. No interrupt/concurrent-memory behavior is claimed.
                boundary("MEMW_sequential_fixture_memory_order");emu.getEmulator().setExecuteAddress(next);continue;
            }
            if(m.equals("call8")||m.equals("callx8")){
                long target;
                if(m.equals("callx8")){Register r=instruction.getRegister(0);target=emu.readRegister(r).longValue()&0xffffffffL;}
                else{Address[] flows=instruction.getFlows();if(flows.length!=1)throw new IllegalStateException("Ambiguous call");target=flows[0].getOffset();}
                if(!sdk(target)){long[] outgoing=new long[6];for(int i=0;i<6;i++)outgoing[i]=reg(10+i);long[] returned=execute(target,outgoing);for(int i=0;i<6;i++)put(10+i,returned[i]);}
                emu.getEmulator().setExecuteAddress(next);continue;
            }
            if(!emu.step(monitor))throw new IllegalStateException("Original instruction failed at "+pc+": "+emu.getLastError());
        }
    }
    private Map<String,Object> one(JsonObject input,JsonArray strings) throws Exception {
        setup(input,strings);JsonArray a=input.getAsJsonArray("args");long[] args=new long[4];for(int i=0;i<a.size();i++)args[i]=a.get(i).getAsLong();
        int action=input.get("action").getAsInt(),assertion=0;long[] value={0,0};
        word(INSTR,args[0]);word(INSTR+4,args[1]);
        try{switch(action){
            case 0:execute(0x4010a48cL,OBJECT,INSTR);break;
            case 1:value[0]=execute(0x400e6bf8L,OBJECT,STATE+0x7c,args[0],clock)[0];break;
            case 2:value[0]=execute(0x400d12b0L,INSTR)[0];break;
            case 3:execute(0x400e6240L,OBJECT,clock);break;
            case 4:execute(0x400e62d8L,OBJECT,clock,args[0]);break;
            case 5:execute(0x400f46e8L,OBJECT,args[0]);break;
            case 6:execute(0x400e64d0L,OBJECT,args[0]);break;
            case 7:execute(0x400e6364L,OBJECT,clock,args[0],args[1],args[2],args[3]);break;
            case 8:value[0]=execute(0x400e660cL,OBJECT,clock,args[0])[0];break;
            case 9:execute(0x400e6124L,OBJECT,0,0,0x40490000L);break;
            case 10:execute(0x400e03a8L,OBJECT,args[0]);break;
            case 11:execute(0x400e0374L,OBJECT+0x114,args[0],args[1]);break;
            case 12:long[] pop=execute(0x400e06fcL,OBJECT+0x114);value[0]=pop[0];value[1]=pop[1];break;
            case 13:value[0]=execute(0x401bf40cL,args[0],args[1])[0];break;
            case 14:word(INSTR+0x80,args[2]);word(INSTR+0x84,args[3]);word(INSTR+0x88,args[0]);
                long[] quantity=execute(0x400dee30L,INSTR+0x80,args[1]);value[0]=quantity[0];value[1]=quantity[1];break;
            case 15:execute(0x400d742cL,OBJECT+0x93c);break;
            case 16:value[0]=execute(0x400d20acL,OBJECT,args[0])[0];break;
            case 17:value[0]=execute(0x400e05c8L,OBJECT,args[0])[0];break;
            case 18:execute(0x400ec5fcL,OBJECT);break;
            case 19:execute(0x4010b200L,OBJECT,clock);break;
            case 20:
                word(PROGRAM+8,args[0]==0?NAMES+128:args[0]==1?NAMES+96:0);
                if(args[1]==1)word(PROGRAM+16,0);if(args[1]==2)word(PROGRAM+12,0);
                word(PROGRAM+20,args[2]==0?OBJECT+0x118:0);
                word(PROGRAM+24,args[3]==0?input.getAsJsonArray("stack").size():args[3]);
                long diagnostic=execute(0x400e01d8L,PROGRAM)[0];
                if(diagnostic!=0){String message=text(diagnostic);value[0]=List.of("Program has no name","Program has no instruction",
                    "Program has too many instructions","Program has inconsistent string table","Program has invalid instruction",
                    "Program has invalid string reference").indexOf(message)+1;if(value[0]==0)throw new IllegalStateException("Unknown program diagnostic: "+message);}
                break;
            default:throw new IllegalArgumentException("Unknown action");
        }}catch(OriginalAssertion failure){assertion=failure.line;}
        Map<String,Object> words=new LinkedHashMap<>(),doubles=new LinkedHashMap<>();
        for(JsonElement e:wordFields){JsonObject f=e.getAsJsonObject();if(!f.get("output").getAsBoolean())continue;
            long v=f.get("width").getAsInt()==1?emu.readMemoryByte(toAddr(address(f)))&255L:word(address(f));
            if(f.get("region").getAsString().equals("map"))v=v==0x3f42556fL?1:0;words.put(f.get("name").getAsString(),v);}
        for(JsonElement e:doubleFields){JsonObject f=e.getAsJsonObject();if(f.get("output").getAsBoolean())doubles.put(f.get("name").getAsString(),real(address(f)));}
        List<Object> stack=new ArrayList<>();long count=word(OBJECT+0x918);if(count>256)throw new IllegalStateException("Corrupt stack count");
        for(int i=0;i<count;i++)stack.add(List.of(word(OBJECT+0x118+8*i),word(OBJECT+0x11c+8*i)));
        return row("assertion",assertion,"value",value,"words",words,"doubles",doubles,"stack",stack,
            "names",List.of(text(word(STATE+0x14)),text(word(STATE+0x6c)),text(STATE+0x18)),"events",new ArrayList<>(events));
    }
    @Override public void run() throws Exception {
        String[] args=getScriptArgs();if(args.length!=3)throw new IllegalArgumentException("Expected input specification, verified image layout, output JSON");
        JsonObject spec=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonObject();
        JsonObject layout=JsonParser.parseString(Files.readString(Path.of(args[1]))).getAsJsonObject();
        byte[] raw=Files.readAllBytes(Path.of(layout.get("input_path").getAsString()));
        String sha=hex(MessageDigest.getInstance("SHA-256").digest(raw));
        if(!sha.equals("d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27"))throw new IllegalStateException("Wrong original image");
        for(JsonElement e:layout.getAsJsonArray("segments")){JsonObject s=e.getAsJsonObject();int size=s.get("size").getAsInt();if(size==0)continue;
            byte[] bytes=new byte[size];currentProgram.getMemory().getBytes(toAddr(s.get("address").getAsLong()),bytes);
            int start=s.get("offset").getAsInt();if(!Arrays.equals(bytes,Arrays.copyOfRange(raw,start,start+size)))throw new IllegalStateException("Project differs from original bytes");}
        wordFields=spec.getAsJsonArray("words");doubleFields=spec.getAsJsonArray("doubles");emu=new EmulatorHelper(currentProgram);
        try{List<Object> output=new ArrayList<>();int index=0;
            for(JsonElement input:spec.getAsJsonArray("cases")){try{output.add(one(input.getAsJsonObject(),spec.getAsJsonArray("strings")));}
                catch(Exception failure){throw new IllegalStateException("Case "+index+" "+input,failure);}index++;if(index%250==0)println("Original runtime cases: "+index);}
            List<Object> functions=new ArrayList<>();for(long entry:bodies){Function f=getFunctionAt(toAddr(entry));if(f==null)throw new IllegalStateException("Missing function extent");
                List<Object> spans=new ArrayList<>();var ranges=f.getBody().getAddressRanges();while(ranges.hasNext()){var r=ranges.next();byte[] b=new byte[(int)r.getLength()];
                    currentProgram.getMemory().getBytes(r.getMinAddress(),b);spans.add(row("address",r.getMinAddress().getOffset(),"size",b.length,"sha256",hex(MessageDigest.getInstance("SHA-256").digest(b))));}
                functions.add(row("entry",entry,"ranges",spans));}
            Map<String,Object> result=row("schema",1,"input_sha256",sha,"cases",output,"original_functions",functions,"boundary_calls",boundaries,
                "scope","Original Xtensa instructions; reviewed ENTRY/CALL8/RETW register-window boundaries; MEMW uses the fixture's sequential memory order; pinned SDK scalar math, clocks, diagnostic/string library services, independently validated MSP operations and explicit heater/PicoStill/event callbacks",
                "hardware_tested",false,"full_cpu_emulation",false);
            Files.writeString(Path.of(args[2]),new GsonBuilder().setPrettyPrinting().create().toJson(result)+"\n");println("Wrote "+index+" original runtime cases");
        }finally{emu.dispose();}
    }
}
