// Executes complete Runtime and PicoStill self-test bodies and returning assertions.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;

public class EmulateBackendConsoleTests extends GhidraScript {
    private long assertionLine,clock,water; private final List<Object> events=new ArrayList<>();
    private static String sha(byte[] bytes)throws Exception{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));}
    private static Map<String,Object> row(Object... values){Map<String,Object> r=new LinkedHashMap<>();for(int i=0;i<values.length;i+=2)r.put((String)values[i],values[i+1]);return r;}
    private byte[] bytes(BackendConsoleExecution e,long p,int count)throws Exception{return e.emulator.readMemory(e.address(p),count);}
    private String string(BackendConsoleExecution e,long p)throws Exception{if(p==0)throw new IllegalStateException("Null string boundary");int n=0;while(n<65536&&e.read(p+n,1)!=0)n++;if(n==65536)throw new IllegalStateException("Unbounded string");return n==0?"":new String(bytes(e,p,n),StandardCharsets.UTF_8);}
    private void boundary(BackendConsoleExecution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        if(target==0x4012d48cL){e.countBoundary("__truncdfsf2");e.register(10,Float.floatToRawIntBits((float)e.number(10)));return;}
        switch((int)target){
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
                }else{long v=argument(e,argument++);if(ch=='d')replacement=Integer.toString((int)v);else if(ch=='u')replacement=Long.toString(v);else if(ch=='s')replacement=string(e,v);else if(ch=='c')replacement=String.valueOf((char)(v&255));else if(ch=='x'||ch=='X'){replacement=Long.toHexString(v);if(ch=='X')replacement=replacement.toUpperCase(Locale.ROOT);}else throw new IllegalStateException("Unreviewed format "+format);}
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
        if(!prefix.matches())throw new IllegalStateException("Unexpected diagnostic log "+fmt);
        String label=prefix.group(1);int level=label.equals("W_")?1:label.equals("E_")?2:label.equals("D_")?3:0;
        events.add("log|"+level+"|"+prefix.group(2)+"|"+(prefix.group(3)==null?"":prefix.group(3))+"|"+(prefix.group(4)==null?"0":prefix.group(4))+"|"+prefix.group(5));
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
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();validateImage(Path.of(args[2]));JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();List<Object> results=new ArrayList<>();
        try(BackendConsoleExecution e=new BackendConsoleExecution(currentProgram,monitor,this::boundary)){
            for(JsonElement value:inputs){JsonObject c=value.getAsJsonObject();e.resetRegisters();assertionLine=0;events.clear();clock=c.get("clock").getAsLong();water=c.get("water").getAsLong();e.write(0x3ffc4654L,0x5a61c0deL,4);e.write(0x3ffc2e84L,c.get("counter").getAsLong(),4);
                e.clear(0x3ffc1018L,0x1070);e.write(0x3ffc2ea0L,0,4);e.clear(0x3ffc2088L,0xdf8);long[] functions={0x401188f0L,0x4011dc20L};int operation=c.get("operation").getAsInt();if(operation<2)invoke(e,functions[operation]);else if(operation==2){
                    String[] categories={"Basic Tests","Network Tests","Int Tests","All Tests",""};put(e,0x3ff3f000L,categories[c.get("category").getAsInt()]);invoke(e,0x400ee670L,0x3ff3e000L,0x3ff40000L,0x3ff3f000L);
                    clock=(clock+c.get("elapsed").getAsLong())&0xffffffffL;e.write(0x3ffc2e84L,(e.read(0x3ffc2e84L,4)+c.get("failed").getAsLong())&0xffffffffL,4);invoke(e,0x4010a26cL,0x3ff3e000L);
                }else{
                    e.clear(0x3ff30000L,0x2000);long argument=c.get("argument").getAsLong();
                    if(operation==3){e.write(0x3ff31010L,argument,4);invoke(e,0x4011da68L,0x3ff30000L,0x3ff31000L);}
                    else if(operation==4){long second=c.get("second").getAsLong();for(int j=0;j<3;j++)e.write(0x3ff31000L+j,(second>>>(j*8))&255,1);for(int j=0;j<4;j++)e.write(0x3ff31003L+j,(argument>>>j)&1,1);invoke(e,0x4011d974L,0x3ff30000L,0x3ff31000L);}
                    else throw new IllegalArgumentException("Unknown operation");
                }
                List<Long> state=new ArrayList<>(Arrays.asList(e.read(0x3ffc2e84L,4),clock));if(operation>=3)state.add(e.read(0x3ff30000L,4));results.add(row("state",state,"events",new ArrayList<>(events)));Files.writeString(Path.of(args[1]+".partial"),new GsonBuilder().setPrettyPrinting().create().toJson(results));
            }
            Files.writeString(Path.of(args[1]),new GsonBuilder().setPrettyPrinting().create().toJson(row("input_sha256",BackendConsoleExecution.IMAGE_SHA,"cases",results,"original_functions",e.functions,"modeled_boundaries",e.boundaries))+"\n");
        }
        println("Executed "+results.size()+" original backend diagnostic callbacks");
    }
}
