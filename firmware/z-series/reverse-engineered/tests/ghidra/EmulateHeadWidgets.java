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

public class EmulateHeadWidgets extends GhidraScript {
    private static final long W=0x3ff20000L,D=0x3ff50000L,R=0x3ff55000L,T=0x3ff60000L,L=0x3ff70000L;
    private static final long[] FONTS={0x3f42b31cL,0x3f42ded4L,0x3f437478L,0x3f43d718L,0x3f440f88L};
    private long assertionLine;
    private static String sha(byte[] bytes)throws Exception{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));}
    private static Map<String,Object> row(Object... values){Map<String,Object> r=new LinkedHashMap<>();for(int i=0;i<values.length;i+=2)r.put((String)values[i],values[i+1]);return r;}
    private byte[] bytes(DisplayExecution e,long p,int count)throws Exception{return e.emulator.readMemory(e.address(p),count);}
    private String string(DisplayExecution e,long p)throws Exception{if(p==0)throw new IllegalStateException("Null string boundary");int n=0;while(n<65536&&e.read(p+n,1)!=0)n++;if(n==65536)throw new IllegalStateException("Unbounded string");return new String(bytes(e,p,n),StandardCharsets.UTF_8);}
    private void boundary(DisplayExecution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        switch((int)target){
        case 0x40081388:e.countBoundary("prescribed_millis");e.register(10,777);return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:e.countBoundary("diagnostic_output");return;
        case 0x400db014:assertionLine=e.register(11);e.invoke(target);return;
        case 0x401289a0:if(e.register(10)!=200)throw new IllegalStateException("Unexpected assertion delay");e.countBoundary("assertion_delay_200ms_prescribed_clock");return;
        case 0x401367a0:throw new IllegalStateException("Original stack guard failed");
        case 0x4000c2c8:{int n=(int)e.register(12);if(n<0||n>65536)throw new IllegalStateException("memcpy bound");if(n>0)e.emulator.writeMemory(e.address(e.register(10)),bytes(e,e.register(11),n));e.countBoundary("ROM_memcpy");return;}
        case 0x4000c44c:{int n=(int)e.register(12);if(n<0||n>65536)throw new IllegalStateException("memset bound");byte[] bytes=new byte[n];Arrays.fill(bytes,(byte)e.register(11));if(n>0)e.emulator.writeMemory(e.address(e.register(10)),bytes);e.countBoundary("ROM_memset");return;}
        case 0x400014c0:{long p=e.register(10);int n=0;if(p==0)throw new IllegalStateException("Null strlen");while(n<65536&&e.read(p+n,1)!=0)n++;if(n==65536)throw new IllegalStateException("strlen bound");e.register(10,n);e.countBoundary("ROM_strlen");return;}
        case 0x4013cd98:{
            long p=e.register(10),capacity=e.register(11);String format=string(e,e.register(12));StringBuilder out=new StringBuilder();int argument=13;
            for(int i=0;i<format.length();i++){char ch=format.charAt(i);if(ch!='%'){out.append(ch);continue;}ch=format.charAt(++i);if(ch=='%'){out.append('%');continue;}int width=0;boolean zero=ch=='0';while(ch>='0'&&ch<='9'){width=width*10+ch-'0';ch=format.charAt(++i);}long value=e.register(argument++);String replacement;if(ch=='d')replacement=Integer.toString((int)value);else if(ch=='u')replacement=Long.toString(value);else if(ch=='c')replacement=String.valueOf((char)(value&255));else throw new IllegalStateException("Unreviewed format "+format);while(replacement.length()<width)replacement=(zero?"0":" ")+replacement;out.append(replacement);}
            byte[] bytes=out.toString().getBytes(StandardCharsets.UTF_8);if(capacity<0||capacity>65536)throw new IllegalStateException("snprintf bound");int n=Math.min(bytes.length,Math.max(0,(int)capacity-1));if(n>0)e.emulator.writeMemory(e.address(p),Arrays.copyOf(bytes,n));if(capacity>0)e.write(p+n,0,1);e.register(10,bytes.length);e.countBoundary("bounded_snprintf_integer_character");return;
        }
        default:
            // Application graphics helpers execute their actual instructions;
            // no draw calls or rectangle/text/bitmap bodies are substituted.
            if((target>=0x400d0000L&&target<0x40100000L)||(target>=0x401bf000L&&target<0x401c0200L)){e.invoke(target);return;}
            throw new IllegalStateException(String.format("Unreviewed widget dependency %08x",target));
        }
    }
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
                JsonObject input=value.getAsJsonObject();int op=input.get("operation").getAsInt();JsonArray bounds=input.getAsJsonArray("rect");long number=input.get("value").getAsLong(),flag=input.get("flag").getAsLong();
                e.resetRegisters();e.clear(W,0x3000);e.clear(D,0x2100);e.clear(R,0x1000);e.clear(T,0x4000);assertionLine=0;e.write(0x3ffc4654L,0x5a61c0deL,4);e.write(0x3ffc2e84L,input.get("counter").getAsLong(),4);
                byte[] pixels=new byte[4096];for(int i=0;i<4096;i++)pixels[i]=(byte)((i*73+input.get("seed").getAsInt())^(i>>>3));e.emulator.writeMemory(e.address(D+11),pixels);e.write(D+0x2068,64,4);e.write(D+0x201c,0,4);e.write(D+0x2020,0,4);e.write(D+0x2024,127,4);e.write(D+0x2028,63,4);invoke(e,0x400d1ae4L,D+0x200c,FONTS[4],0);
                put(e,T,input.get("first").getAsString());put(e,T+0x100,input.get("second").getAsString());e.write(L,L+0x100,4);e.write(L+0x100,input.get("language").getAsLong(),4);long titleHeight=0;
                try{
                    switch(op){
                    case 0:e.write(W,D,4);font(e,W+36,0);e.write(W+49,flag,1);e.write(W+50,1,1);invoke(e,0x400f82e8L,W+4,T);invoke(e,0x400f8424L,W);titleHeight=e.read(W+48,1);break;
                    case 1:e.write(W,D,4);rect(e,W+4,bounds);font(e,W+20,2);e.write(W+32,5,4);e.write(W+36,6,4);e.write(W+40,6,4);e.write(W+44,number,4);e.write(W+48,flag,1);invoke(e,0x400e9ed4L,W);break;
                    case 2:e.write(W+4,D,4);rect(e,W+8,bounds);font(e,W+24,2);e.write(W+36,13,4);e.write(W+45,flag,1);e.write(W+48,number,4);invoke(e,0x400e8aacL,W);break;
                    case 3:e.write(W+4,D,4);rect(e,W+8,bounds);font(e,W+24,1);font(e,W+36,4);e.write(W+52,number,4);invoke(e,0x400eb49cL,W);break;
                    case 4:e.write(W+4,D,4);e.write(W+8,L,4);rect(e,W+12,bounds);font(e,W+28,4);e.write(W+40,9,4);e.write(W+48,number,4);e.write(W+52,3,4);e.write(W+56,5,4);e.write(W+60,2,4);e.write(W+68,3,4);invoke(e,0x400e9cacL,W);break;
                    case 5:e.write(W,D,4);rect(e,W+4,bounds);font(e,W+20,4);put(e,W+32,input.get("first").getAsString());put(e,W+80,input.get("second").getAsString());e.write(W+129,number,1);e.write(W+136,flag,1);e.write(W+137,1,1);e.write(W+140,0x3f40bbddL,4);invoke(e,0x400e8b44L,W);break;
                    case 6:e.write(W+8,D,4);rect(e,W+12,bounds);e.write(W+36,number,4);e.writeDouble(W+48,0);e.writeDouble(W+56,1);invoke(e,0x400ee760L,W);break;
                    case 7:{
                        e.write(W+12,D,4);rect(e,R,bounds);JsonArray lines=input.getAsJsonArray("lines");for(int i=0;i<lines.size();i++){long address=T+0x400+i*0x100;put(e,address,lines.get(i).getAsString());e.write(R+0x100+4*i,address,4);}e.write(e.register(1),input.get("vertical").getAsLong(),4);e.write(e.register(1)+4,input.get("padding").getAsLong(),4);invoke(e,0x400e7ddcL,W,R,R+0x100,lines.size(),FONTS[input.get("font").getAsInt()],input.get("horizontal").getAsLong());break;
                    }
                    default:throw new IllegalStateException("Invalid widget operation");
                    }
                }catch(Exception ex){throw new IllegalStateException("Case "+caseIndex+" (widget "+op+") failed: "+input,ex);}
                results.add(row("pixels_sha256",sha(bytes(e,D+11,4096)),"state",Arrays.asList(e.read(D+0x205c,1),assertionLine,e.read(0x3ffc2e84L,4),titleHeight)));caseIndex++;
            }
            for(String address:e.functions.keySet()){Function function=currentProgram.getFunctionManager().getFunctionAt(toAddr(Long.parseLong(address.substring(2),16)));if(function==null)throw new IllegalStateException("Executed body lacks defined bounds "+address);List<Object> ranges=new ArrayList<>();for(AddressRange range:function.getBody().getAddressRanges()){byte[] bytes=new byte[(int)range.getLength()];currentProgram.getMemory().getBytes(range.getMinAddress(),bytes);ranges.add(row("address",range.getMinAddress().getOffset(),"size",bytes.length,"sha256",sha(bytes)));}functions.add(row("address",address,"calls",e.functions.get(address),"ranges",ranges));}
            Map<String,Object> report=row("input_sha256",DisplayExecution.IMAGE_SHA,"cases",results,"original_functions",functions,"modeled_boundaries",e.boundaries,"scope","Concrete title, progress, temperature, duration, strength, waiting, valve and text-block painters including original rectangle, glyph, font and bitmap bodies. Widget fields use constructor-established defaults. ROM memory/string functions, bounded integer/character snprintf, compiler arithmetic, clock/log/delay remain explicit boundaries; assertion bodies execute and return.","hardware_tested",false,"full_cpu_emulation",false);Files.writeString(Path.of(args[2]),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+results.size()+" original widget framebuffer cases");
    }
}
