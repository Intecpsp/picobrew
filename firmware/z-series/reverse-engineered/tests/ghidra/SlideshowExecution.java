// Display-specific validation helper, derived from Z119Execution. Executes original bodies while adapting
// Xtensa windowed calls; it is not a full CPU, exception, or RTOS emulator.
import java.util.*;
import ghidra.app.emulator.EmulatorHelper;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Program;
import ghidra.util.task.TaskMonitor;

public final class SlideshowExecution implements AutoCloseable {
    public interface Boundary { void call(SlideshowExecution execution,long target)throws Exception; }
    public static final String IMAGE_SHA="d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27";
    public final EmulatorHelper emulator;
    public final Map<String,Long> functions=new TreeMap<>(), boundaries=new TreeMap<>();
    private final Program program;
    private final TaskMonitor monitor;
    private final Boundary boundary;
    private int depth;
    public SlideshowExecution(Program program,TaskMonitor monitor,Boundary boundary){
        if(!IMAGE_SHA.equals(program.getExecutableSHA256()))throw new IllegalArgumentException("Original Z119 image mismatch");
        this.program=program;this.monitor=monitor;this.boundary=boundary;emulator=new EmulatorHelper(program);
    }
    public Address address(long value){return program.getAddressFactory().getDefaultAddressSpace().getAddress(value);}
    public long register(int index){return emulator.readRegister("a"+index).longValue()&0xffffffffL;}
    public void register(int index,long value){emulator.writeRegister("a"+index,value&0xffffffffL);}
    public double number(int index){return Double.longBitsToDouble(register(index)|register(index+1)<<32);}
    public void number(int index,double value){long bits=Double.doubleToRawLongBits(value);register(index,bits);register(index+1,bits>>>32);}
    public void write(long pointer,long value,int size){byte[] bytes=new byte[size];for(int i=0;i<size;i++)bytes[i]=(byte)(value>>>(8*i));emulator.writeMemory(address(pointer),bytes);}
    public long read(long pointer,int size)throws Exception{byte[] bytes=emulator.readMemory(address(pointer),size);long value=0;for(int i=0;i<size;i++)value|=(bytes[i]&255L)<<(8*i);return value;}
    public void writeDouble(long pointer,double value){write(pointer,Double.doubleToRawLongBits(value),8);}
    public double readDouble(long pointer)throws Exception{return Double.longBitsToDouble(read(pointer,8));}
    public void clear(long pointer,int size){emulator.writeMemory(address(pointer),new byte[size]);}
    public void resetRegisters(){
        for(int i=0;i<16;i++){emulator.writeRegister("a"+i,0L);emulator.writeRegister("f"+i,0L);}
        emulator.writeRegister("SAR",0L);emulator.writeRegister("PS",0L);register(1,0x3ff92000L);
        clear(0x3ff90000L,0x4000);emulator.getEmulator().setExecuteAddress(0x400d001cL);depth=0;
    }
    public void countBoundary(String name){boundaries.merge(name,1L,Long::sum);}
    public boolean arithmetic(long target){
        double a,b;
        switch((int)target){
            case 0x4012cb84:countBoundary("__adddf3");number(10,number(10)+number(12));return true;
            case 0x4012ccd8:countBoundary("__subdf3");number(10,number(10)-number(12));return true;
            case 0x4012cf40:countBoundary("__muldf3");number(10,number(10)*number(12));return true;
            case 0x4012d144:countBoundary("__divdf3");number(10,number(10)/number(12));return true;
            case 0x401c0198:countBoundary("__floatunsidf");number(10,register(10));return true;
            case 0x401c01a4:countBoundary("__floatsidf");number(10,(int)register(10));return true;
            case 0x4012d3e0:
                countBoundary("__fixdfsi");a=number(10);
                if(!Double.isFinite(a)||a<=Integer.MIN_VALUE||a>=Integer.MAX_VALUE)throw new IllegalArgumentException("Outside finite conversion domain");
                register(10,(int)a);return true;
            case 0x4012d29c:case 0x4012d350:case 0x4012d2c4:case 0x4012d328:case 0x4012d268:
                countBoundary(String.format("ordered_comparison_%08x",target));a=number(10);b=number(12);
                if(!Double.isFinite(a)||!Double.isFinite(b))throw new IllegalArgumentException("Outside finite comparison domain");
                register(10,a<b?-1:a>b?1:0);return true;
            case 0x401c00b4:countBoundary("__divsf3");register(10,Float.floatToRawIntBits(Float.intBitsToFloat((int)register(10))/Float.intBitsToFloat((int)register(11))));return true;
            case 0x4012d530:countBoundary("__extendsfdf2");number(10,Float.intBitsToFloat((int)register(10)));return true;
            default:return false;
        }
    }
    public void invoke(long target)throws Exception{
        if(++depth>16)throw new IllegalStateException("Nested application execution limit");
        functions.merge(String.format("0x%08x",target),1L,Long::sum);
        Instruction entry=program.getListing().getInstructionAt(address(target));
        if(entry==null||!entry.getMnemonicString().equalsIgnoreCase("entry")||entry.getLength()!=3)throw new IllegalStateException("Unexpected original ENTRY "+Long.toHexString(target));
        byte[] raw=entry.getBytes();int frame=(((raw[0]&255)|((raw[1]&255)<<8)|((raw[2]&255)<<16))>>>12)<<3;
        long[] saved=new long[16];for(int i=0;i<16;i++)saved[i]=register(i);
        long oldPC=emulator.getExecutionAddress().getOffset();
        for(int i=0;i<16;i++)register(i,0);
        for(int i=0;i<6;i++)register(i+2,saved[i+10]);
        register(1,saved[1]-frame);emulator.getEmulator().setExecuteAddress(target+3);long[] returns=null;
        long loopBegin=0,loopEnd=0,iterations=0;
        for(int step=0;step<500000;step++){
            monitor.checkCancelled();Address pc=emulator.getExecutionAddress();Instruction instruction=program.getListing().getInstructionAt(pc);
            if(instruction==null)throw new IllegalStateException("Undecoded original instruction "+pc);
            String opcode=instruction.getMnemonicString().toLowerCase(Locale.ROOT);
            if(opcode.equals("loop")){
                boolean reversal=target==0x400d1b88L&&pc.getOffset()==0x400d1bd2L;
                boolean textlines=target==0x401bf978L&&pc.getOffset()==0x401bf98dL;
                if(!reversal&&!textlines)throw new IllegalStateException("Unreviewed display LOOP at "+pc);
                Register counter=instruction.getRegister(0);
                iterations=emulator.readRegister(counter).longValue()&0xffffffffL;
                if(iterations!=(reversal?4096:25))throw new IllegalStateException("Unexpected reviewed display LOOP count");
                loopBegin=pc.getOffset()+instruction.getLength();loopEnd=reversal?0x400d1be1L:0x401bf995L;
                countBoundary(reversal?"reviewed_4096_byte_reversal_LOOP":"reviewed_25_byte_TextLines_clear_LOOP");
                emulator.writeRegister("LCOUNT",0L);
                emulator.getEmulator().setExecuteAddress(loopBegin);continue;
            }
            if(opcode.startsWith("loop"))throw new IllegalStateException("Unreviewed Xtensa LOOP variant");
            if(opcode.startsWith("retw")){returns=new long[6];for(int i=0;i<6;i++)returns[i]=register(i+2);break;}
            // Windowed calls above are adapted explicitly. The matching
            // single-thread MOVSP effect is a stack-pointer move; processor
            // register-window spill/underflow exceptions are outside this
            // harness. Leaving Ghidra's synthetic WindowStart at zero would
            // otherwise turn dynamic stack allocation into a silent no-op.
            if(opcode.equals("movsp")){
                Register destination=instruction.getRegister(0), source=instruction.getRegister(1);
                if(destination==null||source==null||!destination.getName().equals("a1"))throw new IllegalStateException("Unreviewed MOVSP at "+pc);
                emulator.writeRegister(destination,emulator.readRegister(source));
                countBoundary("windowed_ABI_MOVSP_stack_move");
                emulator.getEmulator().setExecuteAddress(pc.getOffset()+instruction.getLength());continue;
            }
            // Ghidra 12.2's Xtensa TRUNC.S uses the integer scale bit pattern
            // as a float in its final truncation (fs f* scale), unlike its
            // preceding NaN check (fs f* int2float(scale)). Model the reviewed
            // finite, signed conversion explicitly; exception/saturation
            // behavior outside this domain is deliberately not substituted.
            if(opcode.equals("trunc.s")){
                Register destination=instruction.getRegister(0), source=instruction.getRegister(1);
                if(destination==null||source==null||instruction.getScalar(2)==null)throw new IllegalStateException("Unreviewed TRUNC.S at "+pc);
                long scale=instruction.getScalar(2).getUnsignedValue();
                if(scale>15)throw new IllegalStateException("Unreviewed TRUNC.S scale");
                float value=Float.intBitsToFloat(emulator.readRegister(source).intValue());
                double scaled=Math.scalb((double)value,(int)scale);
                if(!Double.isFinite(scaled)||scaled<Integer.MIN_VALUE||scaled>=2147483648.0)throw new IllegalStateException("TRUNC.S outside reviewed finite domain");
                emulator.writeRegister(destination,((long)(int)scaled)&0xffffffffL);
                countBoundary("reviewed_finite_TRUNC_S_power_of_two_scale");
                emulator.getEmulator().setExecuteAddress(pc.getOffset()+instruction.getLength());continue;
            }
            // The harness has one thread and deterministic memory. MEMW's
            // ordering effect has no competing access to synchronize here.
            // This explicit boundary does not validate concurrent hardware.
            if(opcode.equals("memw")){countBoundary("single_thread_memw");emulator.getEmulator().setExecuteAddress(pc.getOffset()+instruction.getLength());continue;}
            if(opcode.equals("call8")||opcode.equals("callx8")){
                long destination;
                if(opcode.equals("callx8")){Register r=instruction.getRegister(0);if(r==null)throw new IllegalStateException("Missing indirect register");destination=emulator.readRegister(r).longValue()&0xffffffffL;}
                else{Address[] flows=instruction.getFlows();if(flows.length!=1)throw new IllegalStateException("Ambiguous direct call");destination=flows[0].getOffset();}
                boundary.call(this,destination);emulator.getEmulator().setExecuteAddress(pc.getOffset()+instruction.getLength());
            }else if(opcode.startsWith("call"))throw new IllegalStateException("Unexpected call ABI");
            else if(!emulator.step(monitor))throw new IllegalStateException("Execution failed at "+pc+": "+emulator.getLastError());
            if(iterations>0 && emulator.getExecutionAddress().getOffset()==loopEnd){
                if(--iterations>0)emulator.getEmulator().setExecuteAddress(loopBegin);
            }
        }
        if(returns==null)throw new IllegalStateException("Instruction budget exceeded");
        for(int i=0;i<16;i++)register(i,saved[i]);for(int i=0;i<6;i++)register(i+10,returns[i]);
        emulator.getEmulator().setExecuteAddress(oldPC);depth--;
    }
    @Override public void close(){emulator.dispose();}
}
