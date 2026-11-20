// Original console callbacks and helpers through return. Runtime/PicoStill queues
// and MSP command transmission are explicit capture boundaries, separately replayed.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.io.*;
import java.util.*;
import java.util.regex.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
public class EmulateApplicationCommands extends GhidraScript {
    private static final long OBJ=0x3ff10000L,STATE=0x3ff14000L,WRAPPER=0x3ff0f000L,ARG=0x3ff0f100L,TEXT=0x3ff15000L;
    private long clock;private boolean occupied;private List<Object> effects,value;
    private String text(Z119Execution e,long p)throws Exception {if(p==0)return "(null)";StringBuilder out=new StringBuilder();for(int i=0;i<4096;i++){int b=(int)e.read(p+i,1);if(b==0)return out.toString();out.append((char)b);}throw new IllegalStateException("Unterminated string");}
    private void text(Z119Execution e,long p,String s){e.emulator.writeMemory(e.address(p),(s+"\0").getBytes(StandardCharsets.ISO_8859_1));}
    private String format(Z119Execution e,String fmt)throws Exception {
        StringBuilder out=new StringBuilder();int word=0;
        Matcher matcher=Pattern.compile("%([-+ #0]*)([0-9]*)(\\.[0-9]+)?(l?)([sduxf])").matcher(fmt);int end=0;
        while(matcher.find()){out.append(fmt,end,matcher.start());String kind=matcher.group(5),spec=matcher.group();long raw;String result;
            if(kind.equals("f")){if((word&1)!=0)word++;double number=e.readDouble(e.register(1)+word*4);word+=2;result=String.format(Locale.ROOT,spec,number);}
            else{raw=e.read(e.register(1)+word++*4,4);if(kind.equals("s"))result=String.format(Locale.ROOT,spec,text(e,raw));
                else if(kind.equals("d"))result=String.format(Locale.ROOT,spec.replace("l",""),(int)raw);
                else if(kind.equals("x"))result=String.format(Locale.ROOT,spec.replace("l",""),raw);
                else result=String.format(Locale.ROOT,spec.replace("lu","d").replace("u","d"),raw);}
            out.append(result);end=matcher.end();}
        out.append(fmt.substring(end));if(out.indexOf("%")>=0)throw new IllegalStateException("Unmodeled format "+fmt);return out.toString();
    }
    private void log(Z119Execution e)throws Exception {
        String fmt=text(e,e.register(10));if(fmt.contains("[ASSERT]"))return;
        int marker=fmt.indexOf("[%s:%u] ");if(marker<0)throw new IllegalStateException("Unexpected callback log "+fmt);
        String prefix=fmt.substring(0,marker),body=fmt.substring(marker+8).replace("\u001b[0m","");if(body.endsWith("\r\n"))body=body.substring(0,body.length()-2);
        Matcher tag=Pattern.compile("\\]\\[([A-Z]+)\\]").matcher(prefix);if(!tag.find())throw new IllegalStateException("Missing tag");
        int level=fmt.contains("[W_]")?1:fmt.contains("[E_]")?2:fmt.contains("[D_]")?3:0;
        effects.add(List.of("log",level,tag.group(1),text(e,e.register(14)),e.register(15),format(e,body)));
    }
    private int atoi(String s){Matcher m=Pattern.compile("^[\\t\\n\\r\\f\\x0b ]*([+-]?[0-9]+)").matcher(s);if(!m.find())return 0;java.math.BigInteger n=new java.math.BigInteger(m.group(1));return n.max(java.math.BigInteger.valueOf(Integer.MIN_VALUE)).min(java.math.BigInteger.valueOf(Integer.MAX_VALUE)).intValue();}
    private void boundary(Z119Execution e,long target)throws Exception {
        if(e.arithmetic(target))return;
        switch((int)target){
        case 0x4012d48c:e.countBoundary("__truncdfsf2");e.register(10,Float.floatToRawIntBits((float)e.number(10)));return;
        case 0x400566c4:e.countBoundary("ROM_atoi_saturated_32bit");e.register(10,atoi(text(e,e.register(10))));return;
        case 0x400f4900:e.countBoundary("RuntimeCommand_mailbox_capture");if(!occupied){long p=e.register(11);value=List.of(true,e.read(p+4,4),e.read(p+8,4),Long.toUnsignedString(e.read(p+16,8)));}return;
        case 0x400f4ddc:e.countBoundary("PicoStillCommand_mailbox_capture");if(!occupied){long p=e.register(11);value=List.of(true,e.read(p+4,4),(int)e.read(p+8,4));}return;
        case 0x400dc2f8:e.countBoundary("MSP_command_u8_capture");effects.add(List.of("command",e.register(11)&255,e.register(12)&255,1));return;
        case 0x400dc0a4:e.countBoundary("MSP_command_empty_capture");effects.add(List.of("command",e.register(11)&255,0,0));return;
        case 0x400db014:effects.add(List.of("assertion",text(e,e.register(10)),e.register(11),text(e,e.register(12))));e.invoke(target);return;
        case 0x40081388:e.countBoundary("millis");e.register(10,clock);return;
        case 0x401289a0:e.countBoundary("assertion_delay_200ms");clock=(clock+e.register(10))&0xffffffffL;return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");String s=text(e,e.register(10));e.register(10,e.register(10)+Math.max(s.lastIndexOf('/'),s.lastIndexOf('\\'))+1);return;
        case 0x400d21cc:e.countBoundary("diagnostic_output");log(e);return;
        case 0x4000c44c:e.countBoundary("ROM_memset");for(int i=0;i<e.register(12);i++)e.write(e.register(10)+i,e.register(11),1);return;
        case 0x4000c2c8:e.countBoundary("ROM_memcpy");e.emulator.writeMemory(e.address(e.register(10)),e.emulator.readMemory(e.address(e.register(11)),(int)e.register(12)));return;
        default:if(target>=0x400d0000L&&target<0x40120000L){e.invoke(target);return;}throw new IllegalStateException(String.format("Unreviewed application callback call %08x",target));
        }
    }
    private Map<String,Object> one(Z119Execution e,JsonObject c)throws Exception {
        e.resetRegisters();e.clear(WRAPPER,0x8000);e.write(WRAPPER,OBJ,4);e.write(WRAPPER+8,ARG,4);text(e,ARG,c.get("arg").getAsString());
        clock=1000;occupied=c.has("occupied")&&c.get("occupied").getAsBoolean();effects=new ArrayList<>();value=new ArrayList<>();e.write(0x3ffc4654L,0x1195a5a5L,4);e.write(0x3ffc2e84L,7,4);
        String kind=c.get("kind").getAsString();boolean automatic=c.has("automatic")&&c.get("automatic").getAsBoolean();
        if(kind.equals("duty")){e.write(OBJ+8,STATE,4);e.write(OBJ+0x7c,automatic?1:0,1);}
        if(kind.equals("wifi")){e.write(OBJ+0x24,1,4);e.write(OBJ+0x28,TEXT,4);text(e,TEXT,"Wifi.h");}
        if(kind.equals("runtime"))value=occupied?List.of(true,10,0,"0"):List.of(false,0,0,"0");
        if(kind.equals("still"))value=occupied?List.of(true,8,-1):List.of(false,0,-1);
        if(kind.equals("safety")){e.write(OBJ+4,STATE,4);e.write(OBJ+0x10,1,1);for(int flag=3;flag<6;flag++)e.write(STATE+flag,1,1);
            int[] offsets={0x18,0x38,0x58};int[] flags={5,3,4};String[] names={"hexTempMonitor","hexWortDeltaMonitor","drainWortDeltaMonitor"};
            for(int i=0;i<3;i++){e.write(OBJ+offsets[i],TEXT+i*64,4);text(e,TEXT+i*64,names[i]);e.write(OBJ+offsets[i]+4,1,1);e.write(OBJ+offsets[i]+24,STATE+flags[i],4);}}
        if(kind.equals("t1")){e.write(OBJ+0x940,STATE,4);e.write(STATE+4,c.get("t1_enabled").getAsBoolean()?1:0,1);}
        long address=Long.decode(c.get("function").getAsString());if(currentProgram.getListing().getInstructionAt(toAddr(address))==null)disassemble(toAddr(address));
        e.register(10,WRAPPER);e.register(11,WRAPPER+8);e.invoke(address);
        if(kind.equals("duty"))value=List.of(e.readDouble(STATE+0x20),e.read(OBJ+0x7c,1)!=0);
        if(kind.equals("wifi"))value=List.of(e.read(OBJ+0x24,4));
        if(kind.equals("safety"))value=List.of(e.read(OBJ+0x10,1)!=0,e.read(STATE+6,1)!=0,e.read(STATE+5,1)!=0,e.read(STATE+3,1)!=0,e.read(STATE+4,1)!=0);
        if(kind.equals("t1"))value=List.of(e.read(STATE+4,1)!=0,e.readDouble(STATE+0x10));
        if(kind.equals("adc"))value=List.of(e.read(OBJ+0xc,1)!=0);
        if(kind.equals("session"))value=List.of(e.read(OBJ+0x20,1)!=0);
        Map<String,Object> result=new LinkedHashMap<>();result.put("id",c.get("id").getAsString());result.put("value",value);result.put("effects",effects);
        result.put("counter",e.read(0x3ffc2e84L,4));result.put("clock",clock);return result;
    }
    @Override public void run()throws Exception {
        String[] args=getScriptArgs();JsonArray cases=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))) {
            for(JsonElement c:cases){Map<String,Object> result=one(e,c.getAsJsonObject());out.write(new Gson().toJson(result)+"\n");out.flush();}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);report.put("input_file_sha256",HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(Path.of(args[0])))));report.put("cases",cases.size());report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
    }
}
