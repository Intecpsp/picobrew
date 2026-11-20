// Executes widget bodies through the original text/bitmap/rectangle primitives.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;

public class EmulateHeadSlideshow extends GhidraScript {
    private static final long W=0x3ff20000L,D=0x3ff50000L,R=0x3ff55000L,T=0x3ff60000L,L=0x3ff70000L;
    private static final long[] FONTS={0x3f42b31cL,0x3f42ded4L,0x3f437478L,0x3f43d718L,0x3f440f88L};
    private long assertionLine,clock; private final List<Object> events=new ArrayList<>();
    private static String sha(byte[] bytes)throws Exception{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));}
    private static Map<String,Object> row(Object... values){Map<String,Object> r=new LinkedHashMap<>();for(int i=0;i<values.length;i+=2)r.put((String)values[i],values[i+1]);return r;}
    private byte[] bytes(SlideshowExecution e,long p,int count)throws Exception{return e.emulator.readMemory(e.address(p),count);}
    private String string(SlideshowExecution e,long p)throws Exception{if(p==0)throw new IllegalStateException("Null string boundary");int n=0;while(n<65536&&e.read(p+n,1)!=0)n++;if(n==65536)throw new IllegalStateException("Unbounded string");return n==0?"":new String(bytes(e,p,n),StandardCharsets.UTF_8);}
    private void boundary(SlideshowExecution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        if(target==0x4012d48cL){e.countBoundary("__truncdfsf2");e.register(10,Float.floatToRawIntBits((float)e.number(10)));return;}
        switch((int)target){
        case 0x4012d42c:{double v=e.number(10);if(!Double.isFinite(v)||v<0||v>=4294967296.0)throw new IllegalStateException("Unsigned conversion domain");e.register(10,(long)v);e.countBoundary("finite_uint32_conversion");return;}
        case 0x4013b1d4:{double v=e.number(10);if(!Double.isFinite(v)||Math.abs(v)>1000000)throw new IllegalStateException("round domain");e.number(10,Math.copySign(Math.floor(Math.abs(v)+0.5),v));e.countBoundary("finite_round_half_away_from_zero");return;}
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
        case 0x40090e2c:e.countBoundary("prescribed_stack_high_water_1024");e.register(10,1024);return;
        case 0x40136e8c:e.countBoundary("esp_task_wdt_reset");e.register(10,0);return;
        case 0x400ef2a4:e.countBoundary("HeadScreens_reset_owned_by_independent_Head_replay");invoke(e,0x400d1dccL,W);return;
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
            // Application graphics helpers execute their actual instructions;
            // no draw calls or rectangle/text/bitmap bodies are substituted.
            if((target>=0x400d0000L&&target<0x40110000L)||(target>=0x401bf000L&&target<0x401c0200L)){e.invoke(target);return;}
            throw new IllegalStateException(String.format("Unreviewed widget dependency %08x",target));
        }
    }
    private String basename(String value){return value.substring(Math.max(value.lastIndexOf('/'),value.lastIndexOf('\\'))+1);}
    private String stackFormat(SlideshowExecution e,String format)throws Exception{
        java.util.regex.Matcher m=java.util.regex.Pattern.compile("%([-+ #0]*)([0-9]*)(?:\\.([0-9]+))?(l?)([sdxcuf])").matcher(format);StringBuilder out=new StringBuilder();int position=0,word=0;
        while(m.find()){out.append(format,position,m.start());String value;char type=m.group(5).charAt(0);if(type=='f'&&(word&1)!=0)word++;long bits=e.read(e.register(1)+word*4,type=='f'?8:4);word+=type=='f'?2:1;
            if(type=='s')value=string(e,bits);else if(type=='d')value=Integer.toString((int)bits);else if(type=='u')value=Long.toString(bits&0xffffffffL);else if(type=='c')value=String.valueOf((char)bits);else if(type=='x')value=Long.toHexString(bits&0xffffffffL);
            else{double number=Double.longBitsToDouble(bits);int precision=m.group(3)==null?6:Integer.parseInt(m.group(3));if(!Double.isFinite(number))throw new IllegalStateException("nonfinite format domain");value=new java.math.BigDecimal(Math.abs(number)).setScale(precision,java.math.RoundingMode.HALF_EVEN).toPlainString();if(bits<0)value="-"+value;else if(m.group(1).contains("+"))value="+"+value;else if(m.group(1).contains(" "))value=" "+value;}
            int width=m.group(2).isEmpty()?0:Integer.parseInt(m.group(2));if(value.length()<width)out.append(" ".repeat(width-value.length()));out.append(value);position=m.end();}
        out.append(format.substring(position));return out.toString();
    }
    private void log(SlideshowExecution e)throws Exception{
        String fmt=string(e,e.register(10));if(fmt.contains("[ASSERT]"))return;
        if(fmt.startsWith("[T>]")||fmt.startsWith("[T_]")||fmt.startsWith("[IV]")||(fmt.contains("[SYST]")&&e.register(15)==0x4f)){e.countBoundary("scope_renderer_owned_by_System_console_replay");return;}
        int marker=fmt.indexOf("[%s:%u] ");if(marker<0)throw new IllegalStateException("Unexpected slideshow log "+fmt);
        String prefix=fmt.substring(0,marker),body=fmt.substring(marker+8).replace("\u001b[0m","");if(body.endsWith("\r\n"))body=body.substring(0,body.length()-2);
        java.util.regex.Matcher tag=java.util.regex.Pattern.compile("\\]\\[([A-Z]+)\\]").matcher(prefix);if(!tag.find())throw new IllegalStateException("Missing tag");
        int level=fmt.contains("[W_]")?1:fmt.contains("[E_]")?2:fmt.contains("[D_]")?3:0;
        events.add("log|"+level+"|"+tag.group(1)+"|"+string(e,e.register(14))+"|"+e.register(15)+"|"+stackFormat(e,body));
    }
    private long argument(SlideshowExecution e,int index)throws Exception{return index<16?e.register(index):e.read(e.register(1)+(index-16)*4,4);}
    private void invoke(SlideshowExecution e,long target,long... args)throws Exception{for(int i=0;i<args.length;i++)e.register(10+i,args[i]);e.invoke(target);}
    private void put(SlideshowExecution e,long p,String value){byte[] bytes=value.getBytes(StandardCharsets.UTF_8);e.emulator.writeMemory(e.address(p),bytes);e.write(p+bytes.length,0,1);}
    private void rect(SlideshowExecution e,long p,JsonArray rect){for(int i=0;i<4;i++)e.write(p+i*4,rect.get(i).getAsLong(),4);}
    private void font(SlideshowExecution e,long p,int index)throws Exception{for(int i=0;i<3;i++)e.write(p+i*4,e.read(FONTS[index]+i*4,4),4);}
    private long vaArgument(SlideshowExecution e,long stack,long saved,int[] index,int bytes)throws Exception{
        int aligned=(index[0]+bytes-1)&-bytes,next=aligned+bytes;long base;
        if(next<=24)base=saved;else{if(aligned<=24)next=32+bytes;base=stack;}index[0]=next;return e.read(base+next-bytes,bytes);
    }
    private String vaFormat(SlideshowExecution e,String format,long stack,long saved,int index)throws Exception{
        java.util.regex.Matcher m=java.util.regex.Pattern.compile("%([-+ #0]*)([0-9]*)(?:\\.([0-9]+))?([sdxcuf])").matcher(format);StringBuilder out=new StringBuilder();int position=0;int[] next={index};
        while(m.find()){out.append(format,position,m.start());String value;char type=m.group(4).charAt(0);long bits=vaArgument(e,stack,saved,next,type=='f'?8:4);
            if(type=='s')value=string(e,bits);else if(type=='d')value=Integer.toString((int)bits);else if(type=='u')value=Long.toString(bits&0xffffffffL);else if(type=='c')value=String.valueOf((char)bits);else if(type=='x')value=Long.toHexString(bits&0xffffffffL);
            else{double number=Double.longBitsToDouble(bits);int precision=m.group(3)==null?6:Integer.parseInt(m.group(3));if(!Double.isFinite(number))throw new IllegalStateException("nonfinite format domain");value=new java.math.BigDecimal(Math.abs(number)).setScale(precision,java.math.RoundingMode.HALF_EVEN).toPlainString();if(bits<0)value="-"+value;else if(m.group(1).contains("+"))value="+"+value;else if(m.group(1).contains(" "))value=" "+value;}
            int width=m.group(2).isEmpty()?0:Integer.parseInt(m.group(2));if(value.length()<width)out.append(" ".repeat(width-value.length()));out.append(value);position=m.end();}
        out.append(format.substring(position));return out.toString();
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();JsonObject fields=JsonParser.parseString(Files.readString(Path.of(args[1]))).getAsJsonObject();
        List<Object> results=new ArrayList<>();int caseIndex=0;
        try(SlideshowExecution e=new SlideshowExecution(currentProgram,monitor,this::boundary)){
            for(JsonElement value:inputs){JsonObject input=value.getAsJsonObject();int operation=input.get("operation").getAsInt();
                e.resetRegisters();e.clear(W-0x6000,0xe000);e.clear(D,0x2100);e.clear(R,0x1000);e.clear(T,0x4000);e.clear(L,0x5000);assertionLine=0;events.clear();clock=input.get("clock").getAsLong();e.write(0x3ffc4654L,0x5a61c0deL,4);e.write(0x3ffc2e84L,7,4);
                byte[] pixels=new byte[4096];for(int i=0;i<4096;i++)pixels[i]=(byte)((i*73+17)^(i>>>3));e.emulator.writeMemory(e.address(D+11),pixels);e.write(D+0x2068,64,4);e.write(D+0x201c,0,4);e.write(D+0x2020,0,4);e.write(D+0x2024,127,4);e.write(D+0x2028,63,4);invoke(e,0x400d1ae4L,D+0x200c,FONTS[4],0);
                long core=L,settings=L+0x3000;e.write(settings+16,input.get("language").getAsLong(),4);e.write(T+0x100,settings+16,4);
                for(JsonElement field:fields.getAsJsonArray("words")){JsonArray f=field.getAsJsonArray();e.write(core+f.get(1).getAsLong(),input.get(f.get(0).getAsString()).getAsLong(),f.get(4).getAsInt());}
                for(JsonElement field:fields.getAsJsonArray("doubles")){JsonArray f=field.getAsJsonArray();e.writeDouble(core+f.get(1).getAsLong(),input.get(f.get(0).getAsString()).getAsDouble());}
                for(JsonElement field:fields.getAsJsonArray("floats")){JsonArray f=field.getAsJsonArray();e.write(core+f.get(1).getAsLong(),Float.floatToRawIntBits(input.get(f.get(0).getAsString()).getAsFloat()),4);}
                put(e,core+0x24,input.get("name").getAsString());put(e,T+0x400,input.get("step").getAsString());put(e,T+0x500,input.get("substep").getAsString());e.write(core+0x304,T+0x400,4);e.write(core+0x35c,T+0x500,4);
                try{
                    if(operation==0){invoke(e,0x400f347cL,W,D,core,T+0x100);e.write(W+0x1c,2,4);e.write(W+0x20,15,4);e.write(W+0x24,125,4);e.write(W+0x28,61,4);e.write(W+0x38,input.get("type").getAsLong(),4);e.write(W+0x60,input.get("counter").getAsLong(),4);invoke(e,0x400fd970L,W,input.get("imperial").getAsBoolean()?1:0,clock);}
                    else if(operation==4)invoke(e,0x400fe0b8L);
                    else{
                        e.write(W+4,D,4);e.write(W+8,core,4);e.write(W+0x14,127,4);e.write(W+0x18,63,4);e.write(W+0x20,13,4);e.write(W+0x24,127,4);e.write(W+0x28,63,4);e.write(W+0x10b8,0xffffffffL,4);
                        e.write(W+0xf98,input.get("mode").getAsLong(),4);e.write(W+0x10c4,input.get("imperial").getAsBoolean()?1:0,1);e.write(W+0x10c5,input.get("detail").getAsBoolean()?1:0,1);e.write(W+0x10c6,input.get("static").getAsBoolean()?1:0,1);
                        for(int i=0;i<12;i++){long p=W+0x38+i*0x148;invoke(e,0x400f347cL,p,D,core,T+0x100);e.write(p+0x1c,2,4);e.write(p+0x20,15,4);e.write(p+0x24,125,4);e.write(p+0x28,61,4);}
                        if(operation==3){e.write(R,W-0x4970,4);e.write(W-0x10c0,D,4);invoke(e,0x400fea6cL,R);}
                        else{invoke(e,0x401bf89cL,W);e.write(W+0x10c0,1,1);invoke(e,0x400fe6bcL,W,input.get("mode").getAsLong());invoke(e,0x400fdb54L,W,0);if(!input.get("static").getAsBoolean()){e.write(W+0x10cc,1,1);e.write(W+0x10d0,4000,4);invoke(e,0x400d1cbcL,W+0x10cc);}else{e.write(W+0x10cc,0,1);e.write(W+0x10d4,0,4);}}
                        if(input.get("select").getAsLong()!=0)invoke(e,0x400fdb54L,W,input.get("select").getAsLong());
                        int change=input.get("change").getAsInt();if((change&1)!=0)e.write(core+0x361,e.read(core+0x361,1)==0?1:0,1);if((change&2)!=0)e.write(core+0x980,e.read(core+0x980,1)==0?1:0,1);if((change&4)!=0)e.write(core+0x7e0,e.read(core+0x7e0,4)==0?2:0,4);
                        for(String key:List.of("tick","tick2"))if(input.get(key).getAsLong()!=0){clock=input.get(key).getAsLong();invoke(e,0x400fe960L,W,clock);}
                        if(operation==2)invoke(e,0x400d1dccL,W);
                    }
                }catch(Exception ex){throw new IllegalStateException("Case "+caseIndex+" failed: "+input,ex);}
                Map<String,Object> result=row("pixels_sha256",sha(bytes(e,D+11,4096)),"state",Arrays.asList(e.read(D+0x205c,1),assertionLine,e.read(0x3ffc2e84L,4),clock),"events",new ArrayList<>(events));
                if(operation!=0&&operation!=4){result.put("slideshow",Arrays.asList(e.read(W+0x10b8,4),e.read(W+0x10bc,4),e.read(W+0x10c0,1),e.read(W+0x10cc,1),e.read(W+0x10d0,4),e.read(W+0x10d4,4)));List<Object> entries=new ArrayList<>();for(int i=0;i<12;i++){long p=W+0x38+i*0x148;entries.add(Arrays.asList(e.read(p+0x38,4),e.read(p+0x60,4),e.read(p+0x144,4),e.read(p+0x68,1),e.read(p+0x138,1),e.read(p+0x13c,4),e.read(p+0x140,4),string(e,p+0x3c)));}result.put("entries",entries);}
                results.add(result);caseIndex++;Files.writeString(Path.of(args[2]+".partial"),new Gson().toJson(result)+"\n",StandardOpenOption.CREATE,StandardOpenOption.APPEND);
            }
            Files.writeString(Path.of(args[2]),new GsonBuilder().setPrettyPrinting().create().toJson(row("input_sha256",SlideshowExecution.IMAGE_SHA,"cases",results,"original_functions",e.functions,"modeled_boundaries",e.boundaries))+"\n");
        }
        println("Executed "+results.size()+" original Slideshow framebuffer cases");
    }
}
