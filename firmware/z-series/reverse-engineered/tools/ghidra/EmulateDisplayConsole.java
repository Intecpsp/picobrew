// Execute complete original Display console/demo bodies and every frame transfer.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;
import java.io.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.listing.Function;

public class EmulateDisplayConsole extends GhidraScript {
    private static final long D=0x3ff50000L,A=0x3ff55000L,T=0x3ff60000L,SPI=0x7ff10000L;
    private static final long[] FONTS={0x3f42b31cL,0x3f42ded4L,0x3f437478L,0x3f43d718L,0x3f440f88L};
    private final ByteArrayOutputStream events=new ByteArrayOutputStream();
    private final List<String> frames=new ArrayList<>();
    private boolean asserting;
    private static String sha(byte[] data)throws Exception{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(data));}
    private static Map<String,Object> row(Object... values){Map<String,Object> out=new LinkedHashMap<>();for(int i=0;i<values.length;i+=2)out.put((String)values[i],values[i+1]);return out;}
    private byte[] bytes(DisplayConsoleExecution e,long p,int n){return e.emulator.readMemory(e.address(p),n);}
    private String string(DisplayConsoleExecution e,long p)throws Exception{if(p==0)return "";int n=0;while(n<65536&&e.read(p+n,1)!=0)n++;if(n==65536)throw new IllegalStateException("Unbounded text");return new String(bytes(e,p,n),StandardCharsets.UTF_8);}
    private String basename(String s){return s.substring(Math.max(s.lastIndexOf('/'),s.lastIndexOf('\\'))+1);}
    private void word(long value){for(int i=0;i<4;i++)events.write((int)(value>>>(8*i))&255);}
    private void text(String value)throws Exception{events.write(value.getBytes(StandardCharsets.UTF_8));events.write(0);}
    private long argument(DisplayConsoleExecution e,int i)throws Exception{return i<16?e.register(i):e.read(e.register(1)+(i-16)*4,4);}
    private String format(DisplayConsoleExecution e,String value,int arg)throws Exception{
        StringBuilder out=new StringBuilder();
        for(int i=0;i<value.length();i++){
            char ch=value.charAt(i);if(ch!='%'){out.append(ch);continue;}ch=value.charAt(++i);if(ch=='%'){out.append('%');continue;}
            boolean left=false,zero=false;while(ch=='-'||ch=='0'){left|=ch=='-';zero|=ch=='0';ch=value.charAt(++i);}
            int width=0;while(ch>='0'&&ch<='9'){width=width*10+ch-'0';ch=value.charAt(++i);}
            if(ch=='l')ch=value.charAt(++i);long v=argument(e,arg++);String replacement;
            if(ch=='s')replacement=string(e,v);else if(ch=='d')replacement=Integer.toString((int)v);else if(ch=='u')replacement=Long.toString(v);else if(ch=='x')replacement=Long.toHexString(v);else if(ch=='c')replacement=String.valueOf((char)(v&255));else throw new IllegalStateException("Unreviewed format "+value);
            while(replacement.length()<width)replacement=left?replacement+" ":(zero?"0":" ")+replacement;out.append(replacement);
        }
        return out.toString();
    }
    private void log(String tag,String file,long line,String message)throws Exception{events.write(6);events.write(0);text(tag);text(file);word(line);text(message);}
    private void boundary(DisplayConsoleExecution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        switch((int)target){
        case 0x40080a9c:e.countBoundary("GPIO_digitalWrite");events.write(1);events.write((int)e.register(10));events.write((int)e.register(11));return;
        case 0x401257c8:if(e.register(10)!=SPI)throw new IllegalStateException("SPI owner");e.countBoundary("SPI_transfer");events.write(2);events.write((int)e.register(11));e.register(10,0);return;
        case 0x401257e4:{if(e.register(10)!=SPI||e.register(12)!=0||e.register(13)!=4096)throw new IllegalStateException("SPI bulk contract");byte[] data=bytes(e,e.register(11),4096);e.countBoundary("SPI_transferBytes");events.write(3);word(4096);events.write(data);frames.add(sha(data));return;}
        case 0x401289a0:e.countBoundary("SDK_delay");events.write(4);word(e.register(10));return;
        case 0x40081388:e.countBoundary("prescribed_millis");e.register(10,777);return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400db014:{events.write(7);text(basename(string(e,e.register(10))));word(e.register(11));text(string(e,e.register(12)));word(e.read(0x3ffc2e84L,4));boolean previous=asserting;asserting=true;e.invoke(target);asserting=previous;return;}
        case 0x400d21cc:{
            e.countBoundary("diagnostic_printf");if(asserting)return;String fmt=string(e,e.register(10));
            if(fmt.startsWith("[IV][%8.2f][DSP] ")){String value=fmt.substring("[IV][%8.2f][DSP] ".length());value=value.substring(0,value.length()-2);log("DSP","",0,format(e,value,14));return;}
            if(fmt.startsWith("[I_][%8.2f][SHOT][%s:%u] ")){String value=fmt.substring("[I_][%8.2f][SHOT][%s:%u] ".length());value=value.substring(0,value.length()-2);log("SHOT",basename(string(e,e.register(14))),e.register(15),value);return;}
            throw new IllegalStateException("Unreviewed diagnostic "+fmt);
        }
        case 0x400d56c4:{
            e.countBoundary("TestScope_constructor_capture");long p=e.register(10);for(int i=0;i<4;i++)e.write(p+4*i,e.register(11+i),4);
            events.write(5);events.write(1);text(string(e,e.register(11)));text(basename(string(e,e.register(13))));word(e.register(14));text(string(e,e.register(12)));return;
        }
        case 0x400db438:{
            e.countBoundary("TestScope_destructor_capture");long p=e.register(10);events.write(5);events.write(0);text(string(e,e.read(p,4)));text(basename(string(e,e.read(p+8,4))));word(e.read(p+12,4));text(string(e,e.read(p+4,4)));return;
        }
        case 0x400566c4:{String value=string(e,e.register(10)).stripLeading();java.util.regex.Matcher m=java.util.regex.Pattern.compile("^[+-]?[0-9]+").matcher(value);e.register(10,m.find()?Long.parseLong(m.group()):0);e.countBoundary("ROM_atoi_reviewed_int32_domain");return;}
        case 0x4000c44c:{int n=(int)e.register(12);if(n<0||n>65536)throw new IllegalStateException("memset bound");byte[] out=new byte[n];Arrays.fill(out,(byte)e.register(11));if(n>0)e.emulator.writeMemory(e.address(e.register(10)),out);e.countBoundary("ROM_memset");return;}
        case 0x4000c2c8:{int n=(int)e.register(12);if(n<0||n>65536)throw new IllegalStateException("memcpy bound");if(n>0)e.emulator.writeMemory(e.address(e.register(10)),bytes(e,e.register(11),n));e.countBoundary("ROM_memcpy");return;}
        case 0x400015d4:{int n=(int)e.register(12);if(n<0||n>65536)throw new IllegalStateException("strncpy bound");byte[] out=new byte[n];boolean ended=false;for(int i=0;i<n;i++){if(!ended){out[i]=(byte)e.read(e.register(11)+i,1);ended=out[i]==0;}}if(n>0)e.emulator.writeMemory(e.address(e.register(10)),out);e.countBoundary("ROM_strncpy");return;}
        case 0x400014c0:{int n=0;while(n<65536&&e.read(e.register(10)+n,1)!=0)n++;if(n==65536)throw new IllegalStateException("strlen bound");e.register(10,n);e.countBoundary("ROM_strlen");return;}
        case 0x4013cd98:{long p=e.register(10),cap=e.register(11);String formatted=format(e,string(e,e.register(12)),13);byte[] data=formatted.getBytes(StandardCharsets.UTF_8);int n=Math.min(data.length,Math.max(0,(int)cap-1));if(n>0)e.emulator.writeMemory(e.address(p),Arrays.copyOf(data,n));if(cap>0)e.write(p+n,0,1);e.register(10,data.length);e.countBoundary("bounded_snprintf_integer_and_string");return;}
        case 0x401462c4:{
            long p=e.register(10),cap=e.register(11);String value=string(e,e.register(12));
            if(value.equals("Line %d")){int aligned=((int)e.register(15)+3)&-4;int next=aligned+4;long base;if(next<=24)base=e.register(14);else{if(aligned<=24)next=36;base=e.register(13);}value="Line "+(int)e.read(base+next-4,4);}
            else if(value.contains("%"))throw new IllegalStateException("Unreviewed va_list format "+value);
            byte[] data=value.getBytes(StandardCharsets.UTF_8);int n=Math.min(data.length,Math.max(0,(int)cap-1));if(n>0)e.emulator.writeMemory(e.address(p),Arrays.copyOf(data,n));if(cap>0)e.write(p+n,0,1);e.register(10,data.length);e.countBoundary("bounded_vsnprintf_reviewed_Xtensa_va_list");return;
        }
        case 0x401367a0:throw new IllegalStateException("Original stack canary failed");
        default:if((target>=0x400d0000L&&target<0x40110000L)||(target>=0x401bf000L&&target<0x401c0200L)){e.invoke(target);return;}throw new IllegalStateException(String.format("Unreviewed Display dependency %08x",target));
        }
    }
    private void invoke(DisplayConsoleExecution e,long fn,long... args)throws Exception{for(int i=0;i<args.length;i++)e.register(10+i,args[i]);e.invoke(fn);}
    private Map<String,Object> one(DisplayConsoleExecution e,JsonObject input)throws Exception{
        long[] words=new long[19];for(int i=0;i<19;i++)words[i]=input.getAsJsonArray("words").get(i).getAsLong();
        e.resetRegisters();e.clear(D,0x2100);e.clear(A,0x1000);events.reset();frames.clear();asserting=false;
        for(int i=0;i<4096;i++){e.write(D+11+i,(i*73+17)^(i>>>3),1);e.write(D+0x100b+i,(i*73+64)^(i>>>3),1);}
        e.write(D+4,SPI,4);e.write(D+8,0,1);e.write(D+9,2,1);e.write(D+10,4,1);e.write(D+0x2068,64,4);
        e.write(D+0x201c,0,4);e.write(D+0x2020,0,4);e.write(D+0x2024,127,4);e.write(D+0x2028,63,4);invoke(e,0x400d1ae4L,D+0x200c,FONTS[(int)words[5]],words[6]);
        e.write(D+0x2060,words[1],4);e.write(D+0x2064,words[2],4);e.write(D+0x205c,words[4],1);e.write(0x3ffc2e8cL,words[3],1);e.write(0x3ffc4654L,0x1195a5a5L,4);e.write(0x3ffc2e84L,7,4);
        boolean nullArgument=input.get("argument").isJsonNull();
        byte[] argument=(nullArgument?"":input.get("argument").getAsString()).getBytes(StandardCharsets.UTF_8);e.emulator.writeMemory(e.address(T),argument);e.write(T+argument.length,0,1);e.write(A,D,4);e.write(A+4,nullArgument?0:T,4);
        long fn=words[0];if(fn==0x400e7fbcL||fn==0x400f1318L)invoke(e,fn,D);
        else if(fn==0x400ee634L)invoke(e,fn,D-0x17f8,nullArgument?0:T);
        else invoke(e,fn,A,A+4);
        words[1]=e.read(D+0x2060,4);words[2]=e.read(D+0x2064,4);words[3]=e.read(0x3ffc2e8cL,1);words[4]=e.read(D+0x205c,1);
        for(int i=0;i<5;i++)if(e.read(D+0x2014,4)==e.read(FONTS[i]+8,4))words[5]=i;
        words[6]=e.read(D+0x2018,4);words[7]=e.read(0x3ffc2e84L,4);words[8]=0;
        for(int i=0;i<10;i++)words[9+i]=e.read(D+0x201c+i*4,4);
        return row("id",input.get("id").getAsString(),"words",words,"pixels_sha256",sha(bytes(e,D+11,4096)),"transfer_sha256",sha(bytes(e,D+0x100b,4096)),"events_sha256",sha(events.toByteArray()),"frames",new ArrayList<>(frames));
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input JSON and output JSONL");byte[] data=Files.readAllBytes(Path.of(args[0]));JsonArray inputs=JsonParser.parseString(new String(data,StandardCharsets.UTF_8)).getAsJsonArray();int count=0;
        try(DisplayConsoleExecution e=new DisplayConsoleExecution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){
            for(JsonElement value:inputs){try{out.write(new Gson().toJson(one(e,value.getAsJsonObject()))+"\n");out.flush();}catch(Exception failure){throw new IllegalStateException("Display case "+count+": "+value,failure);}count++;}
            List<Object> functions=new ArrayList<>();for(String address:e.functions.keySet()){Function fn=getFunctionAt(toAddr(Long.parseLong(address.substring(2),16)));if(fn==null)fn=createFunction(toAddr(Long.parseLong(address.substring(2),16)),"display_callback_"+address.substring(2));if(fn==null)throw new IllegalStateException("Missing function bounds "+address);List<Object> ranges=new ArrayList<>();for(AddressRange range:fn.getBody().getAddressRanges()){byte[] bytes=new byte[(int)range.getLength()];currentProgram.getMemory().getBytes(range.getMinAddress(),bytes);ranges.add(row("address",range.getMinAddress().getOffset(),"size",bytes.length,"sha256",sha(bytes)));}functions.add(row("address",address,"calls",e.functions.get(address),"ranges",ranges));}
            Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(row("schema_version",1,"input_sha256",DisplayConsoleExecution.IMAGE_SHA,"cases",count,"input_file_sha256",sha(data),"original_functions",functions,"modeled_boundaries",e.boundaries,"hardware_tested",false,"full_cpu_emulation",false,"scope","Complete Display console callbacks and three diagnostic demonstration entry points execute through real text, geometry, bitmap, gradient, caret, animation, graphics and transfer reversal bodies. Compare every transfer frame, ordered GPIO/SPI/delay/diagnostic/test-scope effects, persistent context and assertion continuation. TestScope logging is an explicit capture boundary; ROM primitives, bounded printf/atoi, SDK transports, prescribed clock and windowed ABI are modeled."))+"\n");
        }
        println("Executed "+count+" complete original Display console/demo cases");
    }
}
