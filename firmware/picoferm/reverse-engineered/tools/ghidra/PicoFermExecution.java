// Executes the original ESP8266 call0 ABI. SDK/ROM boundaries are explicit;
// unknown calls, instruction gaps and execution budgets fail the replay.
import java.util.*;
import ghidra.app.emulator.EmulatorHelper;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Program;
import ghidra.util.task.TaskMonitor;

public final class PicoFermExecution implements AutoCloseable {
    public interface Boundary { void call(PicoFermExecution execution,long target)throws Exception; }
    public static final String IMAGE_SHA="08c34af891036494ae81d6f29e7495a475f67be977aa4effd3adde6fee4e9532";
    public final EmulatorHelper emulator;
    public final Map<String,Long> functions=new TreeMap<>(),boundaries=new TreeMap<>();
    private final Program program;private final TaskMonitor monitor;private final Boundary boundary;private int depth;
    public PicoFermExecution(Program program,TaskMonitor monitor,Boundary boundary){
        if(!IMAGE_SHA.equals(program.getExecutableSHA256()))throw new IllegalArgumentException("Wrong original PicoFerm image");
        this.program=program;this.monitor=monitor;this.boundary=boundary;emulator=new EmulatorHelper(program);
    }
    public Address address(long value){return program.getAddressFactory().getDefaultAddressSpace().getAddress(value);}
    public long register(int index){return emulator.readRegister("a"+index).longValue()&0xffffffffL;}
    public void register(int index,long value){emulator.writeRegister("a"+index,value&0xffffffffL);}
    public void write(long pointer,long value,int size){byte[] b=new byte[size];for(int i=0;i<size;i++)b[i]=(byte)(value>>>(8*i));emulator.writeMemory(address(pointer),b);}
    public long read(long pointer,int size)throws Exception{byte[] b=emulator.readMemory(address(pointer),size);long n=0;for(int i=0;i<size;i++)n|=(b[i]&255L)<<(8*i);return n;}
    public void clear(long pointer,int size){emulator.writeMemory(address(pointer),new byte[size]);}
    public String string(long pointer)throws Exception{
        StringBuilder s=new StringBuilder();for(int i=0;i<4096;i++){long b=read(pointer+i,1);if(b==0)return s.toString();s.append((char)b);}throw new IllegalStateException("Unterminated original string");
    }
    public void resetRegisters(){
        for(int i=0;i<16;i++)register(i,0);emulator.writeRegister("SAR",0L);emulator.writeRegister("PS",0L);
        register(1,0x3ff92000L);clear(0x3ff90000L,0x4000);emulator.getEmulator().setExecuteAddress(0x40201010L);depth=0;
    }
    public void countBoundary(String name){boundaries.merge(name,1L,Long::sum);}
    public void invoke(long target)throws Exception{
        if(++depth>24)throw new IllegalStateException("Nested execution limit");functions.merge(String.format("0x%08x",target),1L,Long::sum);
        long oldPC=emulator.getExecutionAddress().getOffset();emulator.getEmulator().setExecuteAddress(target);boolean returned=false;
        for(int step=0;step<50000;step++){
            monitor.checkCancelled();Address pc=emulator.getExecutionAddress();Instruction instruction=program.getListing().getInstructionAt(pc);
            if(instruction==null)throw new IllegalStateException("Undecoded instruction "+pc);
            String opcode=instruction.getMnemonicString().toLowerCase(Locale.ROOT);
            if(opcode.equals("ret")||opcode.equals("ret.n")){returned=true;break;}
            if(opcode.startsWith("loop")||opcode.startsWith("retw")||opcode.equals("entry"))throw new IllegalStateException("Unexpected CPU/window operation "+opcode);
            if(opcode.equals("memw")){countBoundary("single_thread_memw");emulator.getEmulator().setExecuteAddress(pc.getOffset()+instruction.getLength());continue;}
            if(opcode.equals("call0")||opcode.equals("callx0")){
                long destination;
                if(opcode.equals("callx0")){Register r=instruction.getRegister(0);if(r==null)throw new IllegalStateException("Missing indirect register");destination=emulator.readRegister(r).longValue()&0xffffffffL;}
                else{Address[] flows=instruction.getFlows();if(flows.length!=1)throw new IllegalStateException("Ambiguous call");destination=flows[0].getOffset();}
                register(0,pc.getOffset()+instruction.getLength());boundary.call(this,destination);emulator.getEmulator().setExecuteAddress(pc.getOffset()+instruction.getLength());
            }else if(opcode.startsWith("call"))throw new IllegalStateException("Unexpected call ABI");
            else if(!emulator.step(monitor))throw new IllegalStateException("Execution failed at "+pc+": "+emulator.getLastError());
        }
        if(!returned)throw new IllegalStateException("Instruction budget exceeded at "+Long.toHexString(target));
        emulator.getEmulator().setExecuteAddress(oldPC);depth--;
    }
    @Override public void close(){emulator.dispose();}
}
