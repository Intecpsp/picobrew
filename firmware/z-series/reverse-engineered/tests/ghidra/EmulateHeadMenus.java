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

public class EmulateHeadMenus extends GhidraScript {
    private static final long W=0x3ff20000L,D=0x3ff1df50L,R=0x3ff55000L,T=0x3ff60000L,L=0x3ff70000L;
    private static final long[] FONTS={0x3f42b31cL,0x3f42ded4L,0x3f437478L,0x3f43d718L,0x3f440f88L};
    private long assertionLine;
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
        case 0x400deb58:e.countBoundary("Head_alarm_service");return;
        case 0x40082cc4:e.countBoundary("SDK_flash_chip_size");e.register(10,0x800000);return;
        case 0x401231dc:case 0x40123218:e.countBoundary("SDK_WiFi_IPAddress");e.write(e.register(10)+4,target==0x401231dcL?0x642ba8c0L:0x012ba8c0L,4);return;
        case 0x4012333c:e.countBoundary("SDK_WiFi_RSSI");e.register(10,0xffffffd6L);return;
        case 0x40123240:case 0x401232d4:{e.countBoundary("SDK_WiFi_String");String value=target==0x40123240L?"12345678901234567890123456789012":"01:23:45:67:89:AB";put(e,T+0x3800,value);e.write(e.register(10),T+0x3800,4);return;}
        case 0x4012a624:{e.countBoundary("SDK_IPAddress_toString");long address=e.read(e.register(11)+4,4);String value=(address&255)+"."+((address>>8)&255)+"."+((address>>16)&255)+"."+(address>>24);put(e,T+0x3800,value);e.write(e.register(10),T+0x3800,4);return;}
        case 0x4012a994:e.countBoundary("SDK_String_destructor");return;
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
                e.resetRegisters();e.clear(W-0x38a8,0xb900);e.clear(D,0x2100);e.clear(R,0x1000);e.clear(T,0x4000);e.clear(L,0x5000);assertionLine=0;e.write(0x3ffc4654L,0x5a61c0deL,4);e.write(0x3ffc2e84L,7,4);
                byte[] pixels=new byte[4096];for(int i=0;i<4096;i++)pixels[i]=(byte)((i*73+17)^(i>>>3));e.emulator.writeMemory(e.address(D+11),pixels);e.write(D+0x2068,64,4);e.write(D+0x201c,0,4);e.write(D+0x2020,0,4);e.write(D+0x2024,127,4);e.write(D+0x2028,63,4);invoke(e,0x400d1ae4L,D+0x200c,FONTS[4],0);
                long menu=W+0x2210,marquee=W+0x102c,generator=W+0xe0,data=W+0x27b0,paragraph=W+0x21a0,core=L,settings=L+0x3000;
                e.write(W+8,D,4);e.write(W+12,settings,4);e.write(W+16,core,4);e.write(W-44,core,4);e.write(W-40,data,4);e.write(W-56,settings,4);
                e.write(settings+13,input.get("imperial").getAsBoolean()?1:0,1);e.write(settings+16,input.get("language").getAsLong(),4);e.write(data,input.get("language").getAsLong(),4);e.write(core+12,input.get("service").getAsBoolean()?1:0,1);e.write(core+14,input.get("offline").getAsBoolean()?1:0,1);
                JsonArray temperatures=input.getAsJsonArray("temperatures");e.writeDouble(core+0x2b0,temperatures.get(0).getAsDouble());e.writeDouble(core+0x388,temperatures.get(1).getAsDouble());e.writeDouble(core+0x390,temperatures.get(2).getAsDouble());e.write(core+0x39c,input.get("duration").getAsLong(),4);e.write(core+20,input.get("session").getAsLong(),4);
                e.write(W+0x2628,D,4);font(e,W+0x2628+36,0);e.write(W+0x10cc,D,4);e.write(W+0x2270,menu,4);e.write(W+0x2274,paragraph,4);
                e.write(menu+4,D,4);e.write(menu+8,data,4);e.write(menu+12,marquee,4);e.write(menu+20,14,4);font(e,menu+40,4);e.write(menu+56,9,4);e.write(menu+80,1,1);e.write(menu+88,10,4);
                e.write(marquee+8,generator,4);e.write(marquee+12,D,4);e.write(marquee+24,800,4);e.write(marquee+28,25,4);e.write(generator+0xf24,512,4);e.write(generator+0xf28,15,4);e.write(generator+0xf2c,256,4);e.write(generator+0xf38,255,4);e.write(generator+0xf3c,14,4);e.write(generator+0xf40,generator+0x24,4);e.write(generator+0xf44,3840,4);
                e.write(paragraph+4,D,4);e.write(paragraph+8,data,4);font(e,paragraph+0x3c,4);e.write(paragraph+0x48,8,4);e.write(paragraph+0x6c,125,2);
                long character=W+0x227c,quantity=W+0x2470;e.write(character+4,D,4);e.write(character+8,W-40,4);e.write(character+160,64,4);
                e.write(quantity+4,D,4);font(e,quantity+24,2);font(e,quantity+36,0);font(e,quantity+48,4);e.write(quantity+60,1,4);e.write(quantity+64,8,4);e.write(quantity+76,8,4);e.writeDouble(quantity+120,1);
                long temp=W+0x232c;e.write(temp+4,D,4);font(e,temp+24,2);e.write(temp+36,13,4);e.write(temp+56,50,4);e.write(temp+52,200,4);
                long duration=W+0x2370;e.write(duration+4,D,4);font(e,duration+24,1);font(e,duration+36,4);
                long strength=W+0x23b4;e.write(strength+4,D,4);e.write(strength+8,W-40,4);font(e,strength+28,4);e.write(strength+40,9,4);e.write(strength+52,3,4);e.write(strength+56,5,4);e.write(strength+60,2,4);e.write(strength+68,3,4);
                e.write(W+0x2410,D,4);long progress=W+0x2564;e.write(progress,D,4);font(e,progress+20,2);e.write(progress+32,5,4);e.write(progress+36,6,4);e.write(progress+40,6,4);
                long waiting=W+0x2598;e.write(waiting,D,4);font(e,waiting+20,4);e.write(waiting+128,4,1);e.write(waiting+140,0x3f40bbddL,4);
                e.write(W+0x1054,500,4);e.write(W+0x105c,D,4);e.write(W+0x1060,core,4);e.write(W+0x265c,D,4);e.write(W+0x2660,core,4);
                long pause=W+0x2510;e.write(pause,D,4);e.write(pause+4,settings,4);e.write(pause+8,core,4);e.write(pause+12,W-40,4);font(e,pause+36,0);font(e,pause+48,4);e.write(pause+60,5,4);e.write(pause+64,2,4);e.write(pause+68,5,4);e.write(pause+72,500,4);
                e.write(W+0x2770,D,4);e.write(W+0x2774,core,4);long[] levelBox={39,12,87,60,63,36,63,36};for(int i=0;i<8;i++)e.write(W+0x2780+4*i,levelBox[i],4);
                int selectedLayout=input.get("layout").getAsInt();if(operation==0&&(selectedLayout==0||selectedLayout==1||selectedLayout==14||selectedLayout==16)){
                    e.write(core+0x14,6,4);
                    e.write(core+0x2c1,1,1);
                    e.write(core+0x2e5,3,1);
                    e.write(core+0x2e3,4,1);
                    e.write(core+0x2ec,0,1);
                    e.write(core+0x2e1,112,1);
                    e.write(core+0x2e2,8,1);
                    e.write(core+0x2e8,0,4);
                    e.write(core+0x526,0,1);
                    e.write(core+0x2f4,3,4);
                    e.write(core+0x368,8345,4);
                    e.write(core+0x37c,234,4);
                    e.write(core+0x374,23,4);
                    e.write(core+0x36c,4,4);
                    e.write(core+0x370,600,4);
                    e.write(core+0x944,19,4);
                    e.write(core+0x107c,0,1);
                    e.writeDouble(core+0x2b0,81.5);
                    e.writeDouble(core+0x2c8,45.5);
                    e.writeDouble(core+0x548,91.25);
                    e.writeDouble(core+0x558,78.5);
                    e.writeDouble(core+0x568,23.4);
                    e.writeDouble(core+0x578,19.75);
                    e.writeDouble(core+0x788,1);
                    e.writeDouble(core+0x790,0.03);
                    e.writeDouble(core+0x798,0.04);
                    put(e,core+0x24,"Recipe name");put(e,T+0x3000,"Mash");put(e,T+0x3100,"Circulating");e.write(core+0x304,T+0x3000,4);e.write(core+0x35c,T+0x3100,4);put(e,core+0x984,"Still sample error");e.write(core+0x9c8,18,4);e.write(core+0x2fc,input.get("pause_reason").getAsLong(),4);
                }
                e.write(W+0x66d0,input.get("diagnostic_line").getAsLong(),1);
                JsonArray labels=input.getAsJsonArray("labels");e.write(data+4,labels.size(),4);for(int i=0;i<labels.size();i++){put(e,data+12+424*i,labels.get(i).getAsString());e.write(data+156+424*i,i==0&&input.get("back").getAsBoolean()?1:10+i,4);}
                JsonArray scroll=input.getAsJsonArray("scroll");e.write(data+0x3508,scroll.size(),4);for(int i=0;i<scroll.size();i++){long line=data+0x350c+80*i;String v=scroll.get(i).getAsString();put(e,line,v);e.write(line+0x44,v.getBytes(StandardCharsets.UTF_8).length,4);e.write(line+0x48,64,4);}
                put(e,T,input.get("title").getAsString());put(e,T+0x100,input.get("detail").getAsString());put(e,T+0x200,input.get("button").getAsString());
                JsonArray lines=input.getAsJsonArray("lines");for(int i=0;i<lines.size();i++){put(e,T+0x400+256*i,lines.get(i).getAsString());e.write(R+0x200+4*i,T+0x400+256*i,4);}
                e.write(R,T,4);e.write(R+4,input.get("layout").getAsLong(),4);e.write(R+8,input.get("command").getAsLong(),4);e.write(R+12,input.get("argument").getAsLong(),4);
                e.write(R+16,R+0x300,4);e.write(R+0x300,input.get("scroll_alignment").getAsLong(),4);e.write(R+0x304,input.get("scroll_command").getAsLong(),4);e.write(R+0x308,T+0x200,4);e.write(R+0x30c,input.get("custom_scroll").getAsBoolean()?1:0,1);
                e.write(R+20,R+0x100,4);e.write(R+0x100,R+0x200,4);e.write(R+0x104,lines.size(),1);e.write(R+0x108,input.get("horizontal").getAsLong(),4);e.write(R+0x10c,input.get("vertical").getAsLong(),4);
                e.write(R+28,R+0x400,4);e.write(R+0x400,input.get("rows").getAsLong(),1);e.write(R+0x404,input.get("menu_vertical").getAsLong(),4);e.write(R+0x408,0x3f42a954L,4);
                e.write(R+24,R+0x500,4);e.write(R+0x500,input.get("input_kind").getAsLong(),4);e.write(R+0x504,input.get("input_limit").getAsLong(),1);e.write(R+64,input.get("response").getAsLong(),4);
                e.write(R+40,input.get("quantity_id").getAsLong(),4);e.writeDouble(R+48,input.get("quantity_value").getAsDouble());e.write(R+56,input.get("quantity_unit").getAsLong(),4);
                if(!input.get("body").getAsBoolean())e.write(R+20,0,4);
                e.write(R+32,input.get("layout").getAsInt()==13?input.get("counter").getAsLong():input.get("selection").getAsLong(),2);e.write(R+34,input.get("animate").getAsBoolean()?1:0,1);e.write(R+35,input.get("percentage").getAsBoolean()?1:0,1);e.write(R+36,input.get("strength").getAsLong(),4);

