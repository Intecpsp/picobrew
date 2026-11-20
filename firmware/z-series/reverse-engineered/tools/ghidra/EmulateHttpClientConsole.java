// Original live HTTP self-test, real Stream/TestScope and returning assertions.
// Secret-bearing formatted/request bytes are hashed before leaving this process.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.regex.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
public class EmulateHttpClientConsole extends GhidraScript {
    private static final long CORE=0x3ff20000L,SOCKET=0x3ff21000L,ERRNO=0x3ff25f00L;
    private static final Set<Long> ORIGINAL=Set.of(0x400d0e90L,0x400d149cL,0x400d14ccL,0x400d14e0L,0x400d561cL,0x400d5664L,0x400d585cL,0x400d59e0L,0x400db014L,0x400dacb8L,0x400db108L,0x400db188L,0x400db2c8L,0x400db324L,0x400db3b4L,0x400e5b98L,0x400e5cd8L,0x400e5d38L,0x400e5d5cL,0x400ee998L,0x400ef160L,0x400efe1cL,0x400eff2cL,0x400eff54L,0x400eff84L,0x400f005cL,0x400f0088L,0x400f0124L,0x400f8190L,0x400f81c4L,0x400f88c4L,0x400f89f8L,0x400f8a74L,0x400f8f9cL,0x400f912cL,0x400f9238L,0x401007f0L,0x40100830L,0x40100938L,0x40100954L,0x40101bc4L,0x4010c4dcL,0x4010c614L,0x4010c630L,0x4010c65cL,0x4010c7ecL,0x4010ca44L,0x40110cf0L,0x40110e2cL,0x401bf278L,0x401bf3d4L,0x401bf3ecL,0x401c0018L);
    private long[] input;private long now,http;private int position,acquires,scope,heaps,writes,waters,yields,watchdogs,testDepth,scopeDepth,connects;
    private boolean sending;private byte[] response,wire;
    private final List<List<Object>> events=new ArrayList<>();
    private static String sha(byte[] bytes)throws Exception{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));}
    private String string(Z119Execution e,long p)throws Exception{
        if(p==0)return "";StringBuilder out=new StringBuilder();
        for(int i=0;i<4096;++i){int b=(int)e.read(p+i,1);if(b==0)return out.toString();out.append((char)b);}
        throw new IllegalStateException("Unterminated original diagnostic string at "+Long.toHexString(p));
    }
    private byte[] encoded(String value){return value.getBytes(java.nio.charset.StandardCharsets.ISO_8859_1);}
    private void event(long kind,long a,long b,String text,byte[] data)throws Exception{
        byte[] value=encoded(text);events.add(List.of(kind,a&0xffffffffL,b&0xffffffffL,(long)value.length,sha(value),(long)data.length,sha(data)));
    }
    private void event(long kind)throws Exception{event(kind,0,0,"",new byte[0]);}
    private long word(Z119Execution e,int index)throws Exception{return index<16?e.register(index):e.read(e.register(1)+4L*(index-16),4);}
    private void diagnostic(Z119Execution e)throws Exception{
        String pattern=string(e,e.register(10));StringBuilder out=new StringBuilder();int argument=11;byte[] timestamp=new byte[0];
        for(int i=0;i<pattern.length();++i){
            char ch=pattern.charAt(i);if(ch!='%'){out.append(ch);continue;}
            if(++i>=pattern.length())throw new IllegalStateException("Truncated original format");
            if(pattern.charAt(i)=='%'){out.append('%');continue;}
            int first=i;while(i<pattern.length()&&"-+ #0.123456789hlz".indexOf(pattern.charAt(i))>=0)++i;
            if(i==pattern.length())throw new IllegalStateException("Invalid original format");
            String flags=pattern.substring(first,i);ch=pattern.charAt(i);
            if(ch=='f'){
                if((argument&1)!=0)++argument;
                long bits=word(e,argument)|(word(e,argument+1)<<32);argument+=2;timestamp=new byte[8];for(int j=0;j<8;++j)timestamp[j]=(byte)(bits>>>(j*8));out.append('@');continue;
            }
            long value=word(e,argument++);String rendered;
            switch(ch){case 's':rendered=string(e,value);break;case 'c':rendered=Character.toString((char)value);break;
            case 'd':case 'i':rendered=Integer.toString((int)value);break;case 'u':rendered=Long.toUnsignedString(value);break;
            case 'x':rendered=Long.toHexString(value);break;default:throw new IllegalStateException("Unsupported format code "+(int)ch);}
            String width=flags.replaceAll("[^0-9]","");if(!width.isEmpty()&&flags.indexOf('.')<0){int n=Integer.parseInt(width);char pad=flags.startsWith("0")?'0':' ';while(rendered.length()<n)rendered=pad+rendered;}
            out.append(rendered);
        }
        String message=out.toString().replaceAll("\u001b\\[[0-9;]*m","").replace("\r\n","");
        boolean assertion=message.contains("[ASSERT]");event(assertion?16:18,assertion?e.read(0x3ffc2e84L,4):0,0,message,timestamp);
    }
    private long decimal(String value){Matcher m=Pattern.compile("^[\\t\\r\\n\\x0b\\f ]*([+-]?[0-9]+)").matcher(value);if(!m.find())return 0;java.math.BigInteger n=new java.math.BigInteger(m.group(1));return n.max(java.math.BigInteger.valueOf(Integer.MIN_VALUE)).min(java.math.BigInteger.valueOf(Integer.MAX_VALUE)).longValue()&0xffffffffL;}
    private void boundary(Z119Execution e,long target)throws Exception{
        if(e.arithmetic(target))return;long p=e.register(10),arg=e.register(11);
        switch((int)target){
        case 0x40081388:e.countBoundary("clock_millis");e.register(10,now);return;
        case 0x40090e2c:{e.countBoundary("stack_high_water");long value=waters==0?input[5]:waters==9?input[7]:input[6];++waters;event(23,p,value,"",new byte[0]);e.register(10,value);return;}
        case 0x40136e8c:e.countBoundary("esp_task_wdt_reset");event(24);++watchdogs;e.register(10,0);return;
        case 0x40128994:e.countBoundary("yield");event(22);now=(now+input[16])&0xffffffffL;if(++yields>=2000)throw new IllegalStateException("Unbounded fixture wait");return;
        case 0x401289a0:e.countBoundary("delay");event(17,p,e.read(0x3ffc2e84L,4),"",new byte[0]);now=(now+p)&0xffffffffL;return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");String path=string(e,p);e.register(10,p+Math.max(path.lastIndexOf('\\'),path.lastIndexOf('/'))+1);return;
        case 0x400d21cc:e.countBoundary("formatted_diagnostic_output_hashed");diagnostic(e);return;
        case 0x401367a0:throw new IllegalStateException("Stack guard changed");
        case 0x400d56c4:++scope;++testDepth;e.invoke(target);return;
        case 0x400db438:e.invoke(target);--testDepth;return;
        case 0x400d5a44:++scopeDepth;e.invoke(target);return;
        case 0x400d5a68:e.invoke(target);--scopeDepth;return;
        case 0x400d5ae0:e.invoke(target);return;
        case 0x400f01cc:http=p;e.invoke(target);return;
        case 0x400f90d8:case 0x400f912c:sending=true;connects=0;e.invoke(target);sending=false;return;
        case 0x400014c0:e.countBoundary("ROM_strlen");e.register(10,string(e,p).length());return;
        case 0x400015d4:{e.countBoundary("ROM_strncpy");byte[] data=new byte[(int)e.register(12)];boolean ended=false;for(int i=0;i<data.length;++i)if(!ended){data[i]=(byte)e.read(arg+i,1);ended=data[i]==0;}e.emulator.writeMemory(e.address(p),data);return;}
        case 0x400566c4:e.countBoundary("ROM_atoi_signed_decimal");e.register(10,decimal(string(e,p)));return;
        case 0x4000c2c8:e.countBoundary("ROM_memcpy");e.emulator.writeMemory(e.address(p),e.emulator.readMemory(e.address(arg),(int)e.register(12)));return;
        case 0x4000c44c:{e.countBoundary("ROM_memset");byte[] data=new byte[(int)e.register(12)];Arrays.fill(data,(byte)arg);e.emulator.writeMemory(e.address(p),data);return;}
        case 0x4013cd98:{e.countBoundary("C_snprintf_signed_decimal");if(!"%d".equals(string(e,e.register(12))))throw new IllegalStateException("Unexpected bounded format");String value=Integer.toString((int)e.register(13));byte[] data=encoded(value.substring(0,Math.min(value.length(),Math.max(0,(int)arg-1)))+"\0");e.emulator.writeMemory(e.address(p),data);e.register(10,value.length());return;}
        case 0x400ed668:{
            e.countBoundary("application_network_client_factory");event(0,e.register(13),0,"",new byte[0]);long selected,result;
            if(acquires++==0){selected=input[3];result=input[2];}
            else if(scope==input[10]){selected=0;result=0;}
            else{result=1;selected=input[4]==1?1:input[4]==2?2:input[4]==3?3:e.register(13)!=0?3:2;}
            e.write(arg,selected!=0?SOCKET:0,4);e.write(e.register(12),selected,4);e.register(10,result);return;}
        case 0x40136960:e.countBoundary("SDK_free_heap");event(2);e.register(10,(heaps++&1)==0?input[8]:input[9]);return;
        case 0x40132054:e.countBoundary("C_errno");if(sending&&e.read(http+0x1c,4)==1&&connects==0)event(1,e.read(http+0x3d,1),0,"",new byte[0]);else event(12);e.register(10,ERRNO);return;
        case 0x40030010:{e.countBoundary("socket_write");int n=(int)e.register(12);event(5,0,0,"",e.emulator.readMemory(e.address(arg),n));e.register(10,++writes==input[17]&&n>0?n-1:n);return;}
        case 0x40030020:e.countBoundary("socket_available");event(7);e.register(10,Math.min(wire.length-position,input[14]));return;
        case 0x40030030:e.countBoundary("socket_read_byte");event(8);e.register(10,position==input[15]||position==wire.length?0xffffffffL:(wire[position++]&255));return;
        case 0x40030040:e.countBoundary("socket_flush");event(6);return;
        case 0x40030050:case 0x40030060:{
            e.countBoundary(target==0x40030050?"socket_connect_address":"socket_connect_host");
            if(target==0x40030050)event(4,e.register(12),e.read(arg+4,4),"",new byte[0]);else event(3,e.register(12),0,string(e,arg),new byte[0]);
            ++connects;position=0;wire=scope==input[18]?new byte[0]:response;e.write(ERRNO,input[13],4);
            e.register(10,scope==input[11]||(scope>=7&&input[12]==0)?0:1);return;}
        case 0x40030070:e.countBoundary("socket_stop");event(11);return;
        case 0x40030080:e.countBoundary("socket_connected");event(10);e.register(10,position<wire.length?1:0);return;
        case 0x4012a928:{e.countBoundary("Stream_readBytes");int n=(int)e.register(12),actual=Math.min(n,wire.length-position);event(9,n,0,"",new byte[0]);e.emulator.writeMemory(e.address(arg),Arrays.copyOfRange(wire,position,position+actual));position+=actual;e.register(10,actual);return;}
        default:if(ORIGINAL.contains(target)){e.invoke(target);return;}throw new IllegalStateException(String.format("Unmodeled live HTTP call %08x",target));
        }
    }
    private Map<String,Object> one(Z119Execution e,JsonObject item)throws Exception{
        JsonArray words=item.getAsJsonArray("words");input=new long[words.size()];for(int i=0;i<input.length;++i)input[i]=words.get(i).getAsLong();
        e.resetRegisters();e.clear(CORE,0x6000);events.clear();now=input[0];http=0;position=acquires=scope=heaps=writes=waters=yields=watchdogs=testDepth=scopeDepth=connects=0;sending=false;
        response=encoded(item.get("wire").getAsString());wire=new byte[0];e.write(0x3ffc4654L,0x119aabbcL,4);e.write(0x3ffc2ea0L,0,4);e.write(0x3ffc2e84L,input[1],4);e.write(ERRNO,input[13],4);
        e.write(SOCKET,SOCKET+0xa00,4);for(long[] entry:new long[][]{{0xc,0x40030010},{0x10,0x40030020},{0x14,0x40030030},{0x1c,0x40030040},{0x20,0x40030050},{0x24,0x40030060},{0x2c,0x40030070},{0x30,0x40030080}})e.write(SOCKET+0xa00+entry[0],entry[1],4);
        e.register(10,CORE);e.invoke(0x40110f80L);
        Map<String,Object> out=new LinkedHashMap<>();out.put("id",item.get("id").getAsString());out.put("result",0);
        out.put("state",List.of(now,e.read(0x3ffc2e84L,4),acquires,scope,heaps,position,writes,waters,yields,watchdogs,testDepth,scopeDepth));
        out.put("events",new ArrayList<>(events));return out;
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input JSON and output JSONL");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson gson=new Gson();int count=0;
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter output=Files.newBufferedWriter(Path.of(args[1]))){
            for(JsonElement value:inputs){JsonObject item=value.getAsJsonObject();try{output.write(gson.toJson(one(e,item))+"\n");}catch(Exception failure){throw new IllegalStateException("Live HTTP case "+item.get("id").getAsString(),failure);}++count;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);report.put("cases",count);report.put("input_file_sha256",sha(Files.readAllBytes(Path.of(args[0]))));
            report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);report.put("secret_payloads_hashed",true);
            report.put("scope","Complete original live HttpClient diagnostic, eight real TestScope bodies, ordinary debug scopes, original response Stream callbacks, GET/POST/URI/state/timing bodies, returning assertions with counter/delay, wait/yield and watchdog intervals. Explicit socket, SDK, formatted-output, ROM memory and compiler arithmetic boundaries. All formatted/request byte strings are SHA-256 digests in public output.");
            report.put("full_cpu_or_hardware_emulation",false);report.put("hardware_tested",false);Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+count+" original live HTTP diagnostic cases; resource values omitted");
    }
}
