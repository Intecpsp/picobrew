// Execute original Shell, SerialReader and TextCommand queue bodies through return.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.io.*;
import java.util.*;
import java.util.regex.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulateConsole extends GhidraScript {
    private static final long ROOT=0x3ff30000L,CHILD=0x3ff31000L,READER=0x3ff32000L,
        QUEUE=0x3ff34000L,COMMAND=0x3ff36000L,STREAM=0x3ff37000L,VTABLE=0x3ff38000L,FUNCTOR=0x3ff39000L;
    private long heap,now;
    private int position;
    private byte[] stream;
    private List<String> events,texts;
    private String string(Z119Execution e,long address)throws Exception {
        if(address==0)return null;
        StringBuilder s=new StringBuilder();
        for(int i=0;i<4096;i++){int c=(int)e.read(address+i,1);if(c==0)return s.toString();s.append((char)c);}
        throw new IllegalStateException("Unterminated console string");
    }
    private void writeString(Z119Execution e,long address,String value) {
        byte[] bytes=(value+"\0").getBytes(StandardCharsets.ISO_8859_1);e.emulator.writeMemory(e.address(address),bytes);
    }
    private long allocate(Z119Execution e,String value) {
        if(value==null)return 0;long pointer=heap;heap+=value.length()+8;writeString(e,pointer,value);return pointer;
    }
    private String nullable(JsonObject object,String key){JsonElement v=object.get(key);return v.isJsonNull()?null:v.getAsString();}
    private String format(Z119Execution e,String format,List<Long> arguments)throws Exception {
        StringBuilder result=new StringBuilder();int next=0;
        for(int i=0;i<format.length();i++) {
            char c=format.charAt(i);if(c!='%'){result.append(c);continue;}
            if(format.charAt(i+1)=='%'){result.append('%');i++;continue;}
            int start=i++;
            while(i<format.length() && "-+ .0123456789".indexOf(format.charAt(i))>=0)i++;
            char type=format.charAt(i);long value=arguments.get(next++);String rendered;
            if(type=='s')rendered=Objects.toString(string(e,value),"(null)");
            else if(type=='d')rendered=Integer.toString((int)value);
            else if(type=='u')rendered=Long.toString(value&0xffffffffL);
            else throw new IllegalStateException("Unmodeled format "+format.substring(start,i+1));
            String spec=format.substring(start+1,i);boolean left=spec.startsWith("-");
            if(!spec.isEmpty()) {
                int width=Integer.parseInt(left?spec.substring(1):spec);
                String padding=" ".repeat(Math.max(0,width-rendered.length()));rendered=left?rendered+padding:padding+rendered;
            }
            result.append(rendered);
        }
        return result.toString();
    }
    private void diagnostic(Z119Execution e)throws Exception {
        String format=string(e,e.register(10));String file=string(e,e.register(14));long line=e.register(15);
        if(format.contains("[ASSERT]")) {
            events.add("A|"+file+"|"+line+"|"+string(e,e.read(e.register(1),4))+"|"+
                e.register(12)+"|"+e.register(13)+"|"+e.read(0x3ffc2e84L,4));return;
        }
        int marker=format.indexOf("[%s:%u] ");if(marker<0)throw new IllegalStateException("Unexpected console log "+format);
        String prefix=format.substring(0,marker),body=format.substring(marker+8);
        body=body.replace("\u001b[0m","");if(body.endsWith("\r\n"))body=body.substring(0,body.length()-2);
        Matcher tag=Pattern.compile("\\]\\[([A-Z]+)\\]").matcher(prefix);if(!tag.find())throw new IllegalStateException("No log tag");
        int level=format.contains("[W_]")?1:format.contains("[E_]")?2:format.contains("[D_]")?3:0;
        List<Long> args=new ArrayList<>();for(int i=0;i<8;i++)args.add(e.read(e.register(1)+4*i,4));
        events.add("L|"+level+"|"+tag.group(1)+"|"+file+"|"+line+"|"+format(e,body,args));
    }
    private void copy(Z119Execution e,long to,long from,int bytes)throws Exception {
        e.emulator.writeMemory(e.address(to),e.emulator.readMemory(e.address(from),bytes));
    }
    private void boundary(Z119Execution e,long target)throws Exception {
        if(e.arithmetic(target))return;
        switch((int)target) {
        case 0x400f6b18:case 0x400efd40:case 0x4011005c:case 0x400d4e10:
        case 0x400d598c:case 0x400d585c:case 0x400db014:case 0x400fa21c:
        case 0x400ef118:case 0x400fa1f8:case 0x400f944c:case 0x400f9348:case 0x400f9310:
        case 0x400f08e8:case 0x400d0860:e.invoke(target);return;
        case 0x400d0e60:
            e.countBoundary("CString_exact_match");e.register(10,Objects.equals(string(e,e.register(10)),string(e,e.register(11)))?1:0);return;
        case 0x400d0e90:
            e.countBoundary("CString_prefix_match");
            String input=string(e,e.register(10)),prefix=string(e,e.register(11));
            e.register(10,prefix==null||(input==null?prefix.isEmpty():input.startsWith(prefix))?1:0);return;
        case 0x401bf3d4:
            e.countBoundary("CString_isNullOrEmpty");String value=string(e,e.register(10));e.register(10,value==null||value.isEmpty()?1:0);return;
        case 0x400014c0:e.countBoundary("ROM_strlen");e.register(10,string(e,e.register(10)).length());return;
        case 0x400015d4:
            e.countBoundary("ROM_strncpy");long dest=e.register(10),src=e.register(11);int count=(int)e.register(12);
            String source=string(e,src);for(int i=0;i<count;i++)e.write(dest+i,i<source.length()?source.charAt(i):0,1);return;
        case 0x4000c2c8:e.countBoundary("ROM_memcpy");copy(e,e.register(10),e.register(11),(int)e.register(12));return;
        case 0x4000c44c:
            e.countBoundary("ROM_memset");for(long i=0;i<e.register(12);i++)e.write(e.register(10)+i,e.register(11),1);return;
        case 0x400f6574:e.countBoundary("Cxx_function_copy");copy(e,e.register(10),e.register(11),16);return;
        case 0x401bf35c:e.countBoundary("Cxx_empty_function");e.clear(e.register(10),16);return;
        case 0x4010f6a0:
            e.countBoundary("console_callback");events.add("C|"+e.read(e.register(10),4)+"|"+string(e,e.register(11)));return;
        case 0x401bf244:e.countBoundary("Shell_name");e.register(10,e.read(e.register(10)+4,4));return;
        case 0x4013cd98:
            e.countBoundary("snprintf");
            List<Long> args=new ArrayList<>();for(int i=13;i<16;i++)args.add(e.register(i));
            for(int i=0;i<8;i++)args.add(e.read(e.register(1)+4*i,4));
            String formatted=format(e,string(e,e.register(12)),args);int capacity=(int)e.register(11);
            if(capacity>0)writeString(e,e.register(10),formatted.substring(0,Math.min(formatted.length(),capacity-1)));
            e.register(10,formatted.length());return;
        case 0x4008c2b4:
            e.countBoundary("diagnostic_basename");String path=string(e,e.register(10));
            e.register(10,e.register(10)+Math.max(path.lastIndexOf('\\'),path.lastIndexOf('/'))+1);return;
        case 0x40081388:e.countBoundary("millis");e.register(10,now);return;
        case 0x400d21cc:e.countBoundary("formatted_log");diagnostic(e);return;
        case 0x401289a0:
            e.countBoundary("delay");events.add("D|"+e.register(10)+"|"+e.read(0x3ffc2e84L,4));now=(now+e.register(10))&0xffffffffL;return;
        case 0x70000100:e.countBoundary("Stream_available");e.register(10,stream.length-position);return;
        case 0x70000104:e.countBoundary("Stream_read");e.register(10,stream[position++]&255);return;
        default:throw new IllegalStateException(String.format("Unreviewed console call %08x",target));
        }
    }
    private void shell(Z119Execution e,long pointer,String name)throws Exception {
        e.write(pointer,0x3f41f9c4L,4);e.write(pointer+4,allocate(e,name),4);
    }
    private void add(Z119Execution e,long shell,boolean exact,String token,String description,int id)throws Exception {
        e.clear(FUNCTOR,16);e.write(FUNCTOR,id,4);
        e.register(10,shell);e.register(11,exact?1:0);e.register(12,allocate(e,token));
        e.register(13,allocate(e,description));e.register(14,FUNCTOR);e.invoke(0x400f6b18L);
    }
    private void child(Z119Execution e,long parent,String token,long child)throws Exception {
        e.register(10,parent);e.register(11,allocate(e,token));e.register(12,child);e.invoke(0x400efd40L);
    }
    private void commands(Z119Execution e,int variant)throws Exception {
        if(variant==0)return;
        add(e,CHILD,true,"run","Run child",5);add(e,CHILD,false,"value ","Set child value",6);
        add(e,ROOT,true,"status","Read status",1);add(e,ROOT,false,"set ","Set a value",2);
        add(e,ROOT,true,"set","Set without arguments",3);child(e,ROOT,"child ",CHILD);
        add(e,ROOT,false,"st","Overlapping prefix",4);
        if(variant==2){add(e,ROOT,true,"?","Override help",7);add(e,ROOT,false,"??","Override recursive help",8);}
    }
    private void buffer(Z119Execution e,long pointer,String text)throws Exception {
        e.clear(pointer,0x90);writeString(e,pointer,text);e.write(pointer+0x84,text.length(),4);e.write(pointer+0x88,128-text.length(),4);
    }
    private Map<String,Object> one(Z119Execution e,JsonObject item)throws Exception {
        long[] data=new long[10],output=new long[12];for(int i=0;i<10;i++)data[i]=item.getAsJsonArray("data").get(i).getAsLong();
        String token=nullable(item,"token"),description=nullable(item,"description"),input=nullable(item,"input");
        JsonArray bytes=item.getAsJsonArray("stream");stream=new byte[bytes.size()];for(int i=0;i<stream.length;i++)stream[i]=(byte)bytes.get(i).getAsInt();
        now=data[4];position=0;heap=0x3ff40000L;events=new ArrayList<>();texts=new ArrayList<>();
        e.resetRegisters();e.clear(ROOT,0x11000);e.write(0x3ffc4654L,0x1195a5a5L,4);e.write(0x3ffc2e84L,data[5],4);
        shell(e,ROOT,"Root");shell(e,CHILD,"Child");
        if(data[0]<2) {
            for(int i=0;i<data[1];i++)add(e,ROOT,true,"fill","Filled",0);
            long before=e.read(ROOT+0x108,4);
            if(data[0]==0)add(e,ROOT,data[2]!=0,token,description,9);else child(e,ROOT,token,data[3]!=0?0:CHILD);
            output[0]=e.read(ROOT+0x108,4)>before?0:1;
            for(int i=0;i<e.read(ROOT+0x108,4);i++) {
                long entry=ROOT+8+i*32,sub=e.read(entry+20,4);
                texts.add(string(e,e.read(entry,4))+"|"+Objects.toString(string(e,e.read(entry+24,4)),"<null>")+"|"+
                    e.read(entry+28,1)+"|"+(sub==0?"":string(e,e.read(sub+4,4))));
            }
        } else if(data[0]<4) {
            commands(e,(int)data[1]);e.register(10,ROOT);
            if(data[0]==2){e.register(11,allocate(e,input));e.invoke(0x4011005cL);}
            else{e.register(11,data[2]);e.register(12,0);e.register(13,allocate(e,token));e.invoke(0x400d4e10L);}
        } else {
            e.write(QUEUE,allocate(e,"TextCommands"),4);e.write(QUEUE+4,0x3f41fc00L,4);
            buffer(e,QUEUE+8,description==null?"":description);e.write(QUEUE+0x98,data[7],1);
            String previous=description==null?"":description;writeString(e,QUEUE+0x99,previous.substring(0,Math.min(127,previous.length())));
            if(data[0]==4) {
                e.write(READER+4,QUEUE,4);e.write(READER+8,STREAM,4);e.write(STREAM,VTABLE,4);
                e.write(VTABLE+0x10,0x70000100L,4);e.write(VTABLE+0x14,0x70000104L,4);
                buffer(e,READER+0xc,token==null?"":token);e.write(READER+0x99,data[2],1);
                e.write(READER+0x98,data[9],1);e.write(READER+0x9c,data[8],1);e.write(READER+0xa0,data[6],4);
                for(int i=0;i<data[3];i++){e.register(10,READER);e.invoke(0x400fa21cL);}
                output[4]=position;output[5]=e.read(READER+0x90,4);output[6]=e.read(READER+0x94,4);
                output[8]=e.read(READER+0x99,1);output[9]=e.read(READER+0x98,1);output[10]=e.read(READER+0x9c,1);
                output[11]=e.read(READER+0xa0,4);texts.add(string(e,READER+0xc));
            } else {
                buffer(e,COMMAND+4,input);e.write(COMMAND,0x3f41fc00L,4);
                e.register(10,QUEUE);e.register(11,COMMAND);e.invoke(0x400f08e8L);output[0]=data[7]!=0?1:0;
            }
            output[7]=e.read(QUEUE+0x98,1);texts.add(string(e,QUEUE+8));
        }
        output[1]=now;output[2]=e.read(0x3ffc2e84L,4);output[3]=e.read(ROOT+0x108,4);
        Map<String,Object> result=new LinkedHashMap<>();result.put("output",output);result.put("events",events);result.put("texts",texts);return result;
    }
    @Override public void run()throws Exception {
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Input JSON and output JSONL required");
        for(long entry:new long[]{0x4011005cL,0x400d4e10L,0x400d0860L})disassemble(currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(entry));
        byte[] input=Files.readAllBytes(Path.of(args[0]));JsonArray cases=JsonParser.parseString(new String(input,StandardCharsets.UTF_8)).getAsJsonArray();Gson gson=new Gson();
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter output=Files.newBufferedWriter(Path.of(args[1]))) {
            int index=0;for(JsonElement item:cases){try{output.write(gson.toJson(one(e,item.getAsJsonObject()))+"\n");}
                catch(Exception failure){throw new IllegalStateException("Console case "+index,failure);}index++;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);
            report.put("input_file_sha256",HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(input)));
            report.put("cases",index);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original Shell command/child registration, dispatch, full recursive help, CString rejection-copy diagnostics, SerialReader, text construction, one-slot enqueue/drop and returning assertion instructions. ROM memory/string primitives, CString matching/null tests, C++ callable storage/invocation, stream I/O, formatting, float helpers, clock/delay and single-thread memory ordering are explicit boundaries. Compares complete ordered messages/callback arguments, retained text/queue state, input consumption, assertion count/timestamp bits and wraparound. Null callable/input and cyclic/deeper-than-63 shell graphs are explicit reconstruction guards outside original-equivalence cases. No RTOS/serial hardware or whole-application claim.");
            Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+cases.size()+" original console cases");
    }
}