                long head=W-0x38a8;
                e.write(core+4,12,4);e.write(core+0x7e0,input.get("network").getAsLong(),4);e.write(core+0x7dc,0x962ba8c0L,4);
                put(e,core+0x7ac,"Z119FIXTURE");put(e,core+0x27c,"Kitchen Z");put(e,core+0x898,"12345678901234567890123456789012");
                for(int i=0;i<6;i++){e.write(core+0x7cc+i,0x10+i,1);e.write(core+0x7d2+i,0x20+i,1);}e.write(core+0x2ed,3,1);e.write(core+0x2ee,4,1);
                e.write(settings+12,1,1);e.write(settings+14,input.get("detail_setting").getAsLong(),1);e.write(settings+15,input.get("static_setting").getAsLong(),1);
                put(e,settings+20,"saved-network");put(e,settings+52,"saved-password");e.write(settings+0x74,input.get("mode").getAsLong(),4);e.write(settings+0x78,input.get("mode").getAsLong(),4);e.write(settings+0x7c,input.get("mode").getAsLong(),4);
                e.write(head+0x1284,input.get("ui").getAsLong(),4);e.write(head+0x1288,1,1);e.write(head+0x128c,5,4);e.write(head+0x1398,1,1);
                try {
                    switch(operation){
                    case 0:invoke(e,0x40102d30L,W,R);break;
                    case 1:put(e,T,input.get("title").getAsString());invoke(e,0x400d1e08L,W);invoke(e,0x400e388cL,W,T,input.get("padding").getAsLong());break;
                    case 2:invoke(e,0x40103858L,W-0x38a8);break;
                    case 3:invoke(e,0x40104238L,W-0x38a8,input.get("screen_id").getAsLong(),input.get("title_id").getAsLong());break;
                    case 4:invoke(e,0x40103b1cL,W-0x38a8,T,T+0x100,input.get("animate").getAsBoolean()?1:0,input.get("counter").getAsLong());break;
                    case 5:invoke(e,0x40103aa4L,W-0x38a8,T+0x100);break;
                    case 6:invoke(e,0x400ef3e4L,W,W+0x66d0,input.get("delta").getAsLong());break;
                    case 7:invoke(e,0x401080d4L,head);break;
                    default:throw new IllegalStateException("Unknown screen fixture operation");
                    }
                }catch(Exception ex){throw new IllegalStateException("Case "+caseIndex+" failed: "+input,ex);}
                if(operation==1){List<String> wrapped=new ArrayList<>();for(int i=0;i<e.read(W+0x30,4);i++)wrapped.add(string(e,e.read(W+4*i,4)));results.add(row("lines",wrapped,"truncated",e.read(W+0x34,1)));caseIndex++;continue;}
                List<Long> state=new ArrayList<>();for(int i=0;i<4;i++)state.add(e.read(W+0x66c0+4*i,4));
                state.add(e.read(W+0x27a8,4));state.add(e.read(W+0x27a0,4));state.add(e.read(W+0x27a4,4));
                state.add(e.read(menu+92,1));state.add(e.read(menu+72,4));state.add(e.read(menu+76,4));state.add(e.read(menu+60,4));state.add(e.read(menu+64,4));
                state.add(e.read(paragraph+92,1));state.add(e.read(paragraph+76,4));state.add(e.read(paragraph+80,4));state.add(e.read(paragraph+104,4));state.add(e.read(paragraph+93,1));
                state.add(e.read(temp+44,1));state.add(e.read(temp+48,4));state.add(e.read(temp+56,4));state.add(e.read(temp+52,4));state.add(e.read(temp+45,1));
                state.add(e.read(duration+49,1));state.add(e.read(duration+52,4));state.add(e.read(duration+56,4));state.add(e.read(strength+44,1));state.add(e.read(strength+48,4));
                state.add(e.read(W+0x2428,1));state.add(e.read(W+0x242c,4));state.add(e.read(W+0x2430,4));state.add(e.read(W+0x2429,1));
                state.add(e.read(waiting+137,1));state.add(e.read(waiting+136,1));state.add(e.read(waiting+129,1));state.add(e.read(waiting+128,1));state.add(e.read(waiting+132,4));
                state.add(e.read(W+0x2658,1));state.add(e.read(W+0x2659,1));state.add(e.read(W+0x265a,1));state.add(e.read(W+0x2278,1));state.add(e.read(W+0x2279,1));state.add(e.read(W+0x227a,1));state.add(e.read(W+0x227b,1));
                state.add(e.read(D+0x205c,1));state.add(assertionLine);state.add(e.read(0x3ffc2e84L,4));
                List<Object> items=new ArrayList<>();for(int i=0;i<e.read(data+4,4);i++)items.add(Arrays.asList(e.read(data+156+424*i,4),string(e,data+12+424*i)));
                List<String> textLines=new ArrayList<>();for(int i=0;i<e.read(data+0x3508,4);i++)textLines.add(string(e,data+0x350c+80*i));
                Map<String,Object> result=row("pixels_sha256",sha(bytes(e,D+11,4096)),"state",state,"items",items,"lines",textLines);
                if(operation==0&&input.get("layout").getAsInt()==8)result.put("character",Arrays.asList(e.read(character+56,1),e.read(character+68,4),e.read(character+76,4),e.read(character+172,4),e.read(character+72,4),e.read(character+80,4),e.read(character+168,4),e.read(character+156,4)));
                if(operation==0&&input.get("layout").getAsInt()==12){result.put("quantity",Arrays.asList(e.read(quantity+96,1),e.read(quantity+100,4),e.read(quantity+104,4),e.read(quantity+97,1),e.read(quantity+76,4),e.read(quantity+64,4),e.read(quantity+60,4)));result.put("minimum",e.readDouble(quantity+112));result.put("step",e.readDouble(quantity+120));}
                if(operation==0&&(selectedLayout==0||selectedLayout==1||selectedLayout==14||selectedLayout==16))result.put("dynamic",Arrays.asList(e.read(W+0x2679,1),e.read(W+0x2674,4),e.read(W+0x1078,1),e.read(W+0x107c,4),e.read(W+0x2778,1),e.read(W+0x277c,4),e.read(W+0x2560,1),e.read(W+0x255c,4),e.read(W+0x1074,4),e.read(W+0x2530,4)));
                if(operation==6)result.put("diagnostic_line",e.read(W+0x66d0,1));
                results.add(result);caseIndex++;
            }
            for(String address:e.functions.keySet()){Function function=currentProgram.getFunctionManager().getFunctionAt(toAddr(Long.parseLong(address.substring(2),16)));if(function==null)throw new IllegalStateException("Executed body lacks defined bounds "+address);List<Object> ranges=new ArrayList<>();for(AddressRange range:function.getBody().getAddressRanges()){byte[] bytes=new byte[(int)range.getLength()];currentProgram.getMemory().getBytes(range.getMinAddress(),bytes);ranges.add(row("address",range.getMinAddress().getOffset(),"size",bytes.length,"sha256",sha(bytes)));}functions.add(row("address",address,"calls",e.functions.get(address),"ranges",ranges));}
            Map<String,Object> report=row("input_sha256",DisplayExecution.IMAGE_SHA,"cases",results,"original_functions",functions,"modeled_boundaries",e.boundaries,"scope","Original Head UI dispatcher through application menu presentations and real UiManagement framebuffer composition. Alarm servicing, SDK WiFi/IP/string/flash data, constructor fields, ROM primitives, compiler arithmetic, prescribed clock/log/delay and windowed ABI remain explicit boundaries","hardware_tested",false,"full_cpu_emulation",false);Files.writeString(Path.of(args[2]),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+results.size()+" original owned-screen framebuffer cases");
    }
}
