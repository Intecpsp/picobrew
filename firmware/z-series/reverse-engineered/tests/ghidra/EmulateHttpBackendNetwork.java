// Full HttpBackend network self-test, real HTTP/receiver/parser and returning assertions.
// Private request and formatted log bytes are hashed before any evidence is written.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;

public class EmulateHttpBackendNetwork extends GhidraScript {
    private static final long OBJECT=0x3ff00000L,EVENT=0x3ff10000L,STATE=0x3ff12000L,
        PROGRAM=0x3ff14000L,SENSOR=0x3ff20000L,UID=0x3ff30000L,TEXT=0x3ff31000L,
        LOCATIONS=0x3ff32000L,SOCKET=0x3ff35000L,CORE=0x3ff36000L,BOILER=0x3ff37000L,ERRNO=0x3ff37f00L;
    private long assertionLine,clock,water; private final List<Object> events=new ArrayList<>();
    private JsonObject input;private final List<byte[]> wires=new ArrayList<>();
    private int position,requests,current,yields;private boolean queued;private long queuedType;
    private byte[] wire(){if(current<0||current>=wires.size())throw new IllegalStateException("Unbound prescribed socket");return wires.get(current);}
    private void io(int effect,long value){events.add("io|"+effect+"|"+(value&0xffffffffL));}
    private long decimal(String value){java.util.regex.Matcher m=java.util.regex.Pattern.compile("^[\\t\\r\\n\\x0b\\f ]*([+-]?[0-9]+)").matcher(value);if(!m.find())return 0;java.math.BigInteger n=new java.math.BigInteger(m.group(1));return n.max(java.math.BigInteger.valueOf(Integer.MIN_VALUE)).min(java.math.BigInteger.valueOf(Integer.MAX_VALUE)).longValue()&0xffffffffL;}
    private static String sha(byte[] bytes)throws Exception{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));}
    private static Map<String,Object> row(Object... values){Map<String,Object> r=new LinkedHashMap<>();for(int i=0;i<values.length;i+=2)r.put((String)values[i],values[i+1]);return r;}
    private byte[] bytes(BackendConsoleExecution e,long p,int count)throws Exception{return e.emulator.readMemory(e.address(p),count);}
    private String string(BackendConsoleExecution e,long p)throws Exception{if(p==0)throw new IllegalStateException("Null string boundary");int n=0;while(n<65536&&e.read(p+n,1)!=0)n++;if(n==65536)throw new IllegalStateException("Unbounded string");return n==0?"":new String(bytes(e,p,n),StandardCharsets.UTF_8);}
    private void boundary(BackendConsoleExecution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        if(target==0x4012d48cL){e.countBoundary("__truncdfsf2");e.register(10,Float.floatToRawIntBits((float)e.number(10)));return;}
        switch((int)target){
        case 0x40128994:e.countBoundary("prescribed_yield");events.add("yield");clock=(clock+input.get("yield_ms").getAsLong())&0xffffffffL;if(++yields>4000)throw new IllegalStateException("Unbounded prescribed network wait");return;
        case 0x400ed668:{
            e.countBoundary("application_network_client_factory");if(requests>=wires.size())throw new IllegalStateException("More requests than prescribed responses");
            current=requests++;position=0;long accepted=input.get("factory").getAsLong();io(0,e.register(13));
            e.write(e.register(11),accepted!=0?SOCKET:0,4);e.write(e.register(12),accepted!=0?3:0,4);e.register(10,accepted);return;}
        case 0x40136960:e.countBoundary("SDK_free_heap");io(2,100000);e.register(10,100000);return;
        case 0x40132054:e.countBoundary("C_errno");io(12,113);e.register(10,ERRNO);return;
        case 0x400566c4:e.countBoundary("ROM_atoi_signed_decimal");e.register(10,decimal(string(e,e.register(10))));return;
        case 0x400566b4:e.countBoundary("ROM_itoa_decimal");put(e,e.register(11),Integer.toString((int)e.register(10)));return;
        case 0x40030010:{int count=(int)e.register(12);if(count<0||count>65536)throw new IllegalStateException("Socket write span");byte[] payload=bytes(e,e.register(11),count);events.add("write_bytes|"+count+"|"+sha(payload));e.countBoundary("socket_write_hashed");e.register(10,count);return;}
        case 0x40030020:{long count=wire().length-position;io(7,count);e.register(10,count);e.countBoundary("socket_available");return;}
        case 0x40030030:{long value=position<wire().length?wire()[position++]&255L:0xffffffffL;io(8,value);e.register(10,value);e.countBoundary("socket_read_byte");return;}
        case 0x40030040:e.countBoundary("socket_flush");io(6,0);return;
        case 0x40030060:{events.add("host|"+e.register(12)+"|"+sha(string(e,e.register(11)).getBytes(StandardCharsets.UTF_8)));position=0;e.write(ERRNO,113,4);e.register(10,input.get("connect").getAsLong());e.countBoundary("socket_connect_host_hashed");return;}
        case 0x40030070:e.countBoundary("socket_stop");io(11,0);return;
        case 0x40030080:{long value=position<wire().length?1:0;e.countBoundary("socket_connected");io(10,value);e.register(10,value);return;}
        case 0x4012a928:{int wanted=(int)e.register(12),count=Math.min(wanted,wire().length-position);if(wanted<0||wanted>65536)throw new IllegalStateException("Socket read span");byte[] payload=Arrays.copyOfRange(wire(),position,position+count);if(count>0)e.emulator.writeMemory(e.address(e.register(11)),payload);position+=count;events.add("read_bytes|"+wanted+"|"+count+"|"+sha(payload));e.register(10,count);e.countBoundary("Stream_readBytes_hashed");return;}
        case 0x400f02e4:e.countBoundary("CoreEvent_single_slot_mailbox");if(!queued){queued=true;queuedType=e.read(e.register(11)+4,4);}return;
        case 0x4012d42c:{double v=e.number(10);if(!Double.isFinite(v)||v<0||v>=4294967296.0)throw new IllegalStateException("Unsigned conversion domain");e.register(10,(long)v);e.countBoundary("finite_uint32_conversion");return;}
        case 0x4013b1d4:{double v=e.number(10);if(!Double.isFinite(v)||Math.abs(v)>1000000)throw new IllegalStateException("round domain");e.number(10,Math.copySign(Math.floor(Math.abs(v)+0.5),v));e.countBoundary("finite_round_half_away_from_zero");return;}
        case 0x4013b288:e.countBoundary("finite_positive_log");if(e.number(10)<=0)throw new IllegalStateException("log domain");e.number(10,Math.log(e.number(10)));return;
        case 0x4013b394:e.countBoundary("finite_positive_pow");if(e.number(10)<=0)throw new IllegalStateException("pow domain");e.number(10,Math.pow(e.number(10),e.number(12)));return;
        case 0x4013b678:e.countBoundary("finite_nonnegative_sqrt");if(e.number(10)<0)throw new IllegalStateException("sqrt domain");e.number(10,Math.sqrt(e.number(10)));return;
        case 0x400013ac:{String v=string(e,e.register(11));put(e,e.register(10),v);e.countBoundary("ROM_strcpy");return;}
        case 0x400ee634:e.countBoundary("Head_test_message_renderer");events.add("message|"+string(e,e.register(11)));return;
        case 0x40081388:e.countBoundary("prescribed_millis");e.register(10,clock);return;
        case 0x4008c2b4:{e.countBoundary("diagnostic_basename");String value=string(e,e.register(10));e.register(10,e.register(10)+Math.max(value.lastIndexOf('/'),value.lastIndexOf('\\'))+1);return;}
        case 0x400d21cc:e.countBoundary("diagnostic_output");log(e);return;
        case 0x400db014:assertionLine=e.register(11);events.add("assertion|"+string(e,e.register(10))+"|"+e.register(11)+"|"+string(e,e.register(12)));e.invoke(target);return;
        case 0x401289a0:if(e.register(10)!=200)throw new IllegalStateException("Unexpected assertion delay");e.countBoundary("assertion_delay_200ms");clock=(clock+200)&0xffffffffL;return;
        case 0x400d56c4:{events.add("scope|1|"+string(e,e.register(11))+"|"+basename(string(e,e.register(13)))+"|"+e.register(14)+"|"+string(e,e.register(12)));e.invoke(target);return;}
        case 0x400db438:{long p=e.register(10);events.add("scope|0|"+string(e,e.read(p,4))+"|"+basename(string(e,e.read(p+8,4)))+"|"+e.read(p+12,4)+"|"+string(e,e.read(p+4,4)));e.invoke(target);return;}
        case 0x40090e2c:e.countBoundary("prescribed_stack_high_water");e.register(10,water);return;
        case 0x40136e8c:e.countBoundary("prescribed_success_esp_task_wdt_reset");e.register(10,0);return;
        case 0x4012a5d8:e.invoke(target);return;
        case 0x401367a0:throw new IllegalStateException("Original stack guard failed");
        case 0x400015d4:{int n=(int)e.register(12);if(n<0||n>65536)throw new IllegalStateException("strncpy bound");byte[] out=new byte[n];boolean ended=false;for(int i=0;i<n;i++){if(!ended){out[i]=(byte)e.read(e.register(11)+i,1);if(out[i]==0)ended=true;}}if(n>0)e.emulator.writeMemory(e.address(e.register(10)),out);e.countBoundary("ROM_strncpy");return;}
        case 0x4000c2c8:{int n=(int)e.register(12);if(n<0||n>65536)throw new IllegalStateException("memcpy bound");if(n>0)e.emulator.writeMemory(e.address(e.register(10)),bytes(e,e.register(11),n));e.countBoundary("ROM_memcpy");return;}
        case 0x4000c44c:{int n=(int)e.register(12);if(n<0||n>65536)throw new IllegalStateException("memset bound");byte[] bytes=new byte[n];Arrays.fill(bytes,(byte)e.register(11));if(n>0)e.emulator.writeMemory(e.address(e.register(10)),bytes);e.countBoundary("ROM_memset");return;}
        case 0x40132040:{e.countBoundary("C_atof_decimal_prefix");String value=string(e,e.register(10));java.util.regex.Matcher m=java.util.regex.Pattern.compile("^[\\t\\n\\r ]*([+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?)").matcher(value);e.number(10,m.find()?Double.parseDouble(m.group(1)):0.0);return;}
        case 0x40001274:e.countBoundary("ROM_strcmp");e.register(10,string(e,e.register(10)).compareTo(string(e,e.register(11))));return;
        case 0x4012fecc:e.countBoundary("CXX_static_guard_acquire");e.register(10,e.read(e.register(10),1)==0?1:0);return;
        case 0x4012ff60:e.countBoundary("CXX_static_guard_release");e.write(e.register(10),1,1);return;
        case 0x401c01d8:e.countBoundary("__floatundidf");e.number(10,new java.math.BigInteger(Long.toUnsignedString(e.register(10)|(e.register(11)<<32))).doubleValue());return;
        case 0x400014c0:{long p=e.register(10);int n=0;if(p==0)throw new IllegalStateException("Null strlen");while(n<65536&&e.read(p+n,1)!=0)n++;if(n==65536)throw new IllegalStateException("strlen bound");e.register(10,n);e.countBoundary("ROM_strlen");return;}
        case 0x401462c4:{e.countBoundary("SDK_vsnprintf_Xtensa_va_list");String formatted=vaFormat(e,string(e,e.register(12)),e.register(13),e.register(14),(int)e.register(15));int capacity=(int)e.register(11);byte[] value=formatted.getBytes(StandardCharsets.UTF_8);int size=Math.min(value.length,Math.max(0,capacity-1));if(size>0)e.emulator.writeMemory(e.address(e.register(10)),Arrays.copyOf(value,size));if(capacity>0)e.write(e.register(10)+size,0,1);e.register(10,value.length);return;}
        case 0x4013cd98:{
            long p=e.register(10),capacity=e.register(11);String format=string(e,e.register(12));StringBuilder out=new StringBuilder();int argument=13;
            for(int i=0;i<format.length();i++){
                char ch=format.charAt(i);if(ch!='%'){out.append(ch);continue;}ch=format.charAt(++i);if(ch=='%'){out.append('%');continue;}
                boolean left=false,zero=false,space=false,plus=false;while("-0 +".indexOf(ch)>=0){if(ch=='-')left=true;if(ch=='0')zero=true;if(ch==' ')space=true;if(ch=='+')plus=true;ch=format.charAt(++i);}
                int width=0;while(ch>='0'&&ch<='9'){width=width*10+ch-'0';ch=format.charAt(++i);}int precision=-1;if(ch=='.'){precision=0;ch=format.charAt(++i);while(ch>='0'&&ch<='9'){precision=precision*10+ch-'0';ch=format.charAt(++i);}}
                String replacement;
                if(ch=='f'){
                    if((argument&1)!=0)argument++;double v=Double.longBitsToDouble(argument(e,argument)|argument(e,argument+1)<<32);argument+=2;
                    if(!Double.isFinite(v)||Math.abs(v)>1000000000||precision>12)throw new IllegalStateException("Unreviewed printf float domain");
                    boolean negative=Double.doubleToRawLongBits(v)<0;replacement=new java.math.BigDecimal(Math.abs(v)).setScale(precision<0?6:precision,java.math.RoundingMode.HALF_EVEN).toPlainString();if(negative)replacement="-"+replacement;else if(plus||space)replacement=(plus?"+":" ")+replacement;
                }else{long v=argument(e,argument++);if(ch=='d')replacement=Integer.toString((int)v);else if(ch=='u')replacement=Long.toString(v);else if(ch=='s')replacement=string(e,v);else if(ch=='c')replacement=String.valueOf((char)(v&255));else if(ch=='x'||ch=='X'){replacement=Long.toHexString(v);if(ch=='X')replacement=replacement.toUpperCase(Locale.ROOT);}else throw new IllegalStateException("Unreviewed bounded format; private text omitted");}
                while(replacement.length()<width){if(left)replacement+=" ";else if(zero&&replacement.startsWith("-"))replacement="-0"+replacement.substring(1);else replacement=(zero?"0":" ")+replacement;}out.append(replacement);
            }
            byte[] bytes=out.toString().getBytes(StandardCharsets.UTF_8);if(capacity<0||capacity>65536)throw new IllegalStateException("snprintf bound");int n=Math.min(bytes.length,Math.max(0,(int)capacity-1));if(n>0)e.emulator.writeMemory(e.address(p),Arrays.copyOf(bytes,n));if(capacity>0)e.write(p+n,0,1);e.register(10,bytes.length);e.countBoundary("bounded_snprintf_reviewed_fixed_point_half_even_string_integer");return;
        }
        default:
            // Data and thermistor callees execute their actual instructions.
            if((target>=0x400d0000L&&target<0x40120000L)||(target>=0x401bd000L&&target<0x401c0200L)){e.invoke(target);return;}
            throw new IllegalStateException(String.format("Unreviewed diagnostic dependency %08x",target));
        }
    }
    private String basename(String value){return value.substring(Math.max(value.lastIndexOf('/'),value.lastIndexOf('\\'))+1);}
    private String stackFormat(BackendConsoleExecution e,String format)throws Exception{return registerFormat(e,format,16);}
    private String registerFormat(BackendConsoleExecution e,String format,int word)throws Exception{
        java.util.regex.Matcher m=java.util.regex.Pattern.compile("%([-+ #0]*)([0-9]*)(?:\\.([0-9]+))?(l?)([sdxXcuf])").matcher(format);StringBuilder out=new StringBuilder();int position=0;
        while(m.find()){out.append(format,position,m.start());String value;char type=m.group(5).charAt(0);if(type=='f'&&(word&1)!=0)word++;long bits=argument(e,word);if(type=='f')bits|=argument(e,word+1)<<32;word+=type=='f'?2:1;
            if(type=='s')value=string(e,bits);else if(type=='d')value=Integer.toString((int)bits);else if(type=='u')value=Long.toString(bits&0xffffffffL);else if(type=='c')value=String.valueOf((char)bits);else if(type=='x'||type=='X'){value=Long.toHexString(bits&0xffffffffL);if(type=='X')value=value.toUpperCase(Locale.ROOT);}
            else{double number=Double.longBitsToDouble(bits);int precision=m.group(3)==null?6:Integer.parseInt(m.group(3));if(!Double.isFinite(number))throw new IllegalStateException("nonfinite format domain");value=new java.math.BigDecimal(Math.abs(number)).setScale(precision,java.math.RoundingMode.HALF_EVEN).toPlainString();if(bits<0)value="-"+value;else if(m.group(1).contains("+"))value="+"+value;else if(m.group(1).contains(" "))value=" "+value;}
            int width=m.group(2).isEmpty()?0:Integer.parseInt(m.group(2));boolean left=m.group(1).contains("-"),zero=m.group(1).contains("0")&&!left;
            while(value.length()<width){if(left)value+=" ";else if(zero&&value.startsWith("-"))value="-0"+value.substring(1);else value=(zero?"0":" ")+value;}
            out.append(value);position=m.end();}
        out.append(format.substring(position));return out.toString();
    }
    private void log(BackendConsoleExecution e)throws Exception{
        String fmt=string(e,e.register(10));if(fmt.contains("[ASSERT]"))return;
        if(fmt.equals("\u001b[32m%s\u001b[0m\r\n")||fmt.equals("\u001b[31m%s\u001b[0m\r\n")){String raw=registerFormat(e,fmt,11);events.add("write|"+HexFormat.of().formatHex(raw.getBytes(StandardCharsets.UTF_8)));return;}
        if(fmt.startsWith("[T>]")||fmt.startsWith("[T_]")||(fmt.startsWith("[IV]")&&fmt.contains("[SYST]"))||(fmt.contains("[SYST]")&&e.register(15)==0x4f)){e.countBoundary("scope_renderer_owned_by_System_console_replay");return;}
        String rendered=registerFormat(e,fmt,11).replaceAll("\\x1b\\[[0-9;]*m","");
        if(rendered.endsWith("\r\n"))rendered=rendered.substring(0,rendered.length()-2);
        java.util.regex.Matcher prefix=java.util.regex.Pattern.compile("^\\[([^]]+)\\]\\[[^]]+\\]\\[([^]]+)\\](?:\\[([^]]+):([0-9]+)\\])? (.*)",java.util.regex.Pattern.DOTALL).matcher(rendered);
        if(!prefix.matches())throw new IllegalStateException("Unexpected diagnostic log; private text omitted");
        String label=prefix.group(1);int level=label.equals("W_")?1:label.equals("E_")?2:label.equals("D_")?3:0;
        events.add("log|"+level+"|"+prefix.group(2)+"|"+(prefix.group(3)==null?"":prefix.group(3))+"|"+(prefix.group(4)==null?"0":prefix.group(4))+"|"+sha(prefix.group(5).getBytes(StandardCharsets.UTF_8)));
    }
    private long argument(BackendConsoleExecution e,int index)throws Exception{return index<16?e.register(index):e.read(e.register(1)+(index-16)*4,4);}
    private void invoke(BackendConsoleExecution e,long target,long... args)throws Exception{for(int i=0;i<args.length;i++)e.register(10+i,args[i]);e.invoke(target);}
    private void put(BackendConsoleExecution e,long p,String value){byte[] bytes=value.getBytes(StandardCharsets.UTF_8);e.emulator.writeMemory(e.address(p),bytes);e.write(p+bytes.length,0,1);}
    private long vaArgument(BackendConsoleExecution e,long stack,long saved,int[] index,int bytes)throws Exception{
        int aligned=(index[0]+bytes-1)&-bytes,next=aligned+bytes;long base;
        if(next<=24)base=saved;else{if(aligned<=24)next=32+bytes;base=stack;}index[0]=next;return e.read(base+next-bytes,bytes);
    }
    private String vaFormat(BackendConsoleExecution e,String format,long stack,long saved,int index)throws Exception{
        java.util.regex.Matcher m=java.util.regex.Pattern.compile("%([-+ #0]*)([0-9]*)(?:\\.([0-9]+))?([sdxcuf])").matcher(format);StringBuilder out=new StringBuilder();int position=0;int[] next={index};
        while(m.find()){out.append(format,position,m.start());String value;char type=m.group(4).charAt(0);long bits=vaArgument(e,stack,saved,next,type=='f'?8:4);
            if(type=='s')value=string(e,bits);else if(type=='d')value=Integer.toString((int)bits);else if(type=='u')value=Long.toString(bits&0xffffffffL);else if(type=='c')value=String.valueOf((char)bits);else if(type=='x')value=Long.toHexString(bits&0xffffffffL);
            else{double number=Double.longBitsToDouble(bits);int precision=m.group(3)==null?6:Integer.parseInt(m.group(3));if(!Double.isFinite(number))throw new IllegalStateException("nonfinite format domain");value=new java.math.BigDecimal(Math.abs(number)).setScale(precision,java.math.RoundingMode.HALF_EVEN).toPlainString();if(bits<0)value="-"+value;else if(m.group(1).contains("+"))value="+"+value;else if(m.group(1).contains(" "))value=" "+value;}
            int width=m.group(2).isEmpty()?0:Integer.parseInt(m.group(2));if(value.length()<width)out.append(" ".repeat(width-value.length()));out.append(value);position=m.end();}
        out.append(format.substring(position));return out.toString();
    }
    private void validateImage(Path layoutPath)throws Exception{
        JsonObject layout=JsonParser.parseString(Files.readString(layoutPath)).getAsJsonObject();byte[] original=Files.readAllBytes(Path.of(layout.get("input_path").getAsString()));if(!sha(original).equals(BackendConsoleExecution.IMAGE_SHA))throw new IllegalStateException("Original image mismatch");
        for(JsonElement value:layout.getAsJsonArray("segments")){JsonObject part=value.getAsJsonObject();int size=part.get("size").getAsInt(),offset=part.get("offset").getAsInt();if(size==0)continue;byte[] mapped=new byte[size];currentProgram.getMemory().getBytes(toAddr(part.get("address").getAsLong()),mapped);if(!Arrays.equals(mapped,Arrays.copyOfRange(original,offset,offset+size)))throw new IllegalStateException("Program bytes differ from preserved OEM image");}
    }
    private Map<String,Object> one(BackendConsoleExecution e,JsonObject c)throws Exception{
        input=c;e.resetRegisters();e.clear(OBJECT,0x40000);events.clear();wires.clear();position=requests=current=yields=0;queued=false;queuedType=0;
        for(JsonElement value:c.getAsJsonArray("wires"))wires.add(value.getAsString().getBytes(StandardCharsets.UTF_8));
        clock=c.get("clock").getAsLong();water=c.get("water").getAsLong();assertionLine=0;
        e.write(0x3ffc4654L,0x5a61c0deL,4);e.write(0x3ffc2e84L,c.get("counter").getAsLong(),4);
        e.clear(0x3ffc1018L,0x1070);e.clear(0x3ffc2088L,0xdf8);e.write(0x3ffc2ea0L,0,4);e.write(ERRNO,113,4);
        put(e,TEXT,"application/json");put(e,TEXT+0x100,"HttpBackend.h");put(e,TEXT+0x200,"0.0.119");
        e.write(OBJECT,0x3f41fa10L,4);e.write(OBJECT+4,0x3f41fa2cL,4);e.write(OBJECT+8,TEXT,4);
        e.write(OBJECT+0xc,CORE,4);e.write(OBJECT+0x10,EVENT,4);e.write(OBJECT+0x14,SENSOR,4);e.write(OBJECT+0x18,UID,4);
        invoke(e,0x400ef118L,OBJECT+0x20);invoke(e,0x400f01ccL,OBJECT+0xb0,TEXT+0x100,CORE);invoke(e,0x40111b10L,OBJECT+0x8fc);
        e.write(OBJECT+0x190c,STATE,4);e.write(OBJECT+0x1910,TEXT+0x200,4);e.write(OBJECT+0x1914,PROGRAM,4);e.write(OBJECT+0x1918,0xffffffffL,4);
        invoke(e,0x400eed28L,OBJECT+0x1928);invoke(e,0x400d0f80L,OBJECT+0x19d8);e.write(OBJECT+0x1a30,LOCATIONS,4);e.write(OBJECT+0x1a34,BOILER,4);e.write(BOILER,2,4);
        e.write(STATE+0x19c,0xffffffffL,4);e.write(STATE+0x1a0,16,4);e.write(STATE+0x1a4,0xffffffffL,4);
        invoke(e,0x400eef6cL,EVENT);e.write(EVENT+0xdec,c.get("source").getAsLong(),4);e.write(PROGRAM+4,0xffffffffL,4);invoke(e,0x400dfea0L,PROGRAM);
        e.writeDouble(SENSOR+0x250,c.get("pressure").getAsDouble());e.writeDouble(SENSOR+0x278,c.get("maximum").getAsDouble());
        invoke(e,0x400ef118L,LOCATIONS);invoke(e,0x400ef160L,LOCATIONS+0x90);
        put(e,TEXT+0x300,c.get("uid").getAsString());invoke(e,0x400ef160L,UID);invoke(e,0x400f82e8L,UID,c.get("same_uid").getAsBoolean()?0x3f418735L:TEXT+0x300);
        e.write(SOCKET,SOCKET+0xa00,4);for(long[] entry:new long[][]{{0xc,0x40030010},{0x10,0x40030020},{0x14,0x40030030},{0x1c,0x40030040},{0x24,0x40030060},{0x2c,0x40030070},{0x30,0x40030080}})e.write(SOCKET+0xa00+entry[0],entry[1],4);
        if(!events.isEmpty())throw new IllegalStateException("Unexpected observable constructor diagnostic");
        invoke(e,0x4011b6ccL,OBJECT);
        List<Long> state=List.of(e.read(0x3ffc2e84L,4),clock,e.read(OBJECT+0x1c,4),e.read(STATE+1,1),e.read(STATE+2,1),e.read(STATE+0x145,1),e.read(STATE+0x144,1),e.read(STATE+0x178,4),e.read(STATE+0x1a4,4),e.read(EVENT+4,4),e.read(EVENT+0xdec,4),e.read(EVENT+0xdd0,4),e.read(EVENT+0xdd4,4),e.read(EVENT+0xdd8,4),e.read(EVENT+0xddc,1),e.read(OBJECT+0x1928,4),e.read(OBJECT+0x197c,4),e.read(OBJECT+0x1918,4),e.read(OBJECT+0xd0,4),e.read(OBJECT+0xdc,4),(long)requests,(long)position,queued?1L:0L,queuedType);
        return row("result",true,"state",state,"identifier_sha256",sha(string(e,UID).getBytes(StandardCharsets.UTF_8)),"events",new ArrayList<>(events));
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=3)throw new IllegalArgumentException("Expected inputs, output and original layout");validateImage(Path.of(args[2]));
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();List<Object> results=new ArrayList<>();
        try(BackendConsoleExecution e=new BackendConsoleExecution(currentProgram,monitor,this::boundary)){
            for(JsonElement value:inputs){JsonObject c=value.getAsJsonObject();try{results.add(one(e,c));}catch(Exception failure){throw new IllegalStateException("Backend HTTP original case "+c.get("id").getAsString()+"; private payloads omitted",failure);}Files.writeString(Path.of(args[1]+".partial"),new Gson().toJson(results)+"\n");}
            Files.writeString(Path.of(args[1]),new GsonBuilder().setPrettyPrinting().create().toJson(row("input_sha256",BackendConsoleExecution.IMAGE_SHA,"input_file_sha256",sha(Files.readAllBytes(Path.of(args[0]))),"cases",results,"original_functions",e.functions,"modeled_boundaries",e.boundaries,"secret_payloads_hashed",true))+"\n");
        }
        println("Executed "+results.size()+" complete original HttpBackend network cases; private payloads omitted");
    }
}
