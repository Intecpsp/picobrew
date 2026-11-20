// Original Z119 EEPROM instructions with the Arduino flash/cache boundary explicit.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.emulator.EmulatorHelper;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.scalar.Scalar;

public class EmulateEeprom extends GhidraScript {
    private static final long STACK=0x3ff80000L,OBJECT=0x3ff84000L,CACHE=0x3ff85000L,
        STRING=0x3ff86000L,CONTEXT=0x3ff87000L,ARGUMENT=CONTEXT+4;
    private static final Set<Long> ALLOWED=Set.of(0x400d1a2cL,0x400d6410L,0x401bf7f0L,
        0x400d0ed4L,0x400d0e60L,0x401bf254L,0x400d598cL,0x400d585cL,
        0x400fbc1cL,0x400edb34L,0x400edc18L,0x400de248L,
        0x400d59acL,0x400d59c0L,0x400d6760L,0x400d67a8L,
        0x401bdebcL,0x401bdee0L,0x401bdf04L);
    private final Map<String,Long> boundaries=new TreeMap<>();
    private final Set<Long> bodies=new TreeSet<>();
    private final List<Object> events=new ArrayList<>();
    private EmulatorHelper emu;
    private boolean beginResult,commitResult;
    private int budget;
    private static class OriginalAssertion extends Exception {
        final int line;
        OriginalAssertion(int value){super("Original assertion line "+value);line=value;}
    }
    private long reg(int n){return emu.readRegister("a"+n).longValue()&0xffffffffL;}
    private void put(int n,long v){emu.writeRegister("a"+n,v&0xffffffffL);}
    private void word(long a,long v)throws Exception{emu.writeMemory(toAddr(a),new byte[]{(byte)v,(byte)(v>>>8),(byte)(v>>>16),(byte)(v>>>24)});}
    private long word(long a)throws Exception{byte[] b=emu.readMemory(toAddr(a),4);return(b[0]&255L)|((b[1]&255L)<<8)|((b[2]&255L)<<16)|((b[3]&255L)<<24);}
    private int octet(long a)throws Exception{return emu.readMemoryByte(toAddr(a))&255;}
    private void octet(long a,int v)throws Exception{emu.writeMemory(toAddr(a),new byte[]{(byte)v});}
    private static String hex(byte[] b){return HexFormat.of().formatHex(b);}
    private String hex(long a,int n)throws Exception{return hex(emu.readMemory(toAddr(a),n));}
    private byte[] text(long a)throws Exception{
        var out=new java.io.ByteArrayOutputStream();for(int i=0;i<4096;i++){int b=octet(a+i);if(b==0)return out.toByteArray();out.write(b);}
        throw new IllegalStateException("Unterminated fixture string");
    }
    private void boundary(String n){boundaries.merge(n,1L,Long::sum);}
    private static Map<String,Object> row(Object... v){Map<String,Object> r=new LinkedHashMap<>();for(int i=0;i<v.length;i+=2)r.put((String)v[i],v[i+1]);return r;}
    private boolean sdk(long target)throws Exception{
        switch((int)target){
        case 0x40081388:boundary("millis_for_diagnostics");put(10,0);return true;
        case 0x4008c2b4:boundary("basename_for_diagnostics");return true;
        case 0x401c00b4:boundary("__divsf3_for_diagnostics");put(10,0);return true;
        case 0x4012d530:boundary("__extendsfdf2_for_diagnostics");put(10,0);put(11,0);return true;
        case 0x400d21cc:case 0x400d6204:boundary("diagnostic_output");return true;
        case 0x400db014:boundary("OEM_assertion");throw new OriginalAssertion((int)reg(11));
        case 0x40126534:
            boundary("Arduino_EEPROM_begin_fixture_cache");
            if(reg(10)!=0x3ffc32a0L)throw new IllegalStateException("Unexpected SDK EEPROM object");
            events.add(List.of("begin",reg(11)));put(10,beginResult?1:0);return true;
        case 0x4012659c:
            boundary("Arduino_EEPROM_commit_fixture_status");
            if(reg(10)!=0x3ffc32a0L)throw new IllegalStateException("Unexpected SDK EEPROM object");
            events.add(List.of("commit",octet(0x3ffc32acL),hex(CACHE,120)));
            if(commitResult)octet(0x3ffc32acL,0);put(10,commitResult?1:0);return true;
        case 0x400fba48:
            boundary("Eeprom_console_registration");if(reg(10)!=OBJECT)throw new IllegalStateException("Unexpected Eeprom registry owner");
            events.add(List.of("register"));return true;
        case 0x4000c44c:
            boundary("ROM_memset");byte[] fill=new byte[(int)reg(12)];Arrays.fill(fill,(byte)reg(11));emu.writeMemory(toAddr(reg(10)),fill);return true;
        case 0x4000c2c8:
            boundary("ROM_memcpy");emu.writeMemory(toAddr(reg(10)),emu.readMemory(toAddr(reg(11)),(int)reg(12)));return true;
        case 0x400014c0:boundary("ROM_strlen");put(10,text(reg(10)).length);return true;
        case 0x400015d4:
            boundary("ROM_strncpy_nonoverlapping_fixture");byte[] source=text(reg(11)),copy=new byte[(int)reg(12)];
            System.arraycopy(source,0,copy,0,Math.min(source.length,copy.length));emu.writeMemory(toAddr(reg(10)),copy);return true;
        default:return false;
        }
    }
    private long[] execute(long entry,long... args)throws Exception{
        if(!ALLOWED.contains(entry))throw new IllegalStateException(String.format("Unreviewed function %08x",entry));
        bodies.add(entry);Instruction prologue=currentProgram.getListing().getInstructionAt(toAddr(entry));
        if(prologue==null||!prologue.getMnemonicString().equalsIgnoreCase("entry"))throw new IllegalStateException("No original ENTRY");
        Scalar frame=prologue.getScalar(1);if(frame==null)throw new IllegalStateException("No frame size");
        long[] caller=new long[16];for(int i=0;i<16;i++){caller[i]=reg(i);put(i,0);}put(1,caller[1]-frame.getUnsignedValue());
        for(int i=0;i<args.length;i++)put(i+2,args[i]);emu.getEmulator().setExecuteAddress(entry+prologue.getLength());
        while(true){
            if(--budget<0)throw new IllegalStateException("Original instruction budget exceeded");if((budget&255)==0)monitor.checkCancelled();
            Address pc=emu.getExecutionAddress();Instruction instruction=currentProgram.getListing().getInstructionAt(pc);
            if(instruction==null)throw new IllegalStateException("No original instruction at "+pc);
            String m=instruction.getMnemonicString().toLowerCase(Locale.ROOT);
            if(m.startsWith("retw")){long[] result=new long[6];for(int i=0;i<6;i++)result[i]=reg(i+2);for(int i=0;i<16;i++)put(i,caller[i]);return result;}
            if(m.startsWith("loop"))throw new IllegalStateException("Unreviewed hardware LOOP at "+pc);
            long next=pc.getOffset()+instruction.getLength();
            if(m.equals("call8")||m.equals("callx8")){
                long target;if(m.equals("callx8"))target=emu.readRegister(instruction.getRegister(0)).longValue()&0xffffffffL;
                else{Address[] flows=instruction.getFlows();if(flows.length!=1)throw new IllegalStateException("Ambiguous call");target=flows[0].getOffset();}
                if(!sdk(target)){long[] outgoing=new long[6];for(int i=0;i<6;i++)outgoing[i]=reg(10+i);long[] returned=execute(target,outgoing);for(int i=0;i<6;i++)put(10+i,returned[i]);}
                emu.getEmulator().setExecuteAddress(next);continue;
            }
            if(!emu.step(monitor))throw new IllegalStateException("Original instruction failed at "+pc+": "+emu.getLastError());
        }
    }
    private Map<String,Object> one(JsonObject input)throws Exception{
        for(int i=0;i<16;i++){put(i,0);emu.writeRegister("f"+i,0L);}
        for(String n:List.of("SAR","PS","LCOUNT","LBEG","LEND"))emu.writeRegister(n,0L);
        emu.writeMemory(toAddr(STACK-0x4000),new byte[0xc000]);put(1,STACK);budget=30000;events.clear();
        emu.writeMemory(toAddr(OBJECT+8),HexFormat.of().parseHex(input.get("data").getAsString()));
        emu.writeMemory(toAddr(CACHE),HexFormat.of().parseHex(input.get("cache").getAsString()));
        word(OBJECT+0x84,input.get("requested").getAsLong());octet(OBJECT+0x80,input.get("initialized").getAsInt());
        word(0x3ffc32a4L,CACHE);word(0x3ffc32a8L,input.get("actual").getAsLong());octet(0x3ffc32acL,input.get("dirty").getAsInt());
        beginResult=input.get("begin").getAsInt()!=0;commitResult=input.get("commit").getAsInt()!=0;
        long string=0;if(!input.get("text").isJsonNull()){string=STRING;emu.writeMemory(toAddr(STRING),(input.get("text").getAsString()+"\0").getBytes(StandardCharsets.UTF_8));}
        word(CONTEXT,OBJECT);word(ARGUMENT,string);int assertion=0;long value=0;
        try{switch(input.get("action").getAsInt()){
        case 0:execute(0x400d1a2cL,OBJECT+8);break;
        case 1:value=execute(0x400d6410L,OBJECT+8)[0];break;
        case 2:value=execute(0x401bf7f0L,OBJECT+8)[0];break;
        case 3:value=execute(0x400d0ed4L,string)[0];break;
        case 4:value=execute(0x400d598cL,OBJECT+0x14,32,string)[0];break;
        case 5:value=execute(0x400d598cL,OBJECT+0x34,64,string)[0];break;
        case 6:execute(0x400fbc1cL,OBJECT);break;
        case 7:execute(0x400edb34L,OBJECT);break;
        case 8:execute(0x400edc18L,OBJECT);break;
        case 9:value=execute(0x400de248L,OBJECT)[0]==OBJECT+8?1:0;break;
        case 10:execute(0x400d59acL,CONTEXT,ARGUMENT);break;
        case 11:execute(0x400d59c0L,CONTEXT,ARGUMENT);break;
        case 12:execute(0x400d6760L,CONTEXT,ARGUMENT);break;
        case 13:execute(0x400d67a8L,CONTEXT,ARGUMENT);break;
        case 14:execute(0x401bdebcL,CONTEXT);break;
        case 15:execute(0x401bdee0L,CONTEXT);break;
        case 16:execute(0x401bdf04L,CONTEXT);break;
        default:throw new IllegalArgumentException("Unknown action");
        }}catch(OriginalAssertion failure){assertion=failure.line;}
        return row("assertion",assertion,"value",value,"data",hex(OBJECT+8,120),"cache",hex(CACHE,120),
            "initialized",octet(OBJECT+0x80),"dirty",octet(0x3ffc32acL),"events",new ArrayList<>(events));
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=3)throw new IllegalArgumentException("Expected input cases, verified layout, output JSON");
        JsonObject spec=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonObject();
        JsonObject layout=JsonParser.parseString(Files.readString(Path.of(args[1]))).getAsJsonObject();
        byte[] raw=Files.readAllBytes(Path.of(layout.get("input_path").getAsString()));String sha=hex(MessageDigest.getInstance("SHA-256").digest(raw));
        if(!sha.equals("d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27"))throw new IllegalStateException("Wrong original image");
        for(JsonElement e:layout.getAsJsonArray("segments")){JsonObject s=e.getAsJsonObject();int size=s.get("size").getAsInt();if(size==0)continue;
            byte[] bytes=new byte[size];currentProgram.getMemory().getBytes(toAddr(s.get("address").getAsLong()),bytes);int start=s.get("offset").getAsInt();
            if(!Arrays.equals(bytes,Arrays.copyOfRange(raw,start,start+size)))throw new IllegalStateException("Project differs from original bytes");}
        // These address-taken entries were missed by the original call-graph import.
        // Defining their verified ENTRY boundaries changes only this read-only view.
        // The exact 30-byte getter at 400de248..400de265 was marked as data by
        // direct-call analysis. Its ENTRY, branch, assertion call and RETW are
        // independently checked with the matching Xtensa objdump.
        clearListing(toAddr(0x400de248L),toAddr(0x400de265L));
        disassemble(toAddr(0x400de248L));
        for(long entry:ALLOWED)if(getFunctionAt(toAddr(entry))==null){disassemble(toAddr(entry));createFunction(toAddr(entry),"eeprom_validation_"+Long.toHexString(entry));}
        emu=new EmulatorHelper(currentProgram);
        try{List<Object> output=new ArrayList<>();int index=0;
            for(JsonElement input:spec.getAsJsonArray("cases")){try{output.add(one(input.getAsJsonObject()));}catch(Exception error){throw new IllegalStateException("Case "+index+" "+input,error);}
                index++;if(index%250==0)println("Original EEPROM cases: "+index);}
            List<Object> functions=new ArrayList<>();for(long entry:bodies){Function function=getFunctionAt(toAddr(entry));if(function==null)throw new IllegalStateException("Missing function extent");
                List<Object> spans=new ArrayList<>();var ranges=function.getBody().getAddressRanges();while(ranges.hasNext()){var range=ranges.next();byte[] bytes=new byte[(int)range.getLength()];
                    currentProgram.getMemory().getBytes(range.getMinAddress(),bytes);spans.add(row("address",range.getMinAddress().getOffset(),"size",bytes.length,"sha256",hex(MessageDigest.getInstance("SHA-256").digest(bytes))));}
                functions.add(row("entry",entry,"ranges",spans));}
            Map<String,Object> result=row("schema",1,"input_sha256",sha,"cases",output,"original_functions",functions,"boundary_calls",boundaries,
                "scope","Original Xtensa EEPROM data/lifecycle/setter instructions; reviewed ENTRY/CALL8/RETW register windows; explicit Arduino EEPROM begin/cache/commit outcomes, console registration, ROM string/memory and diagnostic services; no physical flash I/O",
                "hardware_tested",false,"full_cpu_emulation",false);
            Files.writeString(Path.of(args[2]),new GsonBuilder().setPrettyPrinting().create().toJson(result)+"\n");println("Wrote "+index+" original EEPROM cases");
        }finally{emu.dispose();}
    }
}
