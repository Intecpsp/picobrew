// Original outgoing HTTP instructions at socket, receiver and libc boundaries.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.regex.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
public class EmulateHttpClient extends GhidraScript {
    private static final long HTTP=0x3ff20000L,SOCKET=HTTP+0x1000,RECEIVER=HTTP+0x2000,STRINGS=HTTP+0x3000,ERRNO=HTTP+0x5f00;
    private static final Set<Long> ORIGINAL=Set.of(0x400d0e90L,0x400d149cL,0x400d5664L,0x400d585cL,0x400db108L,0x400db188L,0x400db2c8L,0x400db324L,0x400db3b4L,0x400ee998L,0x400ef160L,0x400efe1cL,0x400eff2cL,0x400eff54L,0x400f005cL,0x400f0088L,0x400f0124L,0x400f8190L,0x400f81c4L,0x400f88c4L,0x400f89f8L,0x400f8a74L,0x400f8f9cL,0x400f90d8L,0x400f912cL,0x400f9238L,0x401007f0L,0x40100830L,0x40100938L,0x40100954L,0x40101bc4L,0x4010c4dcL,0x4010c614L,0x4010c630L,0x4010c65cL,0x4010c7ecL,0x40110cf0L,0x40110e2cL,0x401bf278L,0x401bf3d4L,0x401bf3ecL,0x401c0018L);
    private final List<List<Long>> events=new ArrayList<>();private long[] input;private long assertion;private byte[] wire;private int position,writes,heaps,connects;private boolean sending;
    private String string(Z119Execution e,long p)throws Exception{if(p==0)return "";StringBuilder s=new StringBuilder();for(int i=0;i<4096;++i){int c=(int)e.read(p+i,1);if(c==0)return s.toString();s.append((char)c);}throw new IllegalStateException("Unterminated HTTP fixture");}
    private void string(Z119Execution e,long p,String s){e.emulator.writeMemory(e.address(p),(s+"\0").getBytes(java.nio.charset.StandardCharsets.ISO_8859_1));}
    private void append(List<Long> out,String text){out.add((long)text.length());for(int i=0;i<text.length();++i)out.add((long)text.charAt(i));}
    private void bytes(List<Long> out,byte[] data){out.add((long)data.length);for(byte b:data)out.add((long)(b&255));}
    private void event(long id,long argument,long second,String text,byte[] data){List<Long> row=new ArrayList<>(List.of(id,argument&0xffffffffL,second&0xffffffffL));append(row,text);bytes(row,data);events.add(row);}
    private void event(long id){event(id,0,0,"",new byte[0]);}
    private void bounded(Z119Execution e,long p,int capacity,String value){int n=Math.min(capacity,value.length()),aligned=(capacity+4)&~3;e.clear(p,capacity+1);string(e,p,value.substring(0,n));e.write(p+aligned,n,4);e.write(p+aligned+4,capacity-n,4);e.write(p+aligned+8,value.length()>capacity?1:0,1);e.write(p+aligned+9,0,1);}
    private long decimal(String value){Matcher m=Pattern.compile("^[\\t\\r\\n\\x0b\\f ]*([+-]?[0-9]+)").matcher(value);if(!m.find())return 0;java.math.BigInteger n=new java.math.BigInteger(m.group(1));return n.max(java.math.BigInteger.valueOf(Integer.MIN_VALUE)).min(java.math.BigInteger.valueOf(Integer.MAX_VALUE)).longValue()&0xffffffffL;}
    private void boundary(Z119Execution e,long target)throws Exception{
        if(e.arithmetic(target))return;long p=e.register(10),arg=e.register(11);
        switch((int)target){
        case 0x40081388:e.countBoundary("clock_millis");e.register(10,input[6]);return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
        case 0x400d21cc:case 0x400dacb8:case 0x400d561c:e.countBoundary("diagnostic_output");return;
        case 0x400db014:e.countBoundary("returning_assertion");assertion=arg;return;
        case 0x401367a0:throw new IllegalStateException("Stack guard changed");
        case 0x400014c0:e.countBoundary("ROM_strlen");e.register(10,string(e,p).length());return;
        case 0x400015d4:e.countBoundary("ROM_strncpy");byte[] copied=new byte[(int)e.register(12)];boolean ended=false;for(int i=0;i<copied.length;++i){if(!ended){copied[i]=(byte)e.read(arg+i,1);ended=copied[i]==0;}}e.emulator.writeMemory(e.address(p),copied);return;
        case 0x400566c4:e.countBoundary("ROM_atoi_signed_decimal");e.register(10,decimal(string(e,p)));return;
        case 0x4000c2c8:e.countBoundary("ROM_memcpy");e.emulator.writeMemory(e.address(p),e.emulator.readMemory(e.address(arg),(int)e.register(12)));return;
        case 0x4000c44c:e.countBoundary("ROM_memset");byte[] zeros=new byte[(int)e.register(12)];Arrays.fill(zeros,(byte)arg);e.emulator.writeMemory(e.address(p),zeros);return;
        case 0x4013cd98:e.countBoundary("C_snprintf_signed_decimal");if(!"%d".equals(string(e,e.register(12))))throw new IllegalStateException("Unexpected format");String text=Integer.toString((int)e.register(13));string(e,p,text.substring(0,Math.min(text.length(),Math.max(0,(int)arg-1))));e.register(10,text.length());return;
        case 0x400ed668:e.countBoundary("application_network_client_factory");event(0,e.register(13),0,"",new byte[0]);e.write(arg,input[16]!=0?SOCKET:0,4);e.write(e.register(12),input[17],4);e.register(10,input[16]);return;
        case 0x40136960:e.countBoundary("SDK_free_heap");event(2);e.register(10,heaps++==0?input[20]:input[21]);return;
        case 0x40132054:
            e.countBoundary("C_errno");if(sending&&input[1]==1&&connects==0)event(1,e.read(HTTP+0x3d,1),0,"",new byte[0]);else event(12);e.register(10,ERRNO);return;
        case 0x40030010:e.countBoundary("socket_write");int n=(int)e.register(12);event(5,0,0,"",e.emulator.readMemory(e.address(arg),n));e.register(10,++writes==input[19]&&n>0?n-1:n);return;
        case 0x40030020:e.countBoundary("socket_available");event(7);e.register(10,input[25]==0xffffffffL?0xffffffffL:Math.min(wire.length-position,input[25]));return;
        case 0x40030030:e.countBoundary("socket_read_byte");event(8);e.register(10,position==input[24]||position==wire.length?0xffffffffL:(wire[position++]&255));return;
        case 0x40030040:e.countBoundary("socket_flush");event(6);return;
        case 0x40030050:e.countBoundary("socket_connect_address");event(4,e.register(12),e.read(arg+4,4),"",new byte[0]);++connects;e.write(ERRNO,input[4],4);e.register(10,input[18]);return;
        case 0x40030060:e.countBoundary("socket_connect_host");event(3,e.register(12),0,string(e,arg),new byte[0]);++connects;e.write(ERRNO,input[4],4);e.register(10,input[18]);return;
        case 0x40030070:e.countBoundary("socket_stop");event(11);return;
        case 0x40030080:e.countBoundary("socket_connected");event(10);e.register(10,input[22]);return;
        case 0x4012a928:e.countBoundary("Stream_readBytes");int count=(int)e.register(12),actual=Math.min(count,wire.length-position);event(9,count,0,"",new byte[0]);e.emulator.writeMemory(e.address(arg),Arrays.copyOfRange(wire,position,position+actual));position+=actual;e.register(10,actual);return;
        case 0x40030100:e.countBoundary("application_response_headers");event(13,e.read(arg,4),e.read(arg+4,4),string(e,arg+8),new byte[0]);e.register(10,input[23]);return;
        case 0x40030110:e.countBoundary("application_response_complete");event(15);return;
        case 0x40030120:e.countBoundary("application_response_body");event(14,0,0,"",e.emulator.readMemory(e.address(arg),(int)e.register(12)));return;
        default:if(ORIGINAL.contains(target)){e.invoke(target);return;}throw new IllegalStateException(String.format("Unmodeled HTTP call %08x",target));
        }
    }
    private void invoke(Z119Execution e,long target,long... args)throws Exception{for(int i=0;i<args.length;++i)e.register(10+i,args[i]);e.invoke(target);}
    private Map<String,Object> runCase(Z119Execution e,JsonObject item)throws Exception{
        input=new long[36];JsonArray words=item.getAsJsonArray("words");for(int i=0;i<input.length;++i)input[i]=words.get(i).getAsLong();
        e.resetRegisters();e.clear(HTTP,0x6000);events.clear();assertion=0;position=writes=heaps=connects=0;String op=item.get("op").getAsString();sending=op.equals("get")||op.equals("post")||op.equals("put");wire=item.get("wire").getAsString().getBytes(java.nio.charset.StandardCharsets.ISO_8859_1);
        e.write(0x3ffc4654L,0x119aabbcL,4);e.write(0x3ffc2ea0L,0,4);e.write(ERRNO,input[4],4);
        e.write(SOCKET,SOCKET+0xa00,4);for(long[] entry:new long[][]{{0xc,0x40030010},{0x10,0x40030020},{0x14,0x40030030},{0x1c,0x40030040},{0x20,0x40030050},{0x24,0x40030060},{0x2c,0x40030070},{0x30,0x40030080}})e.write(SOCKET+0xa00+entry[0],entry[1],4);
        e.write(RECEIVER,RECEIVER+0x100,4);e.write(RECEIVER+0x100,0x40030100L,4);e.write(RECEIVER+0x104,0x40030110L,4);e.write(RECEIVER+0x108,0x40030120L,4);
        String uri=item.get("uri").getAsString(),value=item.get("value").getAsString(),type=item.get("content_type").getAsString(),buffer=item.get("buffer").getAsString();string(e,STRINGS,uri);string(e,STRINGS+0x800,value);string(e,STRINGS+0x1000,type);
        e.write(HTTP+0x38,80,2);e.write(HTTP+0x3a,443,2);invoke(e,0x4010c65cL,HTTP+0x38,STRINGS);assertion=0;bounded(e,HTTP+0x1a8,512,"old-token");
        e.write(HTTP+0x20,input[0],4);e.write(HTTP+0x1c,input[1],4);e.write(HTTP+0x18,input[2]!=0?SOCKET:0,4);e.write(HTTP+0x2c,input[3],4);e.write(HTTP+0x30,input[4],4);
        e.write(HTTP+0x24,input[7],4);e.write(HTTP+0x28,input[8],4);e.write(HTTP+4,input[9],4);e.write(HTTP+8,input[10],4);e.write(HTTP+0x34,input[11],1);e.write(HTTP+0x410,input[12],4);
        e.write(HTTP+0x3b8,input[13],4);e.write(HTTP+0x3bc,input[14],4);bounded(e,HTTP+0x3c0,64,"old/type");e.write(HTTP+0x414,RECEIVER,4);
        bounded(e,HTTP+0x418,1024,buffer);e.write(HTTP+0x825,input[26],1);e.write(HTTP+0x824,input[27],1);
        e.write(HTTP+0x840,input[28],4);e.write(HTTP+0x844,input[29],4);e.write(HTTP+0x848,input[30],4);e.write(HTTP+0x834,input[31],4);e.write(HTTP+0x838,input[32],4);e.write(HTTP+0x83c,input[33],4);
        long result=0;
        switch(op){
        case "parse":invoke(e,0x4010c65cL,HTTP+0x38,STRINGS);result=e.register(10);break;
        case "reset":invoke(e,0x400f0124L,HTTP);break;
        case "transition":invoke(e,0x400db188L,HTTP,input[15]);break;
        case "error":invoke(e,0x400db324L,HTTP,input[15]);break;
        case "cancel":invoke(e,0x400db2c8L,HTTP);break;
        case "configure":invoke(e,0x4010c7ecL,HTTP,RECEIVER,STRINGS,input[15]!=0?SOCKET:0,input[15]);result=e.register(10);break;
        case "authorization":invoke(e,0x40110e2cL,HTTP,STRINGS+0x800);result=e.register(10);break;
        case "get":invoke(e,0x400f90d8L,HTTP);result=e.register(10);break;
        case "post":case "put":invoke(e,op.equals("post")?0x400f912cL:0x400f9238L,HTTP,STRINGS+0x1000,input[34]!=0?STRINGS+0x800:0,input[35]==0xffffffffL?value.length():input[35]);result=e.register(10);break;
        case "tick":invoke(e,0x40101bc4L,HTTP,input[5]);break;
        default:throw new IllegalArgumentException("Unknown HTTP operation");
        }
        List<Long> state=new ArrayList<>(List.of(e.read(HTTP+0x20,4),e.read(HTTP+0x1c,4),e.read(HTTP+0x18,4)!=0?1L:0L,e.read(HTTP+0x2c,4),e.read(HTTP+0x30,4),e.read(HTTP+0x24,4),e.read(HTTP+0x28,4),e.read(HTTP+0x34,1),e.read(HTTP+0x410,4),e.read(HTTP+0x3b8,4),e.read(HTTP+0x3bc,4),e.read(HTTP+0x40c,1),assertion,
            e.read(HTTP+0x81c,4),e.read(HTTP+0x820,4),e.read(HTTP+0x824,1),e.read(HTTP+0x825,1),e.read(HTTP+0x414,4)!=0?1L:0L,e.read(HTTP+0x840,4),e.read(HTTP+0x844,4),e.read(HTTP+0x848,4),e.read(HTTP+0x834,4),e.read(HTTP+0x838,4),e.read(HTTP+0x83c,4),
            e.read(HTTP+0x38,2),e.read(HTTP+0x3a,2),e.read(HTTP+0x3e,2),e.read(HTTP+0x3c,1),e.read(HTTP+0x3d,1),e.read(HTTP+0x44,4),e.read(HTTP+0x94,1),e.read(HTTP+0x1a4,1),e.read(HTTP+0x3b4,1),(long)position));
        for(long p:new long[]{HTTP+0x48,HTTP+0x98,HTTP+0x1a8,HTTP+0x3c0})append(state,string(e,p));bytes(state,e.emulator.readMemory(e.address(HTTP+0x418),1025));
        Map<String,Object> out=new LinkedHashMap<>();out.put("id",item.get("id").getAsString());out.put("result",result);out.put("state",state);out.put("events",new ArrayList<>(events));return out;
    }
    private static String sha(byte[] data)throws Exception{StringBuilder out=new StringBuilder();for(byte b:MessageDigest.getInstance("SHA-256").digest(data))out.append(String.format("%02x",b&255));return out.toString();}
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input JSON and output JSONL");JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson gson=new Gson();int count=0;
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){
            for(JsonElement value:inputs){JsonObject item=value.getAsJsonObject();try{out.write(gson.toJson(runCase(e,item))+"\n");}catch(Exception failure){throw new IllegalStateException("HTTP scenario "+item.get("id"),failure);}++count;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);report.put("cases",count);report.put("input_file_sha256",sha(Files.readAllBytes(Path.of(args[0]))));report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original outgoing HTTP URI parsing including SDK IPv4 instructions, reset/configuration/authorization, GET/POST/PUT bytes, six-state response polling, deadlines and errors. Explicit network-client acquisition, socket/Stream operations, heap/clock, ROM decimal conversion/memory, receiver callbacks and diagnostics boundaries.");report.put("full_cpu_or_hardware_emulation",false);report.put("hardware_tested",false);Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }println("Executed "+count+" original outgoing HTTP scenarios");
    }
}
