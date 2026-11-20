// Original Core setup call order; separately validated ADC/heater boundaries.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.emulator.EmulatorHelper;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Instruction;

public class EmulateCoreStartup extends GhidraScript {
    private static final long CORE=0x3ff40000L, EEPROM=0x3ff72000L, INTERFACE=0x3ff70000L, VTABLE=0x3ff71000L;
    private static final long[] COMPONENTS={0x400f6ae8L,0x400f7e98L,0x400f6e78L,0x40112f88L,0x401007dcL,
        0x400fbc1cL,0x400f7028L,0x400d0364L,0x400de078L,0x400de10cL,0x400fc0a4L,0x400ff63cL,
        0x400fb8a4L,0x40112378L,0x4010c0a0L,0x401122a0L,0x400fae40L,0x400fb46cL,0x4010dd9cL,
        0x400f71b0L,0x4011038cL,0x400efc24L};
    private final Map<String,Long> boundaries=new TreeMap<>();
    private List<List<Long>> events;
    private long boiler;
    private long word(EmulatorHelper e,int r){return e.readRegister("a"+r).longValue()&0xffffffffL;}
    private void put(EmulatorHelper e,long address,long value){byte[] out=new byte[4];for(int i=0;i<4;i++)out[i]=(byte)(value>>>(i*8));e.writeMemory(toAddr(address),out);}
    private void event(long... values){List<Long> record=new ArrayList<>();for(long value:values)record.add(value);while(record.size()<5)record.add(0L);events.add(record);}
    private void hook(EmulatorHelper e,long target)throws Exception{
        boundaries.merge(String.format("0x%08x",target),1L,Long::sum);
        for(int i=0;i<COMPONENTS.length;i++)if(target==COMPONENTS[i]){event(0,i);return;}
        switch((int)target){
            case 0x4012a798:case 0x400d0b50:case 0x400d5a44:case 0x400d5a68:return;
            case 0x40080904:event(1,word(e,10),word(e,11));return;
            case 0x400d5b24:event(2,33,word(e,11));return;
            case 0x400de248:event(3,boiler);e.writeRegister("a10",EEPROM);return;
            case 0x400fad30:
                if(word(e,11)!=boiler)throw new IllegalStateException("Unexpected ADC boiler argument");
                event(4,0x34);event(5,0x80);event(6,10);event(4,0x34);event(5,0x82);event(5,7);event(7,1);return;
            case 0x400fc3e4:event(1,12,2);event(2,12,0);event(1,19,2);event(2,19,0);return;
            case 0x400de27c:case 0x400d02cc:return;
            case 0x400922bc:
                if(word(e,11)!=1000||word(e,12)!=1||word(e,13)!=1||word(e,14)!=0x40080570L)throw new IllegalStateException("Unexpected software timer arguments");
                e.writeRegister("a10",0x8002L);return;
            case 0x40090a4c:e.writeRegister("a10",1234L);return;
            case 0x400922e0:
                if(word(e,10)!=0x8002L||word(e,11)!=1||word(e,12)!=1234||word(e,13)!=0||word(e,14)!=10)throw new IllegalStateException("Unexpected software timer start");
                event(8,1000,1,0,10);e.writeRegister("a10",1L);return;
            case 0x40129640:event(9,word(e,10),word(e,11),word(e,12));e.writeRegister("a10",0x8000+word(e,10));return;
            case 0x401294dc:
                long callback=word(e,11)==0x400805dcL?1:word(e,11)==0x40080540L?2:-1;
                if(callback<0)throw new IllegalStateException("Unknown timer callback");
                event(10,word(e,10),callback,word(e,12));return;
            case 0x40129450:event(11,word(e,10),word(e,12),word(e,13),word(e,14));return;
            case 0x400d2160:event(12,0x8000,word(e,11));event(12,0x8001,word(e,11));return;
            case 0x400db014:throw new IllegalStateException("Original startup assertion fired");
            default:throw new IllegalStateException(String.format("Unmodeled startup boundary %08x",target));
        }
    }
    private void execute(EmulatorHelper e)throws Exception{
        for(int steps=0;steps<1000;steps++){
            monitor.checkCancelled();Address pc=e.getExecutionAddress();Instruction instruction=currentProgram.getListing().getInstructionAt(pc);
            if(instruction==null)throw new IllegalStateException("Missing original instruction "+pc);
            String op=instruction.getMnemonicString().toLowerCase(Locale.ROOT);
            if(op.startsWith("retw"))return;
            if(op.startsWith("loop"))throw new IllegalStateException("Unexpected hardware loop");
            if(op.equals("call8")||op.equals("callx8")){
                long destination;
                if(op.equals("callx8")){Register r=instruction.getRegister(0);destination=e.readRegister(r).longValue()&0xffffffffL;}
                else{Address[] flows=instruction.getFlows();if(flows.length!=1)throw new IllegalStateException("Ambiguous call");destination=flows[0].getOffset();}
                hook(e,destination);e.getEmulator().setExecuteAddress(pc.getOffset()+instruction.getLength());
            }else if(op.startsWith("call"))throw new IllegalStateException("Unexpected call form");
            else if(!e.step(monitor))throw new IllegalStateException("Execution failed at "+pc+": "+e.getLastError());
        }
        throw new IllegalStateException("Instruction budget exceeded");
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=1)throw new IllegalArgumentException("Expected output JSON");
        String sha="d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27";
        if(!sha.equals(currentProgram.getExecutableSHA256()))throw new IllegalStateException("Original program identity mismatch");
        List<Map<String,Object>> cases=new ArrayList<>();EmulatorHelper e=new EmulatorHelper(currentProgram);
        try{for(boiler=0;boiler<2;boiler++){
            for(int i=0;i<16;i++)e.writeRegister("a"+i,0L);
            e.writeRegister("SAR",0L);e.writeRegister("PS",0L);e.writeMemory(toAddr(CORE),new byte[0x18000]);
            e.writeMemory(toAddr(0x3ff70000L),new byte[0x3000]);e.writeMemory(toAddr(0x3ff90000L),new byte[0x4000]);
            put(e,EEPROM+0x74,boiler);put(e,CORE+0x5d58,INTERFACE);put(e,INTERFACE,VTABLE);put(e,VTABLE+12,0x400fc0a4L);
            put(e,CORE+0x4948,CORE+0x9d8);put(e,CORE+0x4950,CORE+0xc58);
            e.writeRegister("a1",0x3ff92000L);e.writeRegister("a2",CORE);e.getEmulator().setExecuteAddress(0x4011307bL);events=new ArrayList<>();
            execute(e);Map<String,Object> item=new LinkedHashMap<>();item.put("boiler",boiler);item.put("events",events);cases.add(item);
        }}finally{e.dispose();}
        Map<String,Object> result=new LinkedHashMap<>();result.put("schema_version",1);result.put("input_sha256",sha);result.put("function","0x40113078");
        result.put("scope","Original Core setup instruction body and call ordering; component and SDK boundaries modeled; independently checked heater/ADC effects expanded into their recorded pin/bus events; software timer create/start normalized to one port event");
        result.put("boundary_calls",boundaries);result.put("cases",cases);result.put("full_cpu_or_hardware_emulation",false);result.put("hardware_tested",false);
        Files.writeString(Path.of(args[0]),new GsonBuilder().setPrettyPrinting().create().toJson(result)+"\n");println("Recorded both original boiler startup traces");
    }
}
