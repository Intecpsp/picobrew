// Executes complete MAX11613 information/read/detect callbacks and returning assertions.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;

public class EmulateAdcConsole extends GhidraScript {
    private static final long ADC=0x3ff10000L, WIRE=ADC+0x1000, VTABLE=WIRE+0x1000, READ=0x3ffe0300L, CALLBACK=ADC+0x4000;
    private JsonArray reply; private int readPosition; private long returned;
    private long assertionLine,clock,water; private final List<Object> events=new ArrayList<>();
    private static String sha(byte[] bytes)throws Exception{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));}
    private static Map<String,Object> row(Object... values){Map<String,Object> r=new LinkedHashMap<>();for(int i=0;i<values.length;i+=2)r.put((String)values[i],values[i+1]);return r;}
    private byte[] bytes(SlideshowExecution e,long p,int count)throws Exception{return e.emulator.readMemory(e.address(p),count);}
    private String string(SlideshowExecution e,long p)throws Exception{if(p==0)throw new IllegalStateException("Null string boundary");int n=0;while(n<65536&&e.read(p+n,1)!=0)n++;if(n==65536)throw new IllegalStateException("Unbounded string");return n==0?"":new String(bytes(e,p,n),StandardCharsets.UTF_8);}
    private void boundary(SlideshowExecution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        if(target==0x4012d48cL){e.countBoundary("__truncdfsf2");e.register(10,Float.floatToRawIntBits((float)e.number(10)));return;}
        switch((int)target){
        case 0x40125ab4:e.countBoundary("Wire_requestFrom");events.add("request|"+e.register(11)+"|"+e.register(12)+"|1");e.register(10,returned);return;
        case (int)READ:e.countBoundary("Wire_read");if(readPosition>=reply.size())throw new IllegalStateException("Unexpected read");int next=reply.get(readPosition++).getAsInt();events.add("read|"+next);e.register(10,next);return;
        case 0x4012d42c:{double v=e.number(10);if(!Double.isFinite(v)||v<0||v>=4294967296.0)throw new IllegalStateException("Unsigned conversion domain");e.register(10,(long)v);e.countBoundary("finite_uint32_conversion");return;}
        case 0x4013b1d4:{double v=e.number(10);if(!Double.isFinite(v)||Math.abs(v)>1000000)throw new IllegalStateException("round domain");e.number(10,Math.copySign(Math.floor(Math.abs(v)+0.5),v));e.countBoundary("finite_round_half_away_from_zero");return;}
        case 0x4013b288:e.countBoundary("finite_positive_log");if(e.number(10)<=0)throw new IllegalStateException("log domain");e.number(10,Math.log(e.number(10)));return;
        case 0x4013b394:e.countBoundary("finite_positive_pow");if(e.number(10)<=0)throw new IllegalStateException("pow domain");e.number(10,Math.pow(e.number(10),e.number(12)));return;
        case 0x4013b678:e.countBoundary("finite_nonnegative_sqrt");if(e.number(10)<0)throw new IllegalStateException("sqrt domain");e.number(10,Math.sqrt(e.number(10)));return;
        case 0x400013ac:{String v=string(e,e.register(11));put(e,e.register(10),v);e.countBoundary("ROM_strcpy");return;}
        case 0x40081388:e.countBoundary("prescribed_millis");e.register(10,clock);return;
        case 0x4008c2b4:{e.countBoundary("diagnostic_basename");String value=string(e,e.register(10));e.register(10,e.register(10)+Math.max(value.lastIndexOf('/'),value.lastIndexOf('\\'))+1);return;}
        case 0x400d21cc:e.countBoundary("diagnostic_output");log(e);return;
        case 0x400db014:assertionLine=e.register(11);events.add("assertion|"+string(e,e.register(10))+"|"+e.register(11)+"|"+string(e,e.register(12)));e.invoke(target);return;
        case 0x401289a0:if(e.register(10)!=200)throw new IllegalStateException("Unexpected assertion delay");e.countBoundary("assertion_delay_200ms");clock=(clock+200)&0xffffffffL;return;
        case 0x400d56c4:{events.add("scope|1|"+string(e,e.register(11))+"|"+basename(string(e,e.register(13)))+"|"+e.register(14)+"|"+string(e,e.register(12)));e.invoke(target);return;}
        case 0x400db438:{long p=e.register(10);events.add("scope|0|"+string(e,e.read(p,4))+"|"+basename(string(e,e.read(p+8,4)))+"|"+e.read(p+12,4)+"|"+string(e,e.read(p+4,4)));e.invoke(target);return;}
        case 0x40090e2c:e.countBoundary("prescribed_stack_high_water");e.register(10,water);return;
        case 0x40136e8c:e.countBoundary("esp_task_wdt_reset");e.register(10,0);return;
        case 0x401367a0:throw new IllegalStateException("Original stack guard failed");
        case 0x400015d4:{int n=(int)e.register(12);if(n<0||n>65536)throw new IllegalStateException("strncpy bound");byte[] out=new byte[n];boolean ended=false;for(int i=0;i<n;i++){if(!ended){out[i]=(byte)e.read(e.register(11)+i,1);if(out[i]==0)ended=true;}}if(n>0)e.emulator.writeMemory(e.address(e.register(10)),out);e.countBoundary("ROM_strncpy");return;}
        case 0x4000c2c8:{int n=(int)e.register(12);if(n<0||n>65536)throw new IllegalStateException("memcpy bound");if(n>0)e.emulator.writeMemory(e.address(e.register(10)),bytes(e,e.register(11),n));e.countBoundary("ROM_memcpy");return;}
        case 0x4000c44c:{int n=(int)e.register(12);if(n<0||n>65536)throw new IllegalStateException("memset bound");byte[] bytes=new byte[n];Arrays.fill(bytes,(byte)e.register(11));if(n>0)e.emulator.writeMemory(e.address(e.register(10)),bytes);e.countBoundary("ROM_memset");return;}
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
            if((target>=0x400d0000L&&target<0x40110000L)||(target>=0x401bf000L&&target<0x401c0200L)){e.invoke(target);return;}
            throw new IllegalStateException(String.format("Unreviewed diagnostic dependency %08x",target));
        }
    }
    private String basename(String value){return value.substring(Math.max(value.lastIndexOf('/'),value.lastIndexOf('\\'))+1);}
    private String stackFormat(SlideshowExecution e,String format)throws Exception{
        java.util.regex.Matcher m=java.util.regex.Pattern.compile("%([-+ #0]*)([0-9]*)(?:\\.([0-9]+))?(l?)([sdxcuf])").matcher(format);StringBuilder out=new StringBuilder();int position=0,word=0;
        while(m.find()){out.append(format,position,m.start());String value;char type=m.group(5).charAt(0);if(type=='f'&&(word&1)!=0)word++;long bits=e.read(e.register(1)+word*4,type=='f'?8:4);word+=type=='f'?2:1;
            if(type=='s')value=string(e,bits);else if(type=='d')value=Integer.toString((int)bits);else if(type=='u')value=Long.toString(bits&0xffffffffL);else if(type=='c')value=String.valueOf((char)bits);else if(type=='x')value=Long.toHexString(bits&0xffffffffL);
            else{double number=Double.longBitsToDouble(bits);int precision=m.group(3)==null?6:Integer.parseInt(m.group(3));if(!Double.isFinite(number))throw new IllegalStateException("nonfinite format domain");value=new java.math.BigDecimal(Math.abs(number)).setScale(precision,java.math.RoundingMode.HALF_EVEN).toPlainString();if(bits<0)value="-"+value;else if(m.group(1).contains("+"))value="+"+value;else if(m.group(1).contains(" "))value=" "+value;}
            int width=m.group(2).isEmpty()?0:Integer.parseInt(m.group(2));if(value.length()<width&&!m.group(1).contains("-"))out.append(" ".repeat(width-value.length()));out.append(value);if(value.length()<width&&m.group(1).contains("-"))out.append(" ".repeat(width-value.length()));position=m.end();}
        out.append(format.substring(position));return out.toString();
    }
    private void log(SlideshowExecution e)throws Exception{
        String fmt=string(e,e.register(10));if(fmt.contains("[ASSERT]"))return;
        if(fmt.startsWith("[IV]")&&fmt.contains("IsReading")){events.add("log|0|MAX11613||0|IsReading = "+string(e,e.register(14)));return;}
        if(fmt.startsWith("[T>]")||fmt.startsWith("[T_]")||fmt.startsWith("[IV]")||(fmt.contains("[SYST]")&&e.register(15)==0x4f)){e.countBoundary("scope_renderer_owned_by_System_console_replay");return;}
        int marker=fmt.indexOf("[%s:%u] ");if(marker<0)throw new IllegalStateException("Unexpected diagnostic log "+fmt);
        String prefix=fmt.substring(0,marker),body=fmt.substring(marker+8).replace("\u001b[0m","");if(body.endsWith("\r\n"))body=body.substring(0,body.length()-2);
        java.util.regex.Matcher tag=java.util.regex.Pattern.compile("\\]\\[([A-Z0-9]+)\\]").matcher(prefix);if(!tag.find())throw new IllegalStateException("Missing tag");
        int level=fmt.contains("[W_]")?1:fmt.contains("[E_]")?2:fmt.contains("[D_]")?3:0;
        events.add("log|"+level+"|"+tag.group(1)+"|"+string(e,e.register(14))+"|"+e.register(15)+"|"+stackFormat(e,body));
    }
    private long argument(SlideshowExecution e,int index)throws Exception{return index<16?e.register(index):e.read(e.register(1)+(index-16)*4,4);}
    private void invoke(SlideshowExecution e,long target,long... args)throws Exception{for(int i=0;i<args.length;i++)e.register(10+i,args[i]);e.invoke(target);}
    private void put(SlideshowExecution e,long p,String value){byte[] bytes=value.getBytes(StandardCharsets.UTF_8);e.emulator.writeMemory(e.address(p),bytes);e.write(p+bytes.length,0,1);}
    private long vaArgument(SlideshowExecution e,long stack,long saved,int[] index,int bytes)throws Exception{
        int aligned=(index[0]+bytes-1)&-bytes,next=aligned+bytes;long base;
        if(next<=24)base=saved;else{if(aligned<=24)next=32+bytes;base=stack;}index[0]=next;return e.read(base+next-bytes,bytes);
    }
    private String vaFormat(SlideshowExecution e,String format,long stack,long saved,int index)throws Exception{
        java.util.regex.Matcher m=java.util.regex.Pattern.compile("%([-+ #0]*)([0-9]*)(?:\\.([0-9]+))?([sdxcuf])").matcher(format);StringBuilder out=new StringBuilder();int position=0;int[] next={index};
        while(m.find()){out.append(format,position,m.start());String value;char type=m.group(4).charAt(0);long bits=vaArgument(e,stack,saved,next,type=='f'?8:4);
            if(type=='s')value=string(e,bits);else if(type=='d')value=Integer.toString((int)bits);else if(type=='u')value=Long.toString(bits&0xffffffffL);else if(type=='c')value=String.valueOf((char)bits);else if(type=='x')value=Long.toHexString(bits&0xffffffffL);
            else{double number=Double.longBitsToDouble(bits);int precision=m.group(3)==null?6:Integer.parseInt(m.group(3));if(!Double.isFinite(number))throw new IllegalStateException("nonfinite format domain");value=new java.math.BigDecimal(Math.abs(number)).setScale(precision,java.math.RoundingMode.HALF_EVEN).toPlainString();if(bits<0)value="-"+value;else if(m.group(1).contains("+"))value="+"+value;else if(m.group(1).contains(" "))value=" "+value;}
            int width=m.group(2).isEmpty()?0:Integer.parseInt(m.group(2));if(value.length()<width&&!m.group(1).contains("-"))out.append(" ".repeat(width-value.length()));out.append(value);if(value.length()<width&&m.group(1).contains("-"))out.append(" ".repeat(width-value.length()));position=m.end();}
        out.append(format.substring(position));return out.toString();
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();List<Object> results=new ArrayList<>();
        try(SlideshowExecution e=new SlideshowExecution(currentProgram,monitor,this::boundary)){
            for(JsonElement value:inputs){JsonObject c=value.getAsJsonObject();e.resetRegisters();e.clear(ADC,0x5000);assertionLine=0;events.clear();
                clock=c.get("clock").getAsLong();water=1024;returned=c.get("returned").getAsLong();reply=c.getAsJsonArray("bytes");readPosition=0;
                e.write(0x3ffc4654L,0x5a61c0deL,4);e.write(0x3ffc2e84L,c.get("counter").getAsLong(),4);
                e.write(ADC,WIRE,4);e.write(ADC+4,52,1);e.write(ADC+12,c.get("enabled").getAsBoolean()?1:0,1);e.write(WIRE,VTABLE,4);e.write(VTABLE+20,READ,4);e.write(CALLBACK,ADC,4);
                for(int i=0;i<4;i++){e.write(ADC+16+i*4,c.getAsJsonArray("types").get(i).getAsLong(),4);e.write(ADC+32+i*4,c.getAsJsonArray("raw").get(i).getAsLong(),4);}
                int op=c.get("operation").getAsInt();invoke(e,op==0?0x400dd8f4L:op==1?0x400dd9a4L:0x400ddeb8L,CALLBACK);
                List<Object> state=new ArrayList<>();state.add(e.read(0x3ffc2e84L,4));state.add(clock);for(int i=0;i<4;i++)state.add((int)e.read(ADC+32+i*4,4));
                results.add(row("state",state,"events",new ArrayList<>(events)));
            }
            List<Object> functions=new ArrayList<>();for(String address:e.functions.keySet()){
                Function f=getFunctionAt(toAddr(Long.parseLong(address.substring(2),16)));if(f==null)f=createFunction(toAddr(Long.parseLong(address.substring(2),16)),null);if(f==null)throw new IllegalStateException("Missing function "+address);
                List<Object> ranges=new ArrayList<>();for(AddressRange range:f.getBody().getAddressRanges()){
                    byte[] raw=new byte[(int)range.getLength()];currentProgram.getMemory().getBytes(range.getMinAddress(),raw);
                    ranges.add(row("address",range.getMinAddress().getOffset(),"size",raw.length,"sha256",sha(raw)));}
                functions.add(row("address",address,"calls",e.functions.get(address),"ranges",ranges));
            }
            Files.writeString(Path.of(args[1]),new GsonBuilder().setPrettyPrinting().create().toJson(row("input_sha256",SlideshowExecution.IMAGE_SHA,
                "input_file_sha256",sha(Files.readAllBytes(Path.of(args[0]))),"cases",results,"original_functions",functions,
                "original_functions_executed",e.functions,"modeled_boundaries",e.boundaries))+"\n");
        }
        println("Executed "+results.size()+" original MAX11613 console callbacks");
    }
}
