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

public class EmulateHeadQuantity extends GhidraScript {
    private static final long W=0x3ff20000L,D=0x3ff50000L,R=0x3ff55000L,T=0x3ff60000L,L=0x3ff70000L;
    private static final long[] FONTS={0x3f42b31cL,0x3f42ded4L,0x3f437478L,0x3f43d718L,0x3f440f88L};
    private long assertionLine; private final List<Object> events=new ArrayList<>();
    private static String sha(byte[] bytes)throws Exception{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));}
    private static Map<String,Object> row(Object... values){Map<String,Object> r=new LinkedHashMap<>();for(int i=0;i<values.length;i+=2)r.put((String)values[i],values[i+1]);return r;}
    private byte[] bytes(DisplayExecution e,long p,int count)throws Exception{return e.emulator.readMemory(e.address(p),count);}
    private String string(DisplayExecution e,long p)throws Exception{if(p==0)throw new IllegalStateException("Null string boundary");int n=0;while(n<65536&&e.read(p+n,1)!=0)n++;if(n==65536)throw new IllegalStateException("Unbounded string");return n==0?"":new String(bytes(e,p,n),StandardCharsets.UTF_8);}
    private void boundary(DisplayExecution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        if(target==0x4012d48cL){e.countBoundary("__truncdfsf2");e.register(10,Float.floatToRawIntBits((float)e.number(10)));return;}
        switch((int)target){
        case 0x4012d42c:{double v=e.number(10);if(!Double.isFinite(v)||v<0||v>=4294967296.0)throw new IllegalStateException("Unsigned conversion domain");e.register(10,(long)v);e.countBoundary("finite_uint32_conversion");return;}
        case 0x4013b1d4:{double v=e.number(10);if(!Double.isFinite(v)||Math.abs(v)>1000000)throw new IllegalStateException("round domain");e.number(10,Math.copySign(Math.floor(Math.abs(v)+0.5),v));e.countBoundary("finite_round_half_away_from_zero");return;}
        case 0x40081388:e.countBoundary("prescribed_millis");e.register(10,777);return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:e.countBoundary("diagnostic_output");return;
        case 0x400db014:assertionLine=e.register(11);e.invoke(target);return;
        case 0x401289a0:if(e.register(10)!=200)throw new IllegalStateException("Unexpected assertion delay");e.countBoundary("assertion_delay_200ms_prescribed_clock");return;
        case 0x401367a0:throw new IllegalStateException("Original stack guard failed");
        case 0x400015d4:{int n=(int)e.register(12);if(n<0||n>65536)throw new IllegalStateException("strncpy bound");byte[] out=new byte[n];boolean ended=false;for(int i=0;i<n;i++){if(!ended){out[i]=(byte)e.read(e.register(11)+i,1);if(out[i]==0)ended=true;}}if(n>0)e.emulator.writeMemory(e.address(e.register(10)),out);e.countBoundary("ROM_strncpy");return;}
        case 0x4000c2c8:{int n=(int)e.register(12);if(n<0||n>65536)throw new IllegalStateException("memcpy bound");if(n>0)e.emulator.writeMemory(e.address(e.register(10)),bytes(e,e.register(11),n));e.countBoundary("ROM_memcpy");return;}
        case 0x4000c44c:{int n=(int)e.register(12);if(n<0||n>65536)throw new IllegalStateException("memset bound");byte[] bytes=new byte[n];Arrays.fill(bytes,(byte)e.register(11));if(n>0)e.emulator.writeMemory(e.address(e.register(10)),bytes);e.countBoundary("ROM_memset");return;}
        case 0x400014c0:{long p=e.register(10);int n=0;if(p==0)throw new IllegalStateException("Null strlen");while(n<65536&&e.read(p+n,1)!=0)n++;if(n==65536)throw new IllegalStateException("strlen bound");e.register(10,n);e.countBoundary("ROM_strlen");return;}
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
    private long argument(DisplayExecution e,int index)throws Exception{return index<16?e.register(index):e.read(e.register(1)+(index-16)*4,4);}
    private void invoke(DisplayExecution e,long target,long... args)throws Exception{for(int i=0;i<args.length;i++)e.register(10+i,args[i]);e.invoke(target);}
    private void put(DisplayExecution e,long p,String value){byte[] bytes=value.getBytes(StandardCharsets.UTF_8);e.emulator.writeMemory(e.address(p),bytes);e.write(p+bytes.length,0,1);}
    private void rect(DisplayExecution e,long p,JsonArray rect){for(int i=0;i<4;i++)e.write(p+i*4,rect.get(i).getAsLong(),4);}
    private void font(DisplayExecution e,long p,int index)throws Exception{for(int i=0;i<3;i++)e.write(p+i*4,e.read(FONTS[index]+i*4,4),4);}
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=3)throw new IllegalArgumentException("Expected cases, verified image layout and output JSON");JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();JsonObject layout=JsonParser.parseString(Files.readString(Path.of(args[1]))).getAsJsonObject();byte[] raw=Files.readAllBytes(Path.of(layout.get("input_path").getAsString()));
        if(!sha(raw).equals(DisplayExecution.IMAGE_SHA))throw new IllegalStateException("Wrong original image");for(JsonElement value:layout.getAsJsonArray("segments")){JsonObject s=value.getAsJsonObject();int size=s.get("size").getAsInt();if(size==0)continue;byte[] loaded=new byte[size];currentProgram.getMemory().getBytes(toAddr(s.get("address").getAsLong()),loaded);int offset=s.get("offset").getAsInt();if(!Arrays.equals(loaded,Arrays.copyOfRange(raw,offset,offset+size)))throw new IllegalStateException("Project differs from original bytes");}
        List<Object> results=new ArrayList<>(),functions=new ArrayList<>();int caseIndex=0;
        try(DisplayExecution e=new DisplayExecution(currentProgram,monitor,this::boundary)){
            for(JsonElement value:inputs){
                JsonObject input=value.getAsJsonObject();int operation=input.get("operation").getAsInt();
                e.resetRegisters();e.clear(W-0x100,0x8100);e.clear(D,0x2100);e.clear(R,0x1000);e.clear(T,0x4000);e.clear(L,0x5000);assertionLine=0;e.write(0x3ffc4654L,0x5a61c0deL,4);e.write(0x3ffc2e84L,7,4);
                byte[] pixels=new byte[4096];for(int i=0;i<4096;i++)pixels[i]=(byte)((i*73+17)^(i>>>3));e.emulator.writeMemory(e.address(D+11),pixels);e.write(D+0x2068,64,4);e.write(D+0x201c,0,4);e.write(D+0x2020,0,4);e.write(D+0x2024,127,4);e.write(D+0x2028,63,4);invoke(e,0x400d1ae4L,D+0x200c,FONTS[4],0);
                e.write(W+4,D,4);rect(e,R,input.getAsJsonArray("rect"));font(e,W+24,2);font(e,W+36,0);font(e,W+48,4);
                e.write(W+60,1,4);e.write(W+64,8,4);e.write(W+76,8,4);e.writeDouble(W+120,1);
                e.writeDouble(R+32,input.get("value").getAsDouble());e.write(R+40,input.get("unit").getAsLong(),4);
                try {
                    if(operation==0)invoke(e,0x400eb5b8L,W,R,input.get("id").getAsLong(),R+32,input.get("metric").getAsBoolean()?1:0);
                    else invoke(e,0x400df864L,R+32,T,32,input.get("metric").getAsBoolean()?1:0,input.get("precision").getAsLong());
                }catch(Exception ex){throw new IllegalStateException("Case "+caseIndex+" failed: "+input,ex);}
                if(operation==1){results.add(row("text",string(e,T)));caseIndex++;continue;}
                List<Long> state=Arrays.asList(e.read(W+60,4),e.read(W+64,4),e.read(W+76,4),e.read(W+96,1),e.read(W+97,1),e.read(W+100,4),e.read(W+104,4),e.read(D+0x205c,1),assertionLine,e.read(0x3ffc2e84L,4));
                results.add(row("pixels_sha256",sha(bytes(e,D+11,4096)),"state",state,"minimum",e.readDouble(W+112),"step",e.readDouble(W+120)));caseIndex++;
            }
            for(String address:e.functions.keySet()){Function function=currentProgram.getFunctionManager().getFunctionAt(toAddr(Long.parseLong(address.substring(2),16)));if(function==null)throw new IllegalStateException("Executed body lacks defined bounds "+address);List<Object> ranges=new ArrayList<>();for(AddressRange range:function.getBody().getAddressRanges()){byte[] bytes=new byte[(int)range.getLength()];currentProgram.getMemory().getBytes(range.getMinAddress(),bytes);ranges.add(row("address",range.getMinAddress().getOffset(),"size",bytes.length,"sha256",sha(bytes)));}functions.add(row("address",address,"calls",e.functions.get(address),"ranges",ranges));}
            Map<String,Object> report=row("input_sha256",DisplayExecution.IMAGE_SHA,"cases",results,"original_functions",functions,"modeled_boundaries",e.boundaries,"scope","Original quantity editor setup/configuration/drawing and unit formatting execute, including returning assertions. Constructor fields, SDK fixed-point formatting and arithmetic, ROM primitives, prescribed clock/log/delay and windowed ABI remain explicit boundaries","hardware_tested",false,"full_cpu_emulation",false);Files.writeString(Path.of(args[2]),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+results.size()+" original quantity-editor framebuffer cases");
    }
}
