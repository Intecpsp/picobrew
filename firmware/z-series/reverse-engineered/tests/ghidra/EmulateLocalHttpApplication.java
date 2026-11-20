// Original Z119 POST route, recipe parser, owned program and Core mailbox.
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

public class EmulateLocalHttpApplication extends GhidraScript {
    private static final long STACK=0x3ff80000L,BUFFER=0x3ff84000L,WRITER=0x3ff87000L,
        PROGRAM=0x3ff88000L,STATS=0x3ff88100L,CODE=0x3ff89000L,STRING=0x3ff8a000L,
        PARAMS=0x3ff8b000L,SENSOR=0x3ff8c000L,BACKEND=0x3ff8d000L,CONTEXT=0x3ff8e000L,ERROR=0x3ff8f000L,JSON=0x3ff90000L,DOCUMENT=0x3ff98000L;
    private static final Set<Long> ALLOWED=Set.of(0x400db014L,0x401172b8L,0x400f02e4L,0x400fa1f8L,0x400ef118L,0x400f944cL,0x400f9348L,0x400f9310L,0x40114f8cL,0x4011595cL,0x40114344L,0x400d165cL,0x401158bcL,0x400d0e60L,0x400d0f80L,0x400d0fb8L,0x400d12b0L,0x400d1508L,0x400d1590L,0x400d15a8L,0x400d15ccL,0x400d15f0L,0x400d1638L,0x400d585cL,0x400d598cL,0x400d752cL,0x400dee30L,0x400defa4L,0x400dfd9cL,0x400dfe58L,0x400dfea0L,0x400dff0cL,0x400e0e84L,0x400e0f18L,0x400e0f5cL,0x400ec360L,0x400ec52cL,0x400ec670L,0x400f0268L,0x400f02a4L,0x400f02b8L,0x400fa8d0L,0x401118e8L,0x40112430L,0x401129dcL,0x40112a80L,0x40112a9cL,0x40113338L,0x401135dcL,0x4011368cL,0x40113724L,0x401137a8L,0x4011382cL,0x40113ebcL,0x40113fb8L,0x40114068L,0x401140b0L,0x4011416cL,0x401142bcL,0x40114804L,0x401149e0L,0x40116968L,0x401bf254L,0x401bf3d4L,0x401bf40cL,0x401bf484L,0x401bf62cL,0x401bf678L,0x401bf68cL,0x401bf6b4L,0x401bf6d8L,0x401bf6ecL,0x401bf724L,0x401bf79cL,0x401bf7acL,0x401bf9dcL,0x401bf9f8L,0x401bfa6cL);
    private final Set<Long> bodies=new TreeSet<>();
    private final Map<String,Long> boundaries=new TreeMap<>();
    private EmulatorHelper emu;
    private int budget;
    private long responseStatus,responseLength; private String responseType;
    private final java.io.ByteArrayOutputStream responseBody=new java.io.ByteArrayOutputStream();
    private static final long SERVER=0x3ffa0000L, CLIENT=0x3ffa1000L, REQUEST=0x3ffa1100L, EVENT=0x3ffb0000L, QUEUE=0x3ffb2000L, VTABLE=0x3ffb4000L;
    private String errorText;
    private final List<Long> assertionLines=new ArrayList<>(), assertionDelays=new ArrayList<>();
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
        case 0x40001274:boundary("ROM_strcmp");put(10,text(reg(10)).compareTo(text(reg(11))));return true;
        case 0x4012fecc:boundary("CXX_static_guard_acquire_single_thread");put(10,octet(reg(10))==0?1:0);return true;
        case 0x4012ff60:boundary("CXX_static_guard_release_single_thread");emu.writeMemory(toAddr(reg(10)),new byte[]{1});return true;
        case 0x401c01d8:boundary("__floatundidf");fpResult(new java.math.BigInteger(Long.toUnsignedString(reg(10)|(reg(11)<<32))).doubleValue());return true;
        case 0x4012d268:boundary("__eqdf2");put(10,fp(10)==fp(12)?0:1);return true;
        case 0x400d97c4:
            boundary("recovered_HTTP_header_writer");responseStatus=reg(12);responseLength=reg(13);responseType=reg(14)==0?null:text(reg(14));return true;
        case 0x7ff31000:
            boundary("Print_virtual_write");responseBody.write(emu.readMemory(toAddr(reg(11)),(int)reg(12)));put(10,reg(12));return true;
        case 0x400d06b4:boundary("CoreEvent_diagnostic_description");text(reg(11),"");return true;
        case 0x400fa7f8:
            boundary("ErrorText_format");
            if(!text(reg(11)).equals("deserializeJson error %d %s"))throw new IllegalStateException("Unexpected error format");
            errorText="deserializeJson error "+reg(12)+" "+text(reg(13));text(reg(10),errorText);word(reg(10)+0x84,errorText.length());word(reg(10)+0x88,128-errorText.length());return true;

