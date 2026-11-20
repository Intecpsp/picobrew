// Execute original StillClient protocol and IPicoStill sensor instructions.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.regex.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulateStillClientContinuation extends GhidraScript {
    private static final long CLIENT=0x3ff20000L,STATE=CLIENT+0x1000,HTTP=CLIENT+0x2000,STRINGS=CLIENT+0x3000,INPUT=STRINGS+0x1000;
    private static final Set<Long> ORIGINAL=Set.of(0x400d0df8L,0x400d0e60L,0x400d5730L,0x400daeb4L,0x400db708L,0x400db7bcL,0x400e1180L,0x400ee998L,0x400ef118L,0x400ef160L,0x400ef180L,0x400ef818L,0x400ef9ccL,0x400efe1cL,0x400efee0L,0x400f0174L,0x400f1844L,0x400f1efcL,0x400f2010L,0x400f202cL,0x400f4c4cL,0x400f4c70L,0x400f4c90L,0x400f8190L,0x400f81c4L,0x400f82ccL,0x400f82e8L,0x400f83f0L,0x400f9310L,0x400f9348L,0x400f944cL,0x400f947cL,0x401007f0L,0x40100830L,0x40100938L,0x40100954L,0x401009bcL,0x40100f4cL,0x4010c4dcL,0x4010c614L,0x4010c8d0L,0x4010ded0L,0x4010dfc4L,0x401bf254L,0x401bf278L,0x401bf3d4L,0x401bf3ecL,0x401bf8d8L,0x401bf918L);
    private static final Pattern DECIMAL=Pattern.compile("^[\\t\\n\\r ]*([+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?)");
    private final List<List<Long>> events=new ArrayList<>();private long[] input;private final StillContinuationSupport diagnostics=new StillContinuationSupport();
    private String string(Z119Execution e,long p)throws Exception{if(p==0)return "";StringBuilder s=new StringBuilder();for(int i=0;i<2048;++i){int c=(int)e.read(p+i,1);if(c==0)return s.toString();s.append((char)c);}throw new IllegalStateException("Unterminated text");}
    private void string(Z119Execution e,long p,String s){e.emulator.writeMemory(e.address(p),(s+"\0").getBytes(java.nio.charset.StandardCharsets.ISO_8859_1));}
    private void append(List<Long> out,String text){out.add((long)text.length());for(int i=0;i<text.length();++i)out.add((long)text.charAt(i));}
    private void bytes(List<Long> out,Z119Execution e,long p,int count)throws Exception{out.add((long)count);for(int i=0;i<count;++i)out.add(e.read(p+i,1));}
    private void number(List<Long> out,Z119Execution e,long p)throws Exception{out.add(e.read(p,4));out.add(e.read(p+4,4));}
    private void number(Z119Execution e,long p,double value){long bits=Double.doubleToRawLongBits(value);e.write(p,bits&0xffffffffL,4);e.write(p+4,bits>>>32,4);}
    private void bounded(Z119Execution e,long p,int capacity,String value){int n=Math.min(capacity,value.length()),aligned=(capacity+4)&~3;e.clear(p,capacity+1);string(e,p,value.substring(0,n));e.write(p+aligned,n,4);e.write(p+aligned+4,capacity-n,4);e.write(p+aligned+8,value.length()>capacity?1:0,1);}
    private void record(long effect,long kind,String uri,String type,String body){List<Long> row=new ArrayList<>(List.of(effect,kind));append(row,uri);append(row,type);append(row,body);events.add(row);diagnostics.service(events.size()-1);}
    private void boundary(Z119Execution e,long target)throws Exception{
        if(diagnostics.boundary(e,target)||e.arithmetic(target))return;long p=e.register(10),arg=e.register(11);
        switch((int)target){
        case 0x401367a0:throw new IllegalStateException("Original stack guard changed");
        case 0x400014c0:e.countBoundary("ROM_strlen");e.register(10,string(e,p).length());return;
        case 0x4000c2c8:e.countBoundary("ROM_memcpy");e.emulator.writeMemory(e.address(p),e.emulator.readMemory(e.address(arg),(int)e.register(12)));return;
        case 0x4000c44c:e.countBoundary("ROM_memset");byte[] data=new byte[(int)e.register(12)];Arrays.fill(data,(byte)arg);e.emulator.writeMemory(e.address(p),data);return;
        case 0x40132040:
            e.countBoundary("C_atof_decimal_prefix");String value=string(e,p);Matcher matcher=DECIMAL.matcher(value);double parsed=matcher.find()?Double.parseDouble(matcher.group(1)):0.0;e.number(10,parsed);return;
        case 0x4013cd98:
            e.countBoundary("C_snprintf_percent_uppercase_hex");if(!"%%%X".equals(string(e,e.register(12))))throw new IllegalStateException("Unexpected format");
            String encoded="%"+Long.toHexString(e.register(13)).toUpperCase(Locale.ROOT);int n=Math.min(encoded.length(),Math.max(0,(int)arg-1));string(e,p,encoded.substring(0,n));e.register(10,encoded.length());return;
        case 0x400f0124:e.countBoundary("HttpClient_reset");record(0,0,"","","");return;
        case 0x400db2c8:e.countBoundary("HttpClient_cancel");record(1,0,"","","");return;
        case 0x4010c7ec:e.countBoundary("HttpClient_configure");record(2,e.register(14),string(e,e.register(12)),"","");e.register(10,input[7]);return;
        case 0x400f90d8:e.countBoundary("HttpClient_GET");record(3,0,"","","");e.register(10,input[8]);return;
        case 0x400f912c:e.countBoundary("HttpClient_POST");record(4,0,"",string(e,arg),string(e,e.register(12)));e.register(10,input[9]);return;
        default:if(ORIGINAL.contains(target)||target==0x401bf4c8L){e.invoke(target);return;}throw new IllegalStateException(String.format("Unmodeled StillClient call %08x",target));
        }
    }
    private void invoke(Z119Execution e,long target,long... args)throws Exception{for(int i=0;i<args.length;++i)e.register(10+i,args[i]);e.invoke(target);}
    private void initial(Z119Execution e,long p,long timestamp,long delta){
        e.write(p,timestamp,4);for(int i=0;i<5;++i)number(e,p+8+i*8,i+1);e.write(p+0x30,1,1);e.write(p+0x32,1,1);bounded(e,p+0x34,64,"old error");e.write(p+0x84,delta,4);for(int i=0;i<5;++i)number(e,p+0x88+i*8,-i-1);
    }
    private void sample(List<Long> out,Z119Execution e,long p)throws Exception{out.add(e.read(p,4));for(int i=0;i<5;++i)number(out,e,p+8+i*8);for(int i=0;i<3;++i)out.add(e.read(p+0x30+i,1));append(out,string(e,p+0x34));out.add(e.read(p+0x84,4));for(int i=0;i<5;++i)number(out,e,p+0x88+i*8);}
    private void statistics(List<Long> out,Z119Execution e,long p)throws Exception{for(int i=0;i<30;++i)number(out,e,p+i*8);out.add(e.read(p+0xf0,4));out.add(e.read(p+0xf4,4));for(int i=0;i<5;++i)number(out,e,p+0xf8+i*8);out.add(e.read(p+0x120,1));number(out,e,p+0x128);}
    private Map<String,Object> runCase(Z119Execution e,JsonObject item)throws Exception{
        input=new long[23];JsonArray words=item.getAsJsonArray("words");for(int i=0;i<input.length;++i)input[i]=words.get(i).getAsLong();
        e.resetRegisters();e.clear(CLIENT,0x6000);events.clear();diagnostics.reset(e,input[12],item.has("counter")?item.get("counter").getAsLong():0xffffffffL,0);e.write(0x3ffc4654L,0x119aabbcL,4);e.write(CLIENT+8,STATE,4);e.write(CLIENT+12,HTTP,4);
        e.write(CLIENT+0x46c,input[0],4);e.write(CLIENT+0x468,input[1],1);e.write(CLIENT+0x469,input[2],1);e.write(CLIENT+0x5d8,input[3],1);
        e.write(HTTP+0x20,input[4],4);e.write(HTTP+0x2c,input[5],4);e.write(HTTP+0x30,input[6],4);e.write(CLIENT+0x470,input[10],4);e.write(CLIENT+0x474,input[11],4);bounded(e,CLIENT+0x478,64,"old/type");
        bounded(e,CLIENT+0x38,16,"192.168.4.1");bounded(e,CLIENT+0x3d8,128,"old-uri");bounded(e,CLIENT+0x4c8,256,"old-response");
        e.write(STATE+4,input[17],4);e.write(STATE+0x73c,input[18],1);e.write(STATE+0x734,300,4);bounded(e,STATE+0x6d4,32,"old-version");bounded(e,STATE+0x704,32,"old-ip");
        initial(e,STATE+0x10,input[13],input[14]);initial(e,STATE+0xc0,input[15],input[14]);initial(e,STATE+0x170,input[16],input[14]);
        for(long p:new long[]{STATE+0x220,STATE+0x350,STATE+0x480})number(e,p+0x118,0.05);
        String text=item.get("text").getAsString(),password=item.get("password").getAsString();string(e,INPUT,text);long result=0;
        switch(item.get("op").getAsString()){
        case "host":invoke(e,0x400f83f0L,CLIENT,INPUT);break;
        case "uri":e.write(STRINGS,input[19],4);invoke(e,0x400f947cL,CLIENT,STRINGS);break;
        case "reset":invoke(e,0x400f0174L,CLIENT);break;
        case "command":invoke(e,0x4010c8d0L,CLIENT,input[19]);break;
        case "configure":bounded(e,STRINGS,32,text);bounded(e,STRINGS+0x30,64,password);invoke(e,0x4010ded0L,CLIENT,STRINGS);break;
        case "tick":invoke(e,0x400daeb4L,CLIENT);break;
        case "error":invoke(e,0x401bf8d8L,CLIENT);break;
        case "headers":e.write(STRINGS,input[19],4);e.write(STRINGS+4,input[11],4);bounded(e,STRINGS+8,64,text);invoke(e,0x400efee0L,CLIENT,STRINGS);result=e.register(10);break;
        case "body":invoke(e,0x4010dfc4L,CLIENT,INPUT,text.length());break;
        case "complete":bounded(e,CLIENT+0x4c8,256,"");invoke(e,0x4010dfc4L,CLIENT,INPUT,text.length());invoke(e,0x40100f4cL,CLIENT);break;
        case "parse":bounded(e,STRINGS,256,text);invoke(e,0x401009bcL,STRINGS+0x400,input[12],STATE+0x10,STRINGS);result=e.register(10);break;
        case "speeds":e.emulator.writeMemory(e.address(STRINGS),e.emulator.readMemory(e.address(STATE+0x10),0xb0));e.write(STATE+0x10,input[12],4);for(int i=0;i<4;++i)number(e,STATE+0x18+i*8,i+1+10.25);number(e,STATE+0x38,2.5);invoke(e,0x400d5730L,STATE+0x10,STRINGS);break;
        case "stats":
            for(int i=0;i<input[21];++i){long now=(input[12]+i*3000L)&0xffffffffL;e.write(STATE+0x10,now,4);number(e,STATE+0x18,((i*17L+input[22])&0xffffffffL)%31/4.0);number(e,STATE+0x20,((i*7L+input[22])&0xffffffffL)%53/8.0);number(e,STATE+0x38,((i*11L+input[22])&0xffffffffL)%13/16.0);invoke(e,0x400ef9ccL,STATE,now);}break;
        case "encode":
            for(int i=0;i<128;++i)e.write(STRINGS+i,90,1);invoke(e,0x400e1180L,STRINGS,input[20],INPUT);result=e.register(10);List<Long> encoded=new ArrayList<>();for(int i=0;i<128;++i)encoded.add(e.read(STRINGS+i,1));return output(e,item,result,encoded);
        case "has_error":bounded(e,STATE+0x44,64,text);invoke(e,0x401bf4c8L,STATE);result=e.register(10);break;
        case "error_text":bounded(e,STATE+0x44,64,text);invoke(e,0x400e7078L,STATE);List<Long> message=new ArrayList<>(List.of(diagnostics.assertionLine));append(message,string(e,e.register(10)));return output(e,item,0,message);
        default:throw new IllegalArgumentException("Unknown StillClient operation");
        }
        List<Long> state=new ArrayList<>(List.of(e.read(CLIENT+0x46c,4),e.read(CLIENT+0x468,1),e.read(CLIENT+0x469,1),e.read(CLIENT+0x5d8,1),e.read(HTTP+0x20,4),e.read(HTTP+0x2c,4),e.read(HTTP+0x30,4),
            e.read(CLIENT+0x470,4),e.read(CLIENT+0x474,4),e.read(CLIENT+0x5cc,4),e.read(CLIENT+0x54,1),e.read(CLIENT+0x464,1),e.read(CLIENT+0x5d4,1),diagnostics.assertionLine,e.read(STATE+4,4),e.read(STATE+0x73c,1),e.read(STATE+0x734,4)));
        append(state,string(e,CLIENT+0x38));append(state,string(e,CLIENT+0x3d8));bytes(state,e,CLIENT+0x4c8,(int)e.read(CLIENT+0x5cc,4));append(state,string(e,CLIENT+0x478));append(state,string(e,STATE+0x704));append(state,string(e,STATE+0x6d4));
        for(long p:new long[]{STATE+0x10,STATE+0xc0,STATE+0x170})sample(state,e,p);for(long p:new long[]{STATE+0x220,STATE+0x350,STATE+0x480})statistics(state,e,p);return output(e,item,result,state);
    }
    private Map<String,Object> output(Z119Execution e,JsonObject item,long result,List<Long> state)throws Exception{Map<String,Object> out=new LinkedHashMap<>();out.put("id",item.get("id").getAsString());out.put("result",result);out.put("state",state);out.put("events",new ArrayList<>(events));diagnostics.finish(out,e);return out;}
    private static String sha(byte[] bytes)throws Exception{StringBuilder out=new StringBuilder();for(byte b:MessageDigest.getInstance("SHA-256").digest(bytes))out.append(String.format("%02x",b&255));return out.toString();}
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=3)throw new IllegalArgumentException("Expected input JSON, output JSONL, mapped image layout");StillContinuationSupport.validateImage(currentProgram,Path.of(args[2]));JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson gson=new Gson();int count=0;
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){
            for(JsonElement value:inputs){JsonObject item=value.getAsJsonObject();try{out.write(gson.toJson(runCase(e,item))+"\n");}catch(Exception failure){throw new IllegalStateException("StillClient scenario "+item.get("id"),failure);}++count;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);report.put("cases",count);report.put("input_file_sha256",sha(Files.readAllBytes(Path.of(args[0]))));report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original StillClient reset/URI/commands/form encoding/HTTP callbacks, PicoStill sensor CSV decoding, sample speeds, timed snapshots and rolling statistics. Explicit outgoing HTTP exchange, libc decimal atof/hex formatting, ROM memory, arithmetic and System rendering boundaries; the real returning assertion helper executes counter updates and delays; no live socket or hardware execution.");report.put("full_cpu_or_hardware_emulation",false);report.put("hardware_tested",false);Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }println("Executed "+count+" original StillClient/protocol scenarios");
    }
}
