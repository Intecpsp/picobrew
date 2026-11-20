// Original SerialLogger, bounded string and all five Core CSV formatter bodies.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.io.*;
import java.math.*;
import java.util.*;
import java.util.regex.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
public class EmulateSerialLogger extends GhidraScript {
    private static final long OBJECT=0x3ff30000L,CORE=0x3ff34000L,TEXT=0x3ff38000L,VTABLE=0x3ff3d000L;
    private JsonObject input;private long clock;private List<Object> effects,assertions,entries;private String name;
    private final Set<Long> bodies=Set.of(0x400efa1cL,0x400efa4cL,0x400efb04L,0x400efb34L,0x400f5d08L,0x400f6064L,0x400f5b40L,0x400f5b78L,0x400f5c84L,0x400f5d60L,0x400f5e40L,0x400f5e74L,0x400f5f40L,0x400f5f94L,0x400f5fccL,0x400db5c4L,0x400d1340L,0x400ef7e4L);
    private void write(Z119Execution e,long p,String value){e.emulator.writeMemory(e.address(p),(value+"\0").getBytes(StandardCharsets.UTF_8));}
    private String text(Z119Execution e,long p)throws Exception{if(p==0)return null;ByteArrayOutputStream b=new ByteArrayOutputStream();for(int i=0;i<20000;i++){int c=(int)e.read(p+i,1);if(c==0)return b.toString(StandardCharsets.UTF_8);b.write(c);}throw new IllegalStateException("Unterminated");}
    private void event(int kind,String text,long value){effects.add(List.of(kind,text==null?"":text,value));}
    private long integer(JsonObject o,String key,long fallback){return o.has(key)?o.get(key).getAsLong():fallback;}
    private boolean flag(String key){return input.has(key)&&input.get(key).getAsBoolean();}
    private String string(String key,String fallback){return input.has(key)&&!input.get(key).isJsonNull()?input.get(key).getAsString():fallback;}
    private long argument(Z119Execution e,long stack,long saved,int[] index,int bytes)throws Exception{
        int aligned=(index[0]+bytes-1)&-bytes,next=aligned+bytes;long base;
        if(next<=24)base=saved;else{if(aligned<=24)next=32+bytes;base=stack;}index[0]=next;return e.read(base+next-bytes,bytes);
    }
    private String format(Z119Execution e,String format,long stack,long saved,int index)throws Exception{
        Matcher m=Pattern.compile("%([0-9]*)(?:\\.([0-9]+))?([sdxf])").matcher(format);StringBuilder out=new StringBuilder();int position=0;int[] next={index};
        while(m.find()){out.append(format,position,m.start());String value;char type=m.group(3).charAt(0);long bits=argument(e,stack,saved,next,type=='f'?8:4);
            if(type=='s')value=Objects.toString(text(e,bits),"(null)");else if(type=='d')value=Integer.toString((int)bits);else if(type=='x')value=Long.toHexString(bits&0xffffffffL);
            else{double number=Double.longBitsToDouble(bits);int precision=m.group(2)==null?6:Integer.parseInt(m.group(2));if(Double.isNaN(number))value="nan";else if(Double.isInfinite(number))value=number<0?"-inf":"inf";else{value=new BigDecimal(Math.abs(number)).setScale(precision,RoundingMode.HALF_EVEN).toPlainString();if(bits<0)value="-"+value;}}
            int width=m.group(1).isEmpty()?0:Integer.parseInt(m.group(1));if(value.length()<width)out.append(" ".repeat(width-value.length()));out.append(value);position=m.end();}
        out.append(format.substring(position));return out.toString();
    }
    private void boundary(Z119Execution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        if(bodies.contains(target)){e.invoke(target);return;}
        switch((int)target){
        case 0x400db014:assertions.add(List.of(text(e,e.register(10)),e.register(11),text(e,e.register(12))));e.invoke(target);return;
        case 0x40081388:e.countBoundary("millis");e.register(10,clock);return;
        case 0x401289a0:e.countBoundary("assertion_delay_200ms");effects.add(List.of("delay",e.register(10)));clock=(clock+e.register(10))&0xffffffffL;return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");String path=text(e,e.register(10));e.register(10,e.register(10)+Math.max(path.lastIndexOf('\\'),path.lastIndexOf('/'))+1);return;
        case 0x400d21cc:
            e.countBoundary("formatted_diagnostic_sink");String f=text(e,e.register(10));if(f.contains("[ASSERT]"))return;
            if(f.contains("CSVHDR"))event(0,text(e,e.register(14)),0);
            else if(f.contains("[CSTA]"))event(1,text(e,e.read(e.register(1),4)),0);
            else if(f.contains("_isLogging"))event(2,text(e,e.register(14)),text(e,e.register(14)).equals("True")?1:0);
            else if(f.contains("_intervalMs"))event(3,"",e.register(14));
            else if(f.contains("Buffer truncated"))event(4,"",e.register(15));
            else if(f.contains("Dropping"))event(5,"",e.read(e.register(1),4));
            else if(f.contains("Interval below"))event(6,"",0x75);
            else throw new IllegalStateException("Unreviewed diagnostic "+f);return;
        case 0x4000c44c:e.countBoundary("ROM_memset");byte[] fill=new byte[(int)e.register(12)];Arrays.fill(fill,(byte)e.register(11));e.emulator.writeMemory(e.address(e.register(10)),fill);return;
        case 0x4000c2c8:e.countBoundary("ROM_memcpy");if(e.register(12)>0)e.emulator.writeMemory(e.address(e.register(10)),e.emulator.readMemory(e.address(e.register(11)),(int)e.register(12)));return;
        case 0x400014c0:e.countBoundary("ROM_strlen");e.register(10,text(e,e.register(10)).getBytes(StandardCharsets.UTF_8).length);return;
        case 0x401462c4:e.countBoundary("SDK_vsnprintf_Xtensa_va_list");String formatted=format(e,text(e,e.register(12)),e.register(13),e.register(14),(int)e.register(15));int capacity=(int)e.register(11);if(capacity>0)write(e,e.register(10),formatted.substring(0,Math.min(formatted.length(),capacity-1)));e.register(10,formatted.length());return;
        case 0x4013b394:e.countBoundary("libm_pow");e.number(10,Math.pow(e.number(10),e.number(12)));return;
        case 0x4013b678:e.countBoundary("libm_sqrt");e.number(10,Math.sqrt(e.number(10)));return;
        case 0x400566c4:e.countBoundary("ROM_atoi_int32");Matcher number=Pattern.compile("^[\\t\\n\\r\\f\\u000b ]*([+-]?[0-9]+)").matcher(text(e,e.register(10)));long value=0;if(number.find()){BigInteger parsed=new BigInteger(number.group(1));value=parsed.max(BigInteger.valueOf(Integer.MIN_VALUE)).min(BigInteger.valueOf(Integer.MAX_VALUE)).longValue();}e.register(10,value);return;
        case 0x70000100:case 0x70000104:e.countBoundary("fixture_CSV_provider");e.register(10,e.register(11));write(e,TEXT,string("fill","fixture"));if(flag("formatted")){e.register(11,TEXT+0x2000);write(e,TEXT+0x2000,"%s");e.register(12,TEXT);e.invoke(0x400f5d60L);}else{e.register(11,TEXT);e.invoke(0x400f5c84L);}return;
        case 0x400f0520:e.countBoundary("Shell_factory");name=text(e,e.register(11));e.write(e.register(10),VTABLE+0x100,4);return;
        case 0x400fab68:e.countBoundary("Shell_ownership_move");e.write(e.register(10),e.read(e.register(11),4),4);e.write(e.register(11),0,4);return;
        case 0x400fa91c:case 0x401bf3c0:e.countBoundary("Cxx_wrapper_destruction");return;
        case 0x400f75dc:e.countBoundary("Shell_registration");entries.add(List.of(e.register(11),text(e,e.register(12)),text(e,e.register(13))));return;
        default:throw new IllegalStateException(String.format("Unreviewed SerialLogger call %08x",target));
        }
    }
    private Map<String,Object> one(Z119Execution e,JsonObject in)throws Exception{
        input=in;effects=new ArrayList<>();assertions=new ArrayList<>();entries=new ArrayList<>();name="";clock=integer(in,"clock",1000);e.resetRegisters();e.clear(OBJECT,0xf000);e.write(0x3ffc4654L,0x5a5a5a5aL,4);e.write(0x3ffc2e84L,integer(in,"counter",7),4);
        e.write(OBJECT+4,integer(in,"minimum",200),4);e.write(OBJECT+0xc,CORE,4);e.write(OBJECT+0x10,integer(in,"deadline",0),4);e.write(OBJECT+0x14,flag("enabled")?1:0,1);e.write(OBJECT+0x15,flag("ever")?1:0,1);e.write(OBJECT+0x18,integer(in,"interval",1000),4);e.write(CORE,VTABLE,4);e.write(VTABLE+8,flag("fixture")?0x70000100L:0x400f5d08L,4);e.write(VTABLE+12,flag("fixture")?0x70000104L:0x400f6064L,4);
        long buffer=OBJECT+0x1c;e.write(buffer+0x188,384,4);String prefix=string("prefix","");write(e,buffer,prefix);e.write(buffer+0x184,prefix.length(),4);e.write(buffer+0x188,384-prefix.length(),4);e.write(buffer+0x18c,flag("truncated")?1:0,1);e.write(buffer+0x18d,flag("frozen")?1:0,1);
        if(in.has("d")){JsonArray d=in.getAsJsonArray("d"),w=in.getAsJsonArray("w");long heater=CORE+0x2a8,t=CORE+0x528,m=CORE+0x2d8,rt=CORE+0x2f0,still=CORE+0x940;
            e.writeDouble(heater+8,d.get(0).getAsDouble());e.writeDouble(heater+0x20,d.get(1).getAsDouble());e.writeDouble(heater+0x28,d.get(2).getAsDouble());e.write(heater+0x19,w.get(0).getAsLong(),1);
            int[] offsets={8,0x60,0x10,0x68,0x30,0x20,0x40,0x50,0x250};for(int i=0;i<offsets.length;i++)e.writeDouble(t+offsets[i],d.get(i+3).getAsDouble());e.write(t+0x25c,Float.floatToRawIntBits(d.get(12).getAsFloat()),4);e.writeDouble(t+0x278,d.get(13).getAsDouble());
            for(int i=0;i<5;i++)e.writeDouble(still+0x18+8*i,d.get(14+i).getAsDouble());int[] stats={0x480,0x220,0x350};for(int i=0;i<3;i++){e.write(still+stats[i]+0xf0,w.get(13+i).getAsLong(),4);e.writeDouble(still+stats[i]+0x108,d.get(19+i).getAsDouble());}
            e.write(m+0x10,w.get(1).getAsLong(),4);int[] mo={10,9,11,20,12,13};for(int i=0;i<6;i++)e.write(m+mo[i],w.get(i+2).getAsLong(),1);int[] ro={0xb0,0x8c,0x7c,0x80,0x10};for(int i=0;i<5;i++)e.write(rt+ro[i],w.get(i+8).getAsLong(),4);write(e,TEXT+0x3000,string("step","Mash"));e.write(rt+0x14,TEXT+0x3000,4);
        }
        String op=in.get("op").getAsString();e.register(10,OBJECT);
        if(op.equals("header"))e.invoke(0x400efa4cL);else if(op.equals("data"))e.invoke(0x400efb04L);else if(op.equals("out"))e.invoke(0x400efb34L);else if(op.equals("info"))e.invoke(0x400efb94L);
        else if(op.equals("enable")){e.register(11,flag("value")?1:0);e.invoke(0x400efaacL);}else if(op.equals("tick")){e.register(11,integer(in,"now",0));e.invoke(0x400efcfcL);}
        else if(op.equals("interval")){write(e,TEXT,string("argument",""));e.write(VTABLE+0x80,OBJECT,4);e.write(VTABLE+0x84,TEXT,4);e.register(10,VTABLE+0x80);e.register(11,VTABLE+0x84);e.invoke(0x400d76ecL);}
        else if(op.equals("append")||op.equals("format")){write(e,TEXT,string("argument",""));e.register(10,buffer);if(op.equals("append")){e.register(11,TEXT);e.invoke(0x400f5c84L);}else{write(e,TEXT+0x2000,"%s");e.register(11,flag("null_format")?0:TEXT+0x2000);e.register(12,TEXT);e.invoke(0x400f5d60L);}}
        else if(op.equals("console"))e.invoke(0x400fae40L);else throw new IllegalArgumentException(op);
        Map<String,Object> result=new LinkedHashMap<>();result.put("id",in.get("id").getAsString());result.put("effects",effects);result.put("assertions",assertions);result.put("text",text(e,buffer));result.put("length",e.read(buffer+0x184,4));result.put("truncated",e.read(buffer+0x18c,1));result.put("frozen",e.read(buffer+0x18d,1));result.put("enabled",e.read(OBJECT+0x14,1));result.put("ever",e.read(OBJECT+0x15,1));result.put("deadline",e.read(OBJECT+0x10,4));result.put("interval",e.read(OBJECT+0x18,4));result.put("clock",clock);result.put("counter",e.read(0x3ffc2e84L,4));if(op.equals("console")){result.put("name",name);result.put("entries",entries);}return result;
    }
    @Override public void run()throws Exception{for(long a:new long[]{0x400f5d08L,0x400d76ecL})if(currentProgram.getListing().getInstructionAt(toAddr(a))==null)disassemble(toAddr(a));String[] args=getScriptArgs();JsonArray cases=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson gson=new Gson();
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){int count=0;for(JsonElement c:cases){try{out.write(gson.toJson(one(e,c.getAsJsonObject()))+"\n");}catch(Exception failure){throw new IllegalStateException("SerialLogger case "+count,failure);}++count;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);report.put("input_file_sha256",HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(Path.of(args[0])))));report.put("cases",count);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");}
    }
}
