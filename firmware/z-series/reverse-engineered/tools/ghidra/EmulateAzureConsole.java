// Complete original Azure password and live integration diagnostics.
// Secret-bearing HMAC, signature and MQTT bytes are hashed before output.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.io.*;
import java.security.MessageDigest;
import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;
import java.util.*;
import java.util.regex.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
public class EmulateAzureConsole extends GhidraScript {
    private static final long OBJECT=0x3ff30000L,STATE=0x3ff32000L,SYSTEM=0x3ff33000L,NETWORK=0x3ff34000L,STRINGS=0x3ff35000L,QUEUE=0x3ff37000L;
    private static final Set<Long> ORIGINAL=Set.of(0x400d0e60L,0x400d0e90L,0x400d1a2cL,0x400d5b74L,0x400d77c4L,0x400d782cL,0x400e1180L,0x400e17d0L,0x400e1820L,0x400dabd4L,0x400db014L,0x400ed8acL,0x400ed990L,0x400edaacL,0x400ee198L,0x400ef118L,0x400ef160L,0x400efe1cL,0x400efe50L,0x400f3fa4L,0x400f400cL,0x400f9310L,0x400f9348L,0x400f944cL,0x400fa400L,0x400fa684L,0x400fec00L,0x400fec18L,0x401bee80L,0x401bee94L,0x401beea0L,0x401beeb8L,0x401bf254L,0x401bf3c0L,0x401bf3d4L,0x401bf884L);
    private long[] input;private long now;private boolean connected;private int connections,hmacs,watchdogs,scopeCount,scopeDepth,hmacError;private long pending,command;
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
    private void string(Z119Execution e,long pointer,String value){e.emulator.writeMemory(e.address(pointer),encoded(value+"\0"));}
    private void boundary(Z119Execution e,long target)throws Exception{
        if(e.arithmetic(target))return;long p=e.register(10),arg=e.register(11);
        switch((int)target){
        case 0x40081388:e.countBoundary("clock_millis");e.register(10,now);return;
        case 0x40090e2c:e.countBoundary("stack_high_water");event(23,p,input[9],"",new byte[0]);e.register(10,input[9]);return;
        case 0x40136e8c:e.countBoundary("esp_task_wdt_reset");event(24);now=(now+input[10])&0xffffffffL;if(++watchdogs>=2000)throw new IllegalStateException("Unbounded Azure fixture wait");e.register(10,0);return;
        case 0x401289a0:e.countBoundary("delay");event(17,p,e.read(0x3ffc2e84L,4),"",new byte[0]);now=(now+p)&0xffffffffL;return;
        case 0x4008c2b4:e.countBoundary("diagnostic_basename");String path=string(e,p);e.register(10,p+Math.max(path.lastIndexOf('\\'),path.lastIndexOf('/'))+1);return;
        case 0x400d21cc:e.countBoundary("formatted_diagnostic_output_hashed");diagnostic(e);return;
        case 0x401367a0:throw new IllegalStateException("Stack guard changed");
        case 0x4019d660:throw new IllegalStateException("Empty original predicate");
        case 0x400d56c4:++scopeCount;++scopeDepth;e.invoke(target);return;
        case 0x400db438:e.invoke(target);--scopeDepth;return;
        case 0x400014c0:e.countBoundary("ROM_strlen");e.register(10,string(e,p).length());return;
        case 0x4000c2c8:e.countBoundary("ROM_memcpy");e.emulator.writeMemory(e.address(p),e.emulator.readMemory(e.address(arg),(int)e.register(12)));return;
        case 0x4000c44c:{e.countBoundary("ROM_memset");byte[] bytes=new byte[(int)e.register(12)];Arrays.fill(bytes,(byte)arg);e.emulator.writeMemory(e.address(p),bytes);return;}
        case 0x4013cd98:{
            e.countBoundary("C_snprintf_url_escape");String pattern=string(e,e.register(12));
            if(!pattern.equals("%%%X"))throw new IllegalStateException("Unexpected original escape format");
            String value=String.format(Locale.ROOT,"%%%X",e.register(13)&255);string(e,p,value.substring(0,Math.min(value.length(),Math.max(0,(int)arg-1))));e.register(10,value.length());return;}
        case 0x40126b10:e.countBoundary("unused_local_MQTT_constructor");e.clear(p,0x434);return;
        case 0x401c09a8:e.countBoundary("mbedtls_md_init");e.clear(p,12);return;
        case 0x4014b2bc:e.countBoundary("mbedtls_md_info_SHA256");if(p!=6)throw new IllegalStateException("Unexpected digest algorithm");e.register(10,0x3ff39000L);return;
        case 0x4014b328:e.countBoundary("mbedtls_md_setup_HMAC");e.register(10,0);return;
        case 0x4014b2f4:e.countBoundary("mbedtls_md_free");return;
        case 0x4014b63c:{
            e.countBoundary("HMAC_SHA256_JCE");byte[] key=e.emulator.readMemory(e.address(arg),(int)e.register(12)),data=e.emulator.readMemory(e.address(e.register(13)),(int)e.register(14));
            ++hmacs;boolean failed=input[11]==hmacs||input[11]==0xffffffffL;hmacError=failed?(int)input[12]:0;
            event(40,hmacs,hmacError,new String(key,java.nio.charset.StandardCharsets.ISO_8859_1),data);
            if(!failed){Mac mac=Mac.getInstance("HmacSHA256");mac.init(new SecretKeySpec(key,"HmacSHA256"));byte[] hash=mac.doFinal(data);if(input[13]!=0)hash[0]^=1;e.emulator.writeMemory(e.address(e.register(15)),hash);}
            e.register(10,hmacError);return;}
        case 0x40149a94:{
            e.countBoundary("mbedtls_base64_encode");byte[] data=e.emulator.readMemory(e.address(e.register(13)),(int)e.register(14)),encoded=Base64.getEncoder().encode(data);
            if(encoded.length>=arg)throw new IllegalStateException("Insufficient original base64 span");
            e.emulator.writeMemory(e.address(p),encoded);e.write(e.register(12),encoded.length,4);e.register(10,0);return;}
        case 0x401bff2c:e.countBoundary("MQTT_connected");event(51);e.register(10,connected?1:0);return;
        case 0x40126d48:e.countBoundary("MQTT_disconnect");event(52);if(input[8]==0)connected=false;return;
        case 0x400ed4d4:e.countBoundary("network_TLS_client_factory");event(53,1,0,"",new byte[0]);e.write(arg,input[6]!=0?0x3ff36000L:0,4);e.write(e.register(12),3,4);e.register(10,input[6]);return;
        case 0x401bff88:e.countBoundary("MQTT_bind_client");event(54);return;
        case 0x401bff6c:e.countBoundary("MQTT_set_server");event(55,e.register(12),0,string(e,arg),new byte[0]);return;
        case 0x401bff7c:e.countBoundary("MQTT_set_callback");event(56);return;
        case 0x40126f4c:e.countBoundary("MQTT_connect");event(57,0,0,string(e,arg)+"|"+string(e,e.register(12))+"|"+string(e,e.register(13)),new byte[0]);++connections;if(input[7]!=0)connected=true;e.register(10,input[7]);return;
        case 0x40127200:e.countBoundary("MQTT_subscribe");event(58,0,0,string(e,arg),new byte[0]);e.register(10,1);return;
        case 0x40126f6c:e.countBoundary("MQTT_loop");event(59);e.register(10,1);return;
        case 0x400ef230:e.countBoundary("CoreCommand_envelope_constructor");e.write(p+4,arg,4);return;
        case 0x400f3d3c:e.countBoundary("CoreCommand_single_slot_mailbox");if(pending==0){pending=1;command=e.read(arg+4,4);}return;
        default:if(ORIGINAL.contains(target)){e.invoke(target);return;}throw new IllegalStateException(String.format("Unmodeled Azure diagnostic call %08x",target));
        }
    }
    private Map<String,Object> one(Z119Execution e,JsonObject item)throws Exception{
        JsonArray words=item.getAsJsonArray("words");input=new long[words.size()];for(int i=0;i<input.length;++i)input[i]=words.get(i).getAsLong();
        e.resetRegisters();e.clear(OBJECT,0xa000);events.clear();now=input[1];connected=input[5]!=0;connections=hmacs=watchdogs=scopeCount=scopeDepth=hmacError=0;pending=command=0;
        e.write(0x3ffc4654L,0x119aabbcL,4);e.write(0x3ffc2ea0L,0,4);e.write(0x3ffc2e84L,input[2],4);
        e.write(OBJECT,0x3f41fe88L,4);e.write(OBJECT+4,SYSTEM,4);e.write(SYSTEM+0x8c,NETWORK,4);e.write(OBJECT+8,STRINGS,4);e.write(OBJECT+12,QUEUE,4);string(e,STRINGS,"fixture-live");
        e.write(OBJECT+0x724,STATE,4);e.write(STATE,input[3],4);e.write(OBJECT+0x454,input[4],1);e.write(OBJECT+0x730,input[18],4);e.write(OBJECT+0x734,input[19],4);
        byte[] key=e.emulator.readMemory(e.address(0x3f42a4dcL),32);if(input[15]!=0)key[0]^=1;e.emulator.writeMemory(e.address(STRINGS+0x800),key);e.write(OBJECT+0x458,STRINGS+0x800,4);e.write(OBJECT+0x45c,32,4);
        String[] hosts={"picobrewdev.azure-devices.net","picobrew.azure-devices.net","","","fixture.azure.invalid"};string(e,STRINGS+0x100,hosts[(int)input[14]]);e.write(OBJECT+0x570,input[14]==3?0:STRINGS+0x100,4);
        string(e,OBJECT+0x574,"fixture-user");string(e,OBJECT+0x460,"fixture-password");string(e,OBJECT+0x604,"fixture-topic");string(e,OBJECT+0x694,"fixture-publish");
        string(e,STRINGS+0x200,"Azure.h");e.write(OBJECT+0x738,STRINGS+0x200,4);e.write(OBJECT+0x738+20,input[16],4);e.write(OBJECT+0x738+24,input[17],4);
        if(input[0]==0){e.register(10,SYSTEM);e.register(11,QUEUE);e.register(12,OBJECT);e.invoke(0x400fec74L);}
        else{e.register(10,OBJECT);e.invoke(0x400fa684L);}
        Map<String,Object> out=new LinkedHashMap<>();out.put("id",item.get("id").getAsString());out.put("result",0);
        out.put("state",List.of(e.read(STATE,4),e.read(OBJECT+0x454,1),e.read(OBJECT+0x730,4),e.read(OBJECT+0x734,4),e.read(OBJECT+0x73c,1),
            e.read(OBJECT+0x740,4),e.read(OBJECT+0x744,4),e.read(OBJECT+0x748,4),now,e.read(0x3ffc2e84L,4),connected?1:0,connections,scopeCount,hmacs,watchdogs,pending,command,scopeDepth));
        out.put("events",new ArrayList<>(events));return out;
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input JSON and output JSONL");JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();Gson gson=new Gson();int count=0;
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter output=Files.newBufferedWriter(Path.of(args[1]))){
            for(JsonElement value:inputs){JsonObject item=value.getAsJsonObject();try{output.write(gson.toJson(one(e,item))+"\n");}catch(Exception failure){throw new IllegalStateException("Azure console case "+item.get("id").getAsString(),failure);}++count;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);report.put("cases",count);report.put("input_file_sha256",sha(Files.readAllBytes(Path.of(args[0]))));
            report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);report.put("secret_payloads_hashed",true);
            report.put("scope","Complete original Azure live integration400fec74 and password400fa684 diagnostics, signature400fa400, base64-to-URL conversion, lifecycle/backoff, wait predicates, real TestScope and returning assertion bodies. HMAC-SHA256 uses JCE on original image key bytes at an explicit mbedTLS boundary; base64, unused MQTT constructor, socket/MQTT, clock/watchdog/stack, ROM memory and output are explicit boundaries. All key, signature, credential and diagnostic bytes are SHA-256 hashes in public traces.");
            report.put("hardware_tested",false);report.put("full_cpu_or_hardware_emulation",false);Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+count+" complete original Azure console cases; private bytes omitted");
    }
}
