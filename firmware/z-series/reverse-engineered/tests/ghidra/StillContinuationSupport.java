// Exact diagnostic capture shared by full PicoStill/controller replays.
// Only the System renderer and SDK clock/delay calls are explicit boundaries.
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;
import com.google.gson.*;
import ghidra.program.model.listing.Program;
public final class StillContinuationSupport {
    public long clock,assertionLine;
    public final List<String> trace=new ArrayList<>();
    private String string(Z119Execution e,long p)throws Exception {
        if(p==0)return "(null)";
        int n=0;while(n<65536&&e.read(p+n,1)!=0)n++;
        if(n==65536)throw new IllegalStateException("Unbounded diagnostic string");
        return n==0?"":new String(e.emulator.readMemory(e.address(p),n),StandardCharsets.ISO_8859_1);
    }
    public void reset(Z119Execution e,long now,long counter,long previous){clock=now;assertionLine=previous;trace.clear();e.write(0x3ffc2e84L,counter,4);}
    public void service(int index){trace.add("service|"+index);}
    public boolean boundary(Z119Execution e,long target)throws Exception {
        switch((int)target){
        case 0x40081388:e.countBoundary("prescribed_millis");e.register(10,clock);return true;
        case 0x4008c2b4:{e.countBoundary("diagnostic_basename");String v=string(e,e.register(10));e.register(10,e.register(10)+Math.max(v.lastIndexOf('/'),v.lastIndexOf('\\'))+1);return true;}
        case 0x400d21cc:e.countBoundary("exact_diagnostic_output");log(e);return true;
        case 0x400db014:assertionLine=e.register(11);trace.add("assertion|"+string(e,e.register(10))+"|"+e.register(11)+"|"+string(e,e.register(12)));e.invoke(target);return true;
        case 0x401289a0:if(e.register(10)!=200)throw new IllegalStateException("Unexpected assertion delay");e.countBoundary("assertion_delay_200ms");clock=(clock+200)&0xffffffffL;return true;
        default:return false;
        }
    }
    private String basename(String value){return value.substring(Math.max(value.lastIndexOf('/'),value.lastIndexOf('\\'))+1);}
    private String registerFormat(Z119Execution e,String format,int word)throws Exception{
        java.util.regex.Matcher m=java.util.regex.Pattern.compile("%([-+ #0]*)([0-9]*)(?:\\.([0-9]+))?(l?)([sdxXcuf])").matcher(format);StringBuilder out=new StringBuilder();int position=0;
        while(m.find()){out.append(format,position,m.start());String value;char type=m.group(5).charAt(0);if(type=='f'&&(word&1)!=0)word++;long bits=argument(e,word);if(type=='f')bits|=argument(e,word+1)<<32;word+=type=='f'?2:1;
            if(type=='s')value=string(e,bits);else if(type=='d')value=Integer.toString((int)bits);else if(type=='u')value=Long.toString(bits&0xffffffffL);else if(type=='c')value=String.valueOf((char)bits);else if(type=='x'||type=='X'){value=Long.toHexString(bits&0xffffffffL);if(type=='X')value=value.toUpperCase(Locale.ROOT);}
            else{double number=Double.longBitsToDouble(bits);int precision=m.group(3)==null?6:Integer.parseInt(m.group(3));if(!Double.isFinite(number))throw new IllegalStateException("nonfinite format domain");value=new java.math.BigDecimal(Math.abs(number)).setScale(precision,java.math.RoundingMode.HALF_EVEN).toPlainString();if(bits<0)value="-"+value;else if(m.group(1).contains("+"))value="+"+value;else if(m.group(1).contains(" "))value=" "+value;}
            int width=m.group(2).isEmpty()?0:Integer.parseInt(m.group(2));boolean left=m.group(1).contains("-"),zero=m.group(1).contains("0")&&!left;
            while(value.length()<width){if(left)value+=" ";else if(zero&&value.startsWith("-"))value="-0"+value.substring(1);else value=(zero?"0":" ")+value;}
            out.append(value);position=m.end();}
        out.append(format.substring(position));return out.toString();
    }
    private void log(Z119Execution e)throws Exception{
        String fmt=string(e,e.register(10));if(fmt.contains("[ASSERT]"))return;
        if(fmt.startsWith("[T>]")||fmt.startsWith("[T_]")||(fmt.startsWith("[IV]")&&fmt.contains("[SYST]"))||(fmt.contains("[SYST]")&&e.register(15)==0x4f)){e.countBoundary("scope_renderer_owned_by_System_console_replay");return;}
        String rendered=registerFormat(e,fmt,11).replaceAll("\\x1b\\[[0-9;]*m","");
        if(rendered.endsWith("\r\n"))rendered=rendered.substring(0,rendered.length()-2);
        java.util.regex.Matcher prefix=java.util.regex.Pattern.compile("^\\[([^]]+)\\]\\[[^]]+\\]\\[([^]]+)\\](?:\\[([^]]+):([0-9]+)\\])? (.*)",java.util.regex.Pattern.DOTALL).matcher(rendered);
        if(!prefix.matches())throw new IllegalStateException("Unexpected diagnostic log "+fmt);
        String label=prefix.group(1);int level=label.equals("W_")?1:label.equals("E_")?2:label.equals("D_")?3:0;
        trace.add("log|"+level+"|"+prefix.group(2)+"|"+(prefix.group(3)==null?"":prefix.group(3))+"|"+(prefix.group(4)==null?"0":prefix.group(4))+"|"+prefix.group(5));
    }
    private long argument(Z119Execution e,int index)throws Exception{return index<16?e.register(index):e.read(e.register(1)+(index-16)*4,4);}
    public void finish(Map<String,Object> out,Z119Execution e)throws Exception{out.put("trace",new ArrayList<>(trace));out.put("clock",clock);out.put("counter",e.read(0x3ffc2e84L,4));}
    public static void validateImage(Program program,Path layoutPath)throws Exception {
        JsonObject layout=JsonParser.parseString(Files.readString(layoutPath)).getAsJsonObject();byte[] original=Files.readAllBytes(Path.of(layout.get("input_path").getAsString()));
        String digest=HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(original));
        if(!digest.equals(Z119Execution.IMAGE_SHA))throw new IllegalStateException("Original image mismatch");
        for(JsonElement value:layout.getAsJsonArray("segments")){JsonObject part=value.getAsJsonObject();int size=part.get("size").getAsInt(),offset=part.get("offset").getAsInt();if(size==0)continue;byte[] mapped=new byte[size];program.getMemory().getBytes(program.getAddressFactory().getDefaultAddressSpace().getAddress(part.get("address").getAsLong()),mapped);if(!Arrays.equals(mapped,Arrays.copyOfRange(original,offset,offset+size)))throw new IllegalStateException("Program bytes differ from preserved OEM image");}
    }
}
