// Executes original Buzzer, System alarm and Core timer instruction bodies.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;

public class EmulateBuzzer extends GhidraScript {
    private static final long CORE=0x3ff30000L, BUZZER=CORE+0x4660, SYSTEM=0x3ff40000L,
        STATE=0x3ff42000L, CAPTURE=0x3ff44000L, ARG=0x3ff45000L, TEXT=0x3ff46000L,
        CONSOLE=0x3ff47000L;
    private static final long[] CALLBACKS={0x400d5d40L,0x400d144cL,0x400d1460L,0x400d08e0L,0x400d08f4L};
    private static final String[] NAMES={"i","on","off","setf ","setd "};
    private static final String[] DESCRIPTIONS={"Displays Buzzer state","Turn buzzer on","Turn buzzer off","Sets buzzer frequency","Sets buzzer duty cycle"};
    private List<Object> events;
    private long now,step,lastClock,assertionLine;
    private boolean constructorExecuted, inAssertion;
    private static String sha(byte[] bytes)throws Exception {
        return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));
    }
    private static Map<String,Object> row(Object... values) {
        Map<String,Object> result=new LinkedHashMap<>();
        for(int i=0;i<values.length;i+=2)result.put((String)values[i],values[i+1]);return result;
    }
    private void event(long... values) {
        List<Long> output=new ArrayList<>();for(long value:values)output.add(value&0xffffffffL);
        while(output.size()<6)output.add(0L);events.add(output);
    }
    private String string(Z119Execution e,long pointer)throws Exception {
        StringBuilder result=new StringBuilder();
        for(int i=0;i<512;i++){long value=e.read(pointer+i,1);if(value==0)return result.toString();result.append((char)value);}
        throw new IllegalStateException("Unterminated string boundary");
    }
    private long decimal(String text) {
        java.util.regex.Matcher match=java.util.regex.Pattern.compile("^[\\t\\n\\u000b\\f\\r ]*([+-]?[0-9]+)").matcher(text);
        if(!match.find())return 0;
        long result=Long.parseLong(match.group(1));
        if(result<Integer.MIN_VALUE||result>Integer.MAX_VALUE)throw new IllegalArgumentException("Outside defined atoi input domain");
        return result&0xffffffffL;
    }
    private void boundary(Z119Execution e,long target)throws Exception {
        if(e.arithmetic(target))return;
        switch((int)target){
        case 0x400d141c:case 0x400f6d58:e.invoke(target);return;
        case 0x40081388:
            if(inAssertion){e.countBoundary("assertion_diagnostic_clock");e.register(10,now);return;}
            e.countBoundary("millis");lastClock=now;now=(now+step)&0xffffffffL;event(3,lastClock);e.register(10,lastClock);return;
        case 0x4012895c:
            e.countBoundary("ledcAttachPin");event(0,e.register(10),e.register(11));return;
        case 0x40128738:
            e.countBoundary("ledcSetup");event(1,e.register(10),e.register(12),e.register(13),e.register(14));
            // Deliberately return zero: the OEM ignores the SDK's actual-frequency result.
            e.number(10,0.0);return;
        case 0x40128874:
            e.countBoundary("ledcWrite");event(2,e.register(10),e.register(11));return;
        case 0x400566c4:
            e.countBoundary("ROM_atoi_representable_int32");long value=decimal(string(e,e.register(10)));event(6,value);e.register(10,value);return;
        case 0x400f5038:
            e.countBoundary("string_constructor");e.write(e.register(10),e.register(11),4);return;
        case 0x400f6550:
            e.countBoundary("console_namespace_registration");
            if(!string(e,e.read(e.register(11),4)).equals("Buzzer"))throw new IllegalStateException("Wrong console namespace");
            e.write(e.register(10),CONSOLE,4);event(4);return;
        case 0x400f64e8:case 0x401bf3c0:
            e.countBoundary("string_or_function_destructor");return;
        case 0x400f6c14: {
            e.countBoundary("console_command_registration");
            if(e.register(10)!=CONSOLE||e.read(e.register(14),4)!=BUZZER)throw new IllegalStateException("Wrong console binding");
            long callback=e.read(e.register(14)+12,4);int index=-1;
            for(int i=0;i<CALLBACKS.length;i++)if(callback==CALLBACKS[i])index=i;
            if(index<0||!string(e,e.register(12)).equals(NAMES[index])||!string(e,e.register(13)).equals(DESCRIPTIONS[index]))
                throw new IllegalStateException("Wrong original console command");
            event(5,index,e.register(11));return;
        }
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc: {
            if(inAssertion){
                e.countBoundary("assertion_diagnostic_log");
                if(e.register(15)!=0x118||!string(e,e.read(e.register(1),4)).equals("_state.IsAlarmOn == _buzzer.isEnabled()"))
                    throw new IllegalStateException("Wrong assertion diagnostic");
                return;
            }
            e.countBoundary("diagnostic_format_and_output");
            String truth=string(e,e.read(e.register(1),4));
            if(e.register(15)==0x57){
                if(!truth.equals("true")&&!truth.equals("false"))throw new IllegalStateException("Wrong info boolean");
                event(7,0,lastClock,truth.equals("true")?1:0,e.read(e.register(1)+4,4),e.read(e.register(1)+8,4));
            }else if(e.register(15)==0x11a){
                if(!truth.equals("True")&&!truth.equals("False"))throw new IllegalStateException("Wrong alarm boolean");
                event(7,1,lastClock,truth.equals("True")?1:0);
            }else throw new IllegalStateException("Unexpected diagnostic line");
            return;
        }
        case 0x400db014: {
            // Execute the original returning helper as well. Its clock/log/
            // delay form one diagnostic service event; functional fixture
            // time is not advanced by that modeled wall-clock delay.
            if(inAssertion)throw new IllegalStateException("Recursive assertion");
            assertionLine=e.register(11);
            if(assertionLine!=0x118||!string(e,e.register(12)).equals("_state.IsAlarmOn == _buzzer.isEnabled()"))
                throw new IllegalStateException("Unknown assertion site");
            event(8,assertionLine);long count=e.read(0x3ffc2e84L,4);
            inAssertion=true;try{e.invoke(target);}finally{inAssertion=false;}
            if(e.read(0x3ffc2e84L,4)!=((count+1)&0xffffffffL))throw new IllegalStateException("Wrong assertion counter increment");
            return;
        }
        case 0x401289a0:
            if(!inAssertion||e.register(10)!=200)throw new IllegalStateException("Unexpected assertion delay");
            e.countBoundary("assertion_delay_200ms");return;
        default:throw new IllegalStateException(String.format("Unreviewed buzzer boundary %08x",target));
        }
    }
    private void constructorSlice(Z119Execution e)throws Exception {
        // Execute the contiguous Buzzer member initializer stores in Core's
        // constructor, with the registers established immediately before them.
        // This does not claim to execute or recover the complete Core constructor.
        constructorExecuted=true;e.register(2,0);e.register(4,CORE);e.register(5,CORE+0x4600);
        e.emulator.getEmulator().setExecuteAddress(0x40120ca5L);
        for(int count=0;count<40;count++){
            long pc=e.emulator.getExecutionAddress().getOffset();if(pc==0x40120ce3L)return;
            Instruction instruction=currentProgram.getListing().getInstructionAt(toAddr(pc));
            if(instruction==null||instruction.getMnemonicString().startsWith("call"))throw new IllegalStateException("Unexpected constructor slice instruction at "+Long.toHexString(pc));
            if(!e.emulator.step(monitor))throw new IllegalStateException("Constructor slice execution failed");
        }
        throw new IllegalStateException("Constructor slice budget exceeded");
    }
    @Override public void run()throws Exception {
        String[] args=getScriptArgs();if(args.length!=3)throw new IllegalArgumentException("Expected cases, layout and output");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();
        JsonObject layout=JsonParser.parseString(Files.readString(Path.of(args[1]))).getAsJsonObject();
        byte[] raw=Files.readAllBytes(Path.of(layout.get("input_path").getAsString()));
        if(!sha(raw).equals(Z119Execution.IMAGE_SHA))throw new IllegalStateException("Wrong original image");
        for(JsonElement value:layout.getAsJsonArray("segments")){
            JsonObject s=value.getAsJsonObject();int size=s.get("size").getAsInt();if(size==0)continue;
            byte[] loaded=new byte[size];currentProgram.getMemory().getBytes(toAddr(s.get("address").getAsLong()),loaded);
            int offset=s.get("offset").getAsInt();
            if(!Arrays.equals(loaded,Arrays.copyOfRange(raw,offset,offset+size)))throw new IllegalStateException("Original bytes differ");
        }
        // Address-taken callbacks were omitted by the initial auto-analysis.
        // Decode their evidenced ENTRY addresses in this transient read-only view.
        for(long entry:new long[]{0x400d141cL,0x400f6d58L,0x400f6e78L,0x40080570L,0x400dbc40L,
                0x400d5d40L,0x400d144cL,0x400d1460L,0x400d08e0L,0x400d08f4L,0x400db014L,0x40120a04L}) {
            if(getInstructionAt(toAddr(entry))==null&&!disassemble(toAddr(entry)))throw new IllegalStateException("Cannot decode "+Long.toHexString(entry));
            if(getFunctionAt(toAddr(entry))==null&&createFunction(toAddr(entry),"buzzer_validation_"+Long.toHexString(entry))==null)
                throw new IllegalStateException("Cannot define "+Long.toHexString(entry));
        }
        List<Object> results=new ArrayList<>(),functions=new ArrayList<>();
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary)){
            for(JsonElement item:inputs){
                JsonObject input=item.getAsJsonObject();JsonArray w=input.getAsJsonArray("state");
                e.resetRegisters();e.clear(CORE,0x18000);events=new ArrayList<>();assertionLine=0;
                e.write(0x3ffc4654L,0x705391adL,4);e.write(0x3ffc2e88L,CORE,4);e.write(0x3ffc2e84L,0xfffffffeL,4);
                e.write(BUZZER+4,w.get(0).getAsLong(),1);e.write(BUZZER+8,w.get(1).getAsLong(),4);
                e.write(BUZZER+12,w.get(2).getAsLong(),1);e.write(BUZZER+16,w.get(3).getAsLong(),4);
                e.write(BUZZER+20,w.get(4).getAsLong(),4);
                for(int i=0;i<3;i++)e.write(BUZZER+24+i,w.get(5+i).getAsLong(),1);
                e.write(SYSTEM+0x8c,STATE,4);e.write(SYSTEM+0x90,BUZZER,4);
                e.write(STATE+0x3c,w.get(8).getAsLong(),1);e.write(STATE+0x40,w.get(9).getAsLong(),4);
                e.write(CAPTURE,BUZZER,4);e.write(ARG+4,TEXT,4);
                e.emulator.writeMemory(toAddr(TEXT),(input.get("text").getAsString()+"\0").getBytes(StandardCharsets.UTF_8));
                now=w.get(10).getAsLong();step=w.get(11).getAsLong();
                int result=0;
                for(JsonElement action:input.getAsJsonArray("actions")){
                    JsonArray a=action.getAsJsonArray();int operation=a.get(0).getAsInt();long argument=a.get(1).getAsLong();
                    switch(operation){
                    case 0:e.write(ARG,argument,1);e.register(10,BUZZER);e.register(11,ARG);e.invoke(0x400d141cL);break;
                    case 1:e.invoke(0x40080570L);break;
                    case 2:e.register(10,SYSTEM);e.register(11,argument);e.invoke(0x400dbc40L);break;
                    case 3:e.register(10,BUZZER);e.invoke(0x400f6e78L);break;
                    case 4:e.register(10,BUZZER);e.invoke(0x400f6d58L);break;
                    case 5:case 6:case 7:case 8:case 9:
                        e.register(10,CAPTURE);e.register(11,ARG+4);e.invoke(CALLBACKS[operation-5]);break;
                    case 10:constructorSlice(e);break;
                    default:throw new IllegalStateException("Bad test operation");
                    }
                }
                List<Long> state=new ArrayList<>();
                for(int offset:new int[]{4,8,12,16,20,24,25,26})state.add(e.read(BUZZER+offset,(offset==8||offset==16||offset==20)?4:1));
                state.add(e.read(STATE+0x3c,1));state.add(e.read(STATE+0x40,4));state.add(now);state.add(step);
                results.add(row("state",state,"result",result,"assertion_line",assertionLine,"effects",events));
            }
            for(String address:e.functions.keySet()){
                Function function=currentProgram.getFunctionManager().getFunctionAt(toAddr(Long.parseLong(address.substring(2),16)));
                if(function==null)throw new IllegalStateException("Missing original function bounds "+address);
                List<Object> ranges=new ArrayList<>();
                for(AddressRange range:function.getBody().getAddressRanges()){
                    int size=(int)range.getLength();byte[] bytes=new byte[size];currentProgram.getMemory().getBytes(range.getMinAddress(),bytes);
                    ranges.add(row("address",range.getMinAddress().getOffset(),"size",size,"sha256",sha(bytes)));
                }
                functions.add(row("address",address,"calls",e.functions.get(address),"ranges",ranges));
            }
            if(constructorExecuted){
                byte[] bytes=new byte[0x3e];currentProgram.getMemory().getBytes(toAddr(0x40120ca5L),bytes);
                functions.add(row("address","0x40120ca5","kind","Core constructor Buzzer initializer slice","calls",1,
                    "ranges",List.of(row("address",0x40120ca5L,"size",bytes.length,"sha256",sha(bytes)))));
            }
            Files.writeString(Path.of(args[2]),new GsonBuilder().setPrettyPrinting().create().toJson(row(
                "input_sha256",Z119Execution.IMAGE_SHA,"cases",results,"original_functions",functions,
                "command_names",NAMES,"command_descriptions",DESCRIPTIONS,"modeled_boundaries",e.boundaries,
                "scope","Original buzzer/alarm/timer/console bodies and member initializer slice; LEDC, functional clock, formatting, returning assertion diagnostics/delay, C++ console storage and representable int32 ROM atoi are boundaries.",
                "hardware_tested",false,"full_cpu_emulation",false))+"\n");
        }
        println("Executed "+results.size()+" original buzzer scenarios");
    }
}