        case 0x40081388:boundary("millis_for_diagnostics");put(10,0);return true;
        case 0x4008c2b4:boundary("basename_for_diagnostics");return true;
        case 0x401c00b4:boundary("__divsf3");put(10,Float.floatToRawIntBits(Float.intBitsToFloat((int)reg(10))/Float.intBitsToFloat((int)reg(11))));return true;
        case 0x4012d530:boundary("__extendsfdf2_for_diagnostics");fpResult(0);return true;
        case 0x400d21cc:case 0x400d74f8:case 0x400e1160:case 0x400de56c:boundary("diagnostic_output");return true;
        case 0x400db014:assertionLines.add(reg(11));return false;
        case 0x401289a0:boundary("assertion_delay");assertionDelays.add(reg(10));return true;
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
            if(mnemonic.equals("trunc.s")){
                if(!Set.of(0x4011602eL,0x40116064L,0x401160baL,0x401160f0L).contains(pc.getOffset())||
                    !hex(instruction.getBytes()).equals("00409a"))throw new IllegalStateException("Unreviewed TRUNC.S site");
                float value=Float.intBitsToFloat(emu.readRegister("f0").intValue());
                if(!Float.isFinite(value)||value < -2147483648.0||value >= 2147483648.0)throw new IllegalStateException("TRUNC.S outside finite fixture domain");
                // Cadence Xtensa ISA Summary, TRUNC.S: scale zero is 1.0 and
                // signed conversion rounds toward zero. Ghidra 12.2's p-code
                // incorrectly uses integer bits 1 as the float scale here.
                boundary("reviewed_TRUNC_S_scale0_finite");put(4,(int)value);emu.getEmulator().setExecuteAddress(next);continue;
            }
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
        emu.writeMemory(toAddr(STACK-0x8000),new byte[0x24000]);
        emu.writeMemory(toAddr(SERVER),new byte[0x2000]);emu.writeMemory(toAddr(EVENT),new byte[0x5000]);
        emu.writeMemory(toAddr(0x3ffc1018L),new byte[0x58]);put(1,STACK);budget=3000000;
        word(0x3ffc4654L,0x5a5a5a5aL);word(0x3ffc2e84L,0);assertionLines.clear();assertionDelays.clear();word(BUFFER+4,0xffffffffL);word(WRITER,BUFFER);execute(0x400dfea0L,BUFFER);
        real(SENSOR+0x250,input.get("pressure").getAsDouble());real(SENSOR+0x278,input.get("maximum").getAsDouble());
        word(SERVER+0x8d0,SENSOR);word(SERVER+0x8d4,QUEUE);word(SERVER+0x8d8,EVENT);word(SERVER+0x8dc,BUFFER);
        emu.writeMemory(toAddr(EVENT),HexFormat.of().parseHex(input.get("current_hex").getAsString()));
        emu.writeMemory(toAddr(QUEUE+8),HexFormat.of().parseHex(input.get("queued_hex").getAsString()));
        word(EVENT,0x3f41f8d0L);word(QUEUE+8,0x3f41f8d0L);emu.writeMemory(toAddr(QUEUE+0xdf8),new byte[]{(byte)(input.get("pending").getAsBoolean()?1:0)});
        word(CLIENT,VTABLE);word(VTABLE+12,0x7ff31000L);
        String json=input.get("json").getAsString();text(SERVER+0x5c,json);word(SERVER+0x860,json.getBytes(StandardCharsets.UTF_8).length);
        text(REQUEST+12,input.get("path").getAsString());responseStatus=0;responseLength=0;responseType=null;responseBody.reset();
        execute(0x401172b8L,SERVER,CLIENT,REQUEST);long assertion=assertionLines.isEmpty()?0:assertionLines.get(0);
        return row("assertion",assertion,"assertion_lines",new ArrayList<>(assertionLines),"assertion_counter",word(0x3ffc2e84L),"assertion_delays",new ArrayList<>(assertionDelays),"status",responseStatus,"length",responseLength,"mime",responseType,"body",responseBody.toString(StandardCharsets.UTF_8),
            "buffer",record(BUFFER),"parameters",parameters(BUFFER+32),"stats",stats(BUFFER+0x78),
            "frozen",octet(SERVER+0x869)!=0,"pending",octet(QUEUE+0xdf8)!=0,
            "current_hex",hex(emu.readMemory(toAddr(EVENT+4),0xdec)),"queued_hex",hex(emu.readMemory(toAddr(QUEUE+12),0xdec)));
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=4)throw new IllegalArgumentException("Expected cases, verified layout, output JSON, progress JSON");
        JsonObject spec=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonObject(),layout=JsonParser.parseString(Files.readString(Path.of(args[1]))).getAsJsonObject();
        byte[] raw=Files.readAllBytes(Path.of(layout.get("input_path").getAsString()));String digest=hex(MessageDigest.getInstance("SHA-256").digest(raw));
        if(!digest.equals("d84e0106b69aa8a51ed62e56ae7215eb90908569e0f666b829e03fbc34c30b27"))throw new IllegalStateException("Wrong original image");
        for(JsonElement e:layout.getAsJsonArray("segments")){JsonObject s=e.getAsJsonObject();int size=s.get("size").getAsInt();if(size==0)continue;byte[] bytes=new byte[size];
            currentProgram.getMemory().getBytes(toAddr(s.get("address").getAsLong()),bytes);int offset=s.get("offset").getAsInt();if(!Arrays.equals(bytes,Arrays.copyOfRange(raw,offset,offset+size)))throw new IllegalStateException("Project differs from original image");}
        emu=new EmulatorHelper(currentProgram);
        try{List<Object> output=new ArrayList<>();int index=0;
            if(!spec.get("resume_progress").isJsonNull()){
                JsonObject prior=JsonParser.parseString(Files.readString(Path.of(spec.get("resume_progress").getAsString()))).getAsJsonObject();
                if(!prior.get("input_sha256").getAsString().equals(digest)||
                   !prior.get("case_sha256").equals(spec.get("case_sha256"))||
                   !prior.get("script_sha256").equals(spec.get("script_sha256")))throw new IllegalStateException("Progress belongs to different inputs or executor");
                for(JsonElement value:prior.getAsJsonArray("cases"))output.add(value);
                for(JsonElement value:prior.getAsJsonArray("original_entries"))bodies.add(value.getAsLong());
                for(var value:prior.getAsJsonObject("boundary_calls").entrySet())boundaries.put(value.getKey(),value.getValue().getAsLong());
                index=output.size();if(index>spec.getAsJsonArray("cases").size())throw new IllegalStateException("Oversized progress");
                println("Resuming verified original HTTP application case "+index);
            }
            for(;index<spec.getAsJsonArray("cases").size();){JsonElement input=spec.getAsJsonArray("cases").get(index);
                try{output.add(one(input.getAsJsonObject()));}catch(Exception error){throw new IllegalStateException("Case "+index+" "+input,error);}
                index++;if(index%25==0||index==spec.getAsJsonArray("cases").size()){
                    Map<String,Object> progress=row("schema",1,"input_sha256",digest,"case_sha256",spec.get("case_sha256"),
                        "script_sha256",spec.get("script_sha256"),"cases",output,"original_entries",bodies,"boundary_calls",boundaries);
                    Path path=Path.of(args[3]),temporary=Path.of(args[3]+".tmp");
                    Files.writeString(temporary,new GsonBuilder().serializeNulls().create().toJson(progress)+"\n");
                    Files.move(temporary,path,StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);
                    println("Original HTTP application cases: "+index);
                }}
            List<Object> functions=new ArrayList<>();for(long entry:bodies){Function function=getFunctionAt(toAddr(entry));if(function==null)throw new IllegalStateException("Missing function extent");
                List<Object> spans=new ArrayList<>();var ranges=function.getBody().getAddressRanges();while(ranges.hasNext()){var range=ranges.next();byte[] bytes=new byte[(int)range.getLength()];
                    currentProgram.getMemory().getBytes(range.getMinAddress(),bytes);spans.add(row("address",range.getMinAddress().getOffset(),"size",bytes.length,"sha256",hex(MessageDigest.getInstance("SHA-256").digest(bytes))));}
                functions.add(row("entry",entry,"ranges",spans));}
            Map<String,Object> result=row("schema",1,"input_sha256",digest,"cases",output,"original_functions",functions,"boundary_calls",boundaries,
                "scope","Original Xtensa POST dispatch, JSON deserializer, local recipe-to-program builder, returning diagnostic assertion handler, CoreEvent reset and complete one-slot mailbox publication; reviewed ENTRY/CALL8/RETW windows and sequential MEMW; explicit scalar math, ROM memory/string, static guards, bounded error formatting, recovered HTTP response writer, assertion delay and diagnostics boundaries. The assertion counter increment and return execute as original instructions. No sockets, concurrency, or physical hardware behavior established.",
                "hardware_tested",false,"full_cpu_emulation",false);
            Files.writeString(Path.of(args[2]),new GsonBuilder().serializeNulls().create().toJson(result)+"\n");println("Wrote "+index+" original HTTP application cases");
        }finally{emu.dispose();}
    }
}
