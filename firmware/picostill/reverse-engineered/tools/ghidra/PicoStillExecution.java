// Shared validation helper. Executes original bodies while explicitly adapting
// Xtensa windowed calls; it is not a full CPU, exception, or RTOS emulator.
import java.util.*;
import ghidra.app.emulator.EmulatorHelper;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Program;
import ghidra.util.task.TaskMonitor;

public final class PicoStillExecution implements AutoCloseable {
    public interface Boundary { void call(PicoStillExecution execution,long target)throws Exception; }
    public static final String IMAGE_SHA="99ac1244a17b72dab51af1f61ed461cee5e85e050c898fc84518fcaeedadc7fc";
    public final EmulatorHelper emulator;
    public final Map<String,Long> functions=new TreeMap<>(), boundaries=new TreeMap<>();
    private final Program program;
    private final TaskMonitor monitor;
    private final Boundary boundary;
    private int depth;
    public int instructionLimit=20000;
    public PicoStillExecution(Program program,TaskMonitor monitor,Boundary boundary){
        if(!IMAGE_SHA.equals(program.getExecutableSHA256()))throw new IllegalArgumentException("Original PicoStill 0.0.35 image mismatch");
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
        emulator.writeRegister("LCOUNT",0L);emulator.writeRegister("LBEG",0L);emulator.writeRegister("LEND",0L);
        emulator.writeRegister("SAR",0L);emulator.writeRegister("PS",0L);register(1,0x3ff92000L);
        clear(0x3ff90000L,0x4000);emulator.getEmulator().setExecuteAddress(0x400d001cL);depth=0;
    }
    public void countBoundary(String name){boundaries.merge(name,1L,Long::sum);}
    public boolean arithmetic(long target){
        switch((int)target){
            case 0x40002590:countBoundary("ROM_adddf3");number(10,number(10)+number(12));return true;
            case 0x400026e4:countBoundary("ROM_subdf3");number(10,number(10)-number(12));return true;
            case 0x4006358c:countBoundary("ROM_muldf3");number(10,number(10)*number(12));return true;
            case 0x40002954:countBoundary("ROM_divdf3");number(10,number(10)/number(12));return true;
            case 0x4000c944:countBoundary("ROM_floatsidf");number(10,(int)register(10));return true;
            case 0x4000c938:countBoundary("ROM_floatunsidf");number(10,register(10));return true;
            case 0x4000234c:countBoundary("ROM_divsf3");register(10,Float.floatToRawIntBits(Float.intBitsToFloat((int)register(10))/Float.intBitsToFloat((int)register(11))));return true;
            case 0x40002c34:countBoundary("ROM_extendsfdf2");number(10,Float.intBitsToFloat((int)register(10)));return true;
            case 0x40002b90:countBoundary("ROM_truncdfsf2");register(10,Float.floatToRawIntBits((float)number(10)));return true;
            case 0x400eb678:countBoundary("libm_log");number(10,Math.log(number(10)));return true;
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
        long loopBegin=-1,loopEnd=-1,loopRemaining=0;
        for(int step=0;step<instructionLimit;step++){
            monitor.checkCancelled();Address pc=emulator.getExecutionAddress();
            if(pc.getOffset()==loopEnd){
                if(loopRemaining!=0){loopRemaining--;countBoundary("adapted_loop_backedge");emulator.getEmulator().setExecuteAddress(loopBegin);continue;}
                loopBegin=-1;loopEnd=-1;
            }
            Instruction instruction=program.getListing().getInstructionAt(pc);
            if(instruction==null)throw new IllegalStateException("Undecoded original instruction "+pc);
            String opcode=instruction.getMnemonicString().toLowerCase(Locale.ROOT);
            if(opcode.startsWith("loop")){
                if(!(opcode.equals("loop")||opcode.equals("loopnez")||opcode.equals("loopgtz")))throw new IllegalStateException("Unsupported loop "+opcode);
                Register r=instruction.getRegister(0);Address end=instruction.getAddress(1);
                if(r==null||end==null)throw new IllegalStateException("Missing hardware loop operand");
                long count=emulator.readRegister(r).longValue()&0xffffffffL;
                loopBegin=pc.getOffset()+instruction.getLength();loopEnd=end.getOffset();loopRemaining=(count-1)&0xffffffffL;
                // Adapt only the hardware loop control. Every body instruction
                // still executes. Keep Sleigh's context-derived loop epilogue
                // disabled; otherwise it can branch after each body instruction.
                emulator.writeRegister("LCOUNT",0L);emulator.writeRegister("LBEG",loopBegin);emulator.writeRegister("LEND",loopEnd);
                countBoundary("adapted_"+opcode);
                if((opcode.equals("loopnez")&&count==0)||(opcode.equals("loopgtz")&&(int)count<=0)){
                    long after=loopEnd;loopBegin=-1;loopEnd=-1;loopRemaining=0;emulator.getEmulator().setExecuteAddress(after);
                }else emulator.getEmulator().setExecuteAddress(loopBegin);
                continue;
            }
            if(opcode.startsWith("retw")){returns=new long[6];for(int i=0;i<6;i++)returns[i]=register(i+2);break;}
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
        }
        if(returns==null)throw new IllegalStateException("Instruction budget exceeded");
        for(int i=0;i<16;i++)register(i,saved[i]);for(int i=0;i<6;i++)register(i+10,returns[i]);
        emulator.getEmulator().setExecuteAddress(oldPC);depth--;
    }
    @Override public void close(){emulator.dispose();}
}
