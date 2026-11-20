// Original Z119 Program/ProgramBuffer/ProgramWriter/ProgramResolver execution.
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

public class EmulateProgramRepository extends GhidraScript {
    private static final long STACK=0x3ff80000L,BUFFER=0x3ff84000L,WRITER=0x3ff87000L,
        PROGRAM=0x3ff88000L,STATS=0x3ff88100L,CODE=0x3ff89000L,STRING=0x3ff8a000L,
        PARAMS=0x3ff8b000L,SENSOR=0x3ff8c000L;
    private static final Set<Long> ALLOWED=Set.of(0x400d7df8L,0x401bf2ccL,0x400dfea0L,
        0x400dfe58L,0x400dfd9cL,0x401bf62cL,0x400e0e84L,0x400e0f18L,0x400e3e24L,
        0x400d752cL,0x400e423cL,0x400d7568L,0x400e4380L,0x400e42d0L,
        0x400ec670L,0x400ec740L,0x400ec7b4L,0x400ec52cL,0x400ec360L,0x400ed15cL,
        0x400d0f80L,0x400d0fb8L,0x400e0f5cL,0x400dee30L,0x400def90L,0x400defa4L,
        0x400d0e60L,0x401bf254L,0x400d598cL,0x400d585cL,0x401bf3d4L,
        0x401bf40cL,0x400d12b0L,0x400dff0cL);
    private final Set<Long> bodies=new TreeSet<>();
    private final Map<String,Long> boundaries=new TreeMap<>();
    private EmulatorHelper emu;
    private int budget;
    private static class Assertion extends Exception {
        final long line; Assertion(long value){super("Original assertion line "+value);line=value;}
    }
    private long reg(int n){return emu.readRegister("a"+n).longValue()&0xffffffffL;}
    private void put(int n,long value){emu.writeRegister("a"+n,value&0xffffffffL);}
    private void word(long address,long value)throws Exception{
        emu.writeMemory(toAddr(address),new byte[]{(byte)value,(byte)(value>>>8),(byte)(value>>>16),(byte)(value>>>24)});
    }
    private long word(long address)throws Exception{
        byte[] b=emu.readMemory(toAddr(address),4);return(b[0]&255L)|((b[1]&255L)<<8)|((b[2]&255L)<<16)|((b[3]&255L)<<24);
    }
    private int octet(long address)throws Exception{return emu.readMemoryByte(toAddr(address))&255;}
    private void real(long address,double value)throws Exception{long b=Double.doubleToRawLongBits(value);word(address,b);word(address+4,b>>>32);}
    private String bits(long address)throws Exception{return String.format("%016x",word(address)|(word(address+4)<<32));}
    private static long low(double value){return Double.doubleToRawLongBits(value)&0xffffffffL;}
    private static long high(double value){return Double.doubleToRawLongBits(value)>>>32;}
    private double fp(int n){return Double.longBitsToDouble(reg(n)|(reg(n+1)<<32));}
    private void fpResult(double value){put(10,low(value));put(11,high(value));}
    private void text(long address,String text)throws Exception{emu.writeMemory(toAddr(address),(text+"\0").getBytes(StandardCharsets.UTF_8));}
    private String text(long address)throws Exception{
        if(address==0)return "<null>";var out=new java.io.ByteArrayOutputStream();
        for(int i=0;i<4096;i++){int c=octet(address+i);if(c==0)return out.toString(StandardCharsets.UTF_8);out.write(c);}
        throw new IllegalStateException("Unterminated string");
    }
    private static String hex(byte[] bytes){return HexFormat.of().formatHex(bytes);}
    private String sha(long address,int size)throws Exception{return hex(MessageDigest.getInstance("SHA-256").digest(emu.readMemory(toAddr(address),size)));}
    private void boundary(String name){boundaries.merge(name,1L,Long::sum);}
    private static Map<String,Object> row(Object... v){Map<String,Object> r=new LinkedHashMap<>();for(int i=0;i<v.length;i+=2)r.put((String)v[i],v[i+1]);return r;}
    private boolean sdk(long target)throws Exception{
        switch((int)target){
        case 0x40081388:boundary("millis_for_diagnostics");put(10,0);return true;
        case 0x4008c2b4:boundary("basename_for_diagnostics");return true;
        case 0x401c00b4:boundary("__divsf3_for_diagnostics");put(10,0);return true;
        case 0x4012d530:boundary("__extendsfdf2_for_diagnostics");fpResult(0);return true;
        case 0x400d21cc:case 0x400d74f8:case 0x400e1160:case 0x400de56c:boundary("diagnostic_output");return true;
        case 0x400db014:boundary("OEM_assertion");throw new Assertion(reg(11));
        case 0x401367a0:throw new IllegalStateException("Original stack-canary failure");
        case 0x401c0198:boundary("__floatunsidf");fpResult((double)reg(10));return true;
        case 0x401c01a4:boundary("__floatsidf");fpResult((double)(int)reg(10));return true;
        case 0x4012cb84:boundary("__adddf3");fpResult(fp(10)+fp(12));return true;
        case 0x4012ccd8:boundary("__subdf3");fpResult(fp(10)-fp(12));return true;
        case 0x4012cf40:boundary("__muldf3");fpResult(fp(10)*fp(12));return true;
        case 0x4012d144:boundary("__divdf3");fpResult(fp(10)/fp(12));return true;
        case 0x4012d29c:case 0x4012d328:
            boundary(target==0x4012d29cL?"__gtdf2":"__gedf2");put(10,Double.isNaN(fp(10))||Double.isNaN(fp(12))?-1:fp(10)>fp(12)?1:fp(10)<fp(12)?-1:0);return true;
        case 0x4012d350:case 0x4012d2c4:
            boundary(target==0x4012d350L?"__ltdf2":"__ledf2");put(10,Double.isNaN(fp(10))||Double.isNaN(fp(12))?1:fp(10)>fp(12)?1:fp(10)<fp(12)?-1:0);return true;
        case 0x4012d3e0:boundary("__fixdfsi_finite_fixture_domain");
            if(!Double.isFinite(fp(10))||fp(10)<-2147483648.0||fp(10)>=2147483648.0)throw new IllegalStateException("Unreviewed signed conversion");
            put(10,(int)fp(10));return true;
        case 0x4012d42c:boundary("__fixunsdfsi_estimator_boundaries");double value=fp(10);
            put(10,Double.isNaN(value)||value>=4294967296.0?0xffffffffL:value<=0?0:(long)value);return true;
        case 0x4000c44c:boundary("ROM_memset");byte[] fill=new byte[(int)reg(12)];Arrays.fill(fill,(byte)reg(11));emu.writeMemory(toAddr(reg(10)),fill);return true;
        case 0x4000c2c8:boundary("ROM_memcpy");if(reg(12)!=0)emu.writeMemory(toAddr(reg(10)),emu.readMemory(toAddr(reg(11)),(int)reg(12)));return true;
        case 0x400014c0:boundary("ROM_strlen");put(10,text(reg(10)).getBytes(StandardCharsets.UTF_8).length);return true;
        case 0x400015d4:boundary("ROM_strncpy_nonoverlapping");byte[] source=text(reg(11)).getBytes(StandardCharsets.UTF_8),copy=new byte[(int)reg(12)];
            System.arraycopy(source,0,copy,0,Math.min(source.length,copy.length));emu.writeMemory(toAddr(reg(10)),copy);return true;
        case 0x4013cd98:
            boundary("C_snprintf_reviewed_coffee_formats");
            String format=text(reg(12)),label=text(reg(13)),suffix=text(word(reg(1))),rendered;
            double water=fp(14);int minutes=(int)word(reg(1)+4),fahrenheit=(int)word(reg(1)+8);
            if(format.equals("%s %3.1f%s"))rendered=String.format(Locale.ROOT,"%s %3.1f%s",label,water,suffix);
            else if(format.equals("%s %3.1f%s (%d min)"))rendered=String.format(Locale.ROOT,"%s %3.1f%s (%d min)",label,water,suffix,minutes);
            else if(format.equals("%s %3.1f%s (%d min @%dF)"))rendered=String.format(Locale.ROOT,"%s %3.1f%s (%d min @%dF)",label,water,suffix,minutes,fahrenheit);
            else throw new IllegalStateException("Unreviewed format "+format);
            long destination=reg(10);int capacity=(int)reg(11);text(destination,rendered.substring(0,Math.min(rendered.length(),capacity-1)));put(10,rendered.length());return true;
        default:return false;
        }
    }
    private long[] execute(long entry,long... args)throws Exception{
        if(!ALLOWED.contains(entry))throw new IllegalStateException(String.format("Unreviewed function %08x",entry));
        bodies.add(entry);Instruction prologue=currentProgram.getListing().getInstructionAt(toAddr(entry));
        if(prologue==null||!prologue.getMnemonicString().equalsIgnoreCase("entry"))throw new IllegalStateException("No original ENTRY");
        long[] caller=new long[16];for(int i=0;i<16;i++){caller[i]=reg(i);put(i,0);}put(1,caller[1]-prologue.getScalar(1).getUnsignedValue());
        for(int i=0;i<args.length;i++)put(i+2,args[i]);emu.getEmulator().setExecuteAddress(entry+prologue.getLength());
        while(true){
            if(--budget<0)throw new IllegalStateException("Original instruction budget exceeded");if((budget&255)==0)monitor.checkCancelled();
            Address pc=emu.getExecutionAddress();Instruction instruction=currentProgram.getListing().getInstructionAt(pc);
            if(instruction==null)throw new IllegalStateException("No original instruction at "+pc);String mnemonic=instruction.getMnemonicString().toLowerCase(Locale.ROOT);
            if(mnemonic.startsWith("retw")){long[] result=new long[6];for(int i=0;i<6;i++)result[i]=reg(i+2);for(int i=0;i<16;i++)put(i,caller[i]);return result;}
            if(mnemonic.startsWith("loop"))throw new IllegalStateException("Unreviewed hardware LOOP at "+pc);
            long next=pc.getOffset()+instruction.getLength();
            if(mnemonic.equals("memw")){boundary("MEMW_sequential_fixture_memory");emu.getEmulator().setExecuteAddress(next);continue;}
            if(mnemonic.equals("call8")||mnemonic.equals("callx8")){
                long target;if(mnemonic.equals("callx8"))target=emu.readRegister(instruction.getRegister(0)).longValue()&0xffffffffL;
                else{Address[] flows=instruction.getFlows();if(flows.length!=1)throw new IllegalStateException("Ambiguous call");target=flows[0].getOffset();}
                if(!sdk(target)){long[] outgoing=new long[6];for(int i=0;i<6;i++)outgoing[i]=reg(10+i);long[] returned=execute(target,outgoing);for(int i=0;i<6;i++)put(10+i,returned[i]);}
                emu.getEmulator().setExecuteAddress(next);continue;
            }
            if(!emu.step(monitor))throw new IllegalStateException("Original instruction failed at "+pc+": "+emu.getLastError());
        }
    }
    private void parameters(long address,JsonObject input)throws Exception{
        word(address,input.get("id").getAsLong());word(address+4,input.get("abv").getAsLong());word(address+8,input.get("ibu").getAsLong());
        int offset=16;for(String name:List.of("time","water","intensity","temperature")){
            JsonArray value=input.getAsJsonArray(name);real(address+offset,value.get(0).getAsDouble());word(address+offset+8,value.get(1).getAsLong());offset+=16;}
        emu.writeMemory(toAddr(address+80),new byte[]{(byte)input.get("resumed").getAsInt()});
    }
    private Map<String,Object> parameters(long address)throws Exception{
        Map<String,Object> out=row("id",word(address),"abv",word(address+4),"ibu",word(address+8));int offset=16;
        for(String name:List.of("time","water","intensity","temperature")){out.put(name,List.of(bits(address+offset),word(address+offset+8)));offset+=16;}
        out.put("resumed",octet(address+80));return out;
    }
    private Map<String,Object> stats(long address)throws Exception{return row("rate",bits(address),"duration",word(address+8),"pressure",bits(address+16),"maximum",bits(address+24),"added",word(address+32));}
    private Map<String,Object> record(long address)throws Exception{
        long count=word(address+24),strings=word(address+16);if(count>256||strings>64)throw new IllegalStateException("Invalid fixture record count");
        List<Object> code=new ArrayList<>(),texts=new ArrayList<>();
        for(int i=0;i<count;i++)code.add(List.of(word(word(address+20)+8*i),word(word(address+20)+8*i+4)));
        for(int i=0;i<strings;i++)texts.add(text(word(word(address+12)+4*i)));
        return row("type",word(address),"id",word(address+4),"name",text(word(address+8)),"code",code,"strings",texts);
    }
    private Map<String,Object> one(JsonObject input)throws Exception{
        for(int i=0;i<16;i++){put(i,0);emu.writeRegister("f"+i,0L);}for(String n:List.of("SAR","PS","LCOUNT","LBEG","LEND"))emu.writeRegister(n,0L);
        emu.writeMemory(toAddr(STACK-0x5000),new byte[0x14000]);put(1,STACK);budget=500000;
        word(0x3ffc4654L,0x5a5a5a5aL);word(BUFFER+4,0xffffffffL);word(WRITER,BUFFER);execute(0x400dfea0L,BUFFER);
        int stringCount=input.get("initial_strings").getAsInt(),codeCount=input.get("initial_code").getAsInt();
        if(stringCount>0){text(STRING,"seed-00");execute(0x400e0f18L,WRITER,10,77,STRING);
            for(int i=1;i<stringCount;i++){text(STRING,String.format("seed-%02d",i));execute(0x400e0e84L,BUFFER,STRING);}}
        for(int i=0;i<codeCount;i++)execute(0x400d752cL,WRITER,9,i);
        parameters(BUFFER+32,JsonParser.parseString("{\"id\":28,\"abv\":7,\"ibu\":11,\"time\":[600,6],\"water\":[9,0],\"intensity\":[4,7],\"temperature\":[70,4],\"resumed\":1}").getAsJsonObject());
        real(BUFFER+0x78,2);word(BUFFER+0x80,321);real(BUFFER+0x88,1234);real(BUFFER+0x90,88);word(BUFFER+0x98,456);
        parameters(PARAMS,input.getAsJsonObject("parameters"));real(SENSOR+0x250,input.get("pressure").getAsDouble());real(SENSOR+0x278,input.get("maximum").getAsDouble());
        long string=0;if(!input.get("text").isJsonNull()){string=STRING;text(STRING,input.get("text").getAsString());}
        JsonArray code=input.getAsJsonArray("code"),args=input.getAsJsonArray("args");
        for(int i=0;i<code.size();i++){JsonArray pair=code.get(i).getAsJsonArray();word(CODE+8*i,pair.get(0).getAsLong());word(CODE+8*i+4,pair.get(1).getAsLong());}
        long[] value={0,0};long assertion=0,selected=0;double number;
        try{switch(input.get("action").getAsInt()){
        case 0:selected=execute(0x400d7df8L,WRITER,args.get(0).getAsLong())[0];break;
        case 1:execute(0x400dfe58L,BUFFER);break;
        case 2:execute(0x400e0f18L,WRITER,args.get(0).getAsLong(),args.get(1).getAsLong(),string);break;
        case 3:execute(0x400d752cL,WRITER,args.get(0).getAsLong(),args.get(1).getAsLong());break;
        case 4:execute(0x400e0e84L,BUFFER,string);break;
        case 5:execute(0x400e3e24L,BUFFER,input.get("null_code").getAsInt()==0?CODE:0,code.size());break;
        case 6:number=args.get(0).getAsDouble();long[] rate=execute(0x400e0f5cL,low(number),high(number));value[0]=rate[0];value[1]=rate[1];break;
        case 7:value[0]=execute(0x400e42d0L,PARAMS+48)[0];break;
        case 8:execute(0x400e423cL,WRITER,string);break;
        case 9:execute(0x400d7568L,WRITER);break;
        case 10:execute(0x400e4380L,WRITER,args.get(0).getAsLong(),args.get(1).getAsLong());break;
        case 11:
            word(STACK,input.get("added").getAsLong());double pressure=input.get("pressure").getAsDouble(),maximum=input.get("maximum").getAsDouble();
            execute(0x400ec670L,WRITER,PARAMS,low(pressure),high(pressure),low(maximum),high(maximum));break;
        case 12:value[0]=execute(0x400ec7b4L,BUFFER,PARAMS,SENSOR,input.get("imperial").getAsLong())[0];break;
        case 13:execute(0x400ed15cL,WRITER,STATS,args.get(0).getAsLong(),SENSOR);break;
        case 14:
            word(PROGRAM+8,STRING);word(PROGRAM+20,CODE);word(PROGRAM+24,code.size());
            real(STACK,args.get(1).getAsDouble());number=args.get(0).getAsDouble();
            value[0]=execute(0x400ec52cL,PROGRAM,0,0,0,low(number),high(number))[0];break;
        case 15:value[0]=execute(0x401bf2ccL,args.get(0).getAsLong())[0];break;
        default:throw new IllegalArgumentException("Unknown action");
        }}catch(Assertion failure){assertion=failure.line;}
        return row("assertion",assertion,"value",value,"buffer",record(BUFFER),"parameters",parameters(BUFFER+32),"stats",stats(BUFFER+0x78),
            "static_stats",stats(STATS),"selected",selected==0?null:record(selected),"code_storage_sha256",sha(BUFFER+0xa0,256*8),"string_storage_sha256",sha(BUFFER+0x9a0,64*48));
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=3)throw new IllegalArgumentException("Expected cases, verified layout, output JSON");
        JsonObject spec=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonObject(),layout=JsonParser.parseString(Files.readString(Path.of(args[1]))).getAsJsonObject();
        byte[] raw=Files.readAllBytes(Path.of(layout.get("input_path").getAsString()));String digest=hex(MessageDigest.getInstance("SHA-256").digest(raw));
        if(!digest.equals("d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27"))throw new IllegalStateException("Wrong original image");
        for(JsonElement e:layout.getAsJsonArray("segments")){JsonObject s=e.getAsJsonObject();int size=s.get("size").getAsInt();if(size==0)continue;byte[] bytes=new byte[size];
            currentProgram.getMemory().getBytes(toAddr(s.get("address").getAsLong()),bytes);int offset=s.get("offset").getAsInt();if(!Arrays.equals(bytes,Arrays.copyOfRange(raw,offset,offset+size)))throw new IllegalStateException("Project differs from original image");}
        emu=new EmulatorHelper(currentProgram);
        try{List<Object> output=new ArrayList<>();int index=0;
            for(JsonElement input:spec.getAsJsonArray("cases")){try{output.add(one(input.getAsJsonObject()));}catch(Exception error){throw new IllegalStateException("Case "+index+" "+input,error);}
                index++;if(index%100==0)println("Original program repository cases: "+index);}
            List<Object> functions=new ArrayList<>();for(long entry:bodies){Function function=getFunctionAt(toAddr(entry));if(function==null)throw new IllegalStateException("Missing function extent");
                List<Object> spans=new ArrayList<>();var ranges=function.getBody().getAddressRanges();while(ranges.hasNext()){var range=ranges.next();byte[] bytes=new byte[(int)range.getLength()];
                    currentProgram.getMemory().getBytes(range.getMinAddress(),bytes);spans.add(row("address",range.getMinAddress().getOffset(),"size",bytes.length,"sha256",hex(MessageDigest.getInstance("SHA-256").digest(bytes))));}
                functions.add(row("entry",entry,"ranges",spans));}
            Map<String,Object> result=row("schema",1,"input_sha256",digest,"cases",output,"original_functions",functions,"boundary_calls",boundaries,
                "scope","Original Xtensa Program/Buffer/Writer/Resolver instructions, static DROM programs, reviewed ENTRY/CALL8/RETW register windows and sequential MEMW; explicit pinned SDK scalar math, reviewed coffee snprintf formats, ROM memory/string and diagnostic services",
                "hardware_tested",false,"full_cpu_emulation",false);
            Files.writeString(Path.of(args[2]),new GsonBuilder().serializeNulls().create().toJson(result)+"\n");println("Wrote "+index+" original repository cases");
        }finally{emu.dispose();}
    }
}
