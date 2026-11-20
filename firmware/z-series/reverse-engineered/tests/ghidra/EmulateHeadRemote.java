// Actual Core remote-session route and returning assertions; queue boundaries explicit.
// @category PicoBrew.Validation
import java.nio.file.*;import java.nio.charset.StandardCharsets;import java.security.MessageDigest;import java.util.*;import com.google.gson.*;
import ghidra.app.script.GhidraScript;import ghidra.program.model.address.AddressRange;import ghidra.program.model.listing.Function;
public class EmulateHeadRemote extends GhidraScript {
 static final long C=0x3ff20000L,R=0x3ff60000L;long now;List<List<Long>> effects=new ArrayList<>();
 static String sha(byte[] b)throws Exception{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(b));}
 static Map<String,Object> row(Object... v){Map<String,Object> r=new LinkedHashMap<>();for(int i=0;i<v.length;i+=2)r.put((String)v[i],v[i+1]);return r;}
 void params(Z119Execution e,long p,List<Long> row)throws Exception{for(int i=0;i<3;i++)row.add(e.read(p+4*i,4));for(int off:new int[]{16,32,48,64})for(int i=0;i<3;i++)row.add(e.read(p+off+4*i,4));row.add(e.read(p+80,1));}
 String text(Z119Execution e,long p)throws Exception{int n=0;while(n<1024&&e.read(p+n,1)!=0)n++;if(n==1024)throw new IllegalStateException("Unbounded diagnostic text");return new String(e.emulator.readMemory(e.address(p),n),StandardCharsets.UTF_8);}
 void boundary(Z119Execution e,long target)throws Exception{
  if(e.arithmetic(target))return;
  switch((int)target){
  case 0x40081388:e.countBoundary("prescribed_millis");e.register(10,now);return;
  case 0x4008c2b4:e.countBoundary("diagnostic_basename");return;
  case 0x400d21cc:e.countBoundary("diagnostic_output");return;
  case 0x400db014:{List<Long> result=new ArrayList<>(List.of(0L,e.register(11)));String file=text(e,e.register(10));file=file.substring(file.lastIndexOf(92)+1);for(String value:List.of(file,text(e,e.register(12)))){byte[] b=value.getBytes(StandardCharsets.UTF_8);result.add((long)b.length);for(byte v:b)result.add(v&255L);}effects.add(result);e.invoke(target);return;}
  case 0x401289a0:if(e.register(10)!=200)throw new IllegalStateException("Unexpected assertion delay");e.countBoundary("assertion_delay_200ms");effects.add(List.of(1L,200L));return;
  case 0x401367a0:throw new IllegalStateException("Original stack guard failed");
  case 0x400014c0:{long p=e.register(10);int n=0;while(n<1024&&e.read(p+n,1)!=0)n++;if(n==1024)throw new IllegalStateException("Unbounded strlen");e.register(10,n);e.countBoundary("ROM_strlen");return;}
  case 0x4000c2c8:{int n=(int)e.register(12);if(n<0||n>4096)throw new IllegalStateException("Unbounded memcpy");byte[] data=e.emulator.readMemory(e.address(e.register(11)),n);if(n>0)e.emulator.writeMemory(e.address(e.register(10)),data);e.countBoundary("ROM_memcpy");return;}
  case 0x4000c44c:{int n=(int)e.register(12);if(n<0||n>4096)throw new IllegalStateException("Unbounded memset");byte[] data=new byte[n];Arrays.fill(data,(byte)e.register(11));if(n>0)e.emulator.writeMemory(e.address(e.register(10)),data);e.countBoundary("ROM_memset");return;}
  case 0x400f51b8:{if(e.register(10)!=C+0x6e58)throw new IllegalStateException("Wrong Head Azure queue");long p=e.register(11);List<Long> result=new ArrayList<>(List.of(2L,e.read(p+4,4),e.read(p+8,4)));params(e,p+16,result);effects.add(result);e.countBoundary("Head_Azure_queue_put");return;}
  case 0x400f3d3c:{if(e.register(10)!=C+0x278)throw new IllegalStateException("Wrong Core queue");long p=e.register(11),data=p+0x1e0,recipe=data+8;List<Long> result=new ArrayList<>(List.of(3L,e.read(p+4,4),e.read(data,1),e.read(recipe,4),e.read(recipe+84,4)));params(e,data+0xb8,result);int n=0;while(n<65&&e.read(recipe+4+n,1)!=0)n++;result.add((long)n);for(int i=0;i<n;i++)result.add(e.read(recipe+4+i,1));effects.add(result);e.countBoundary("Core_command_queue_put");return;}
  case 0x400edaac:if(e.register(10)!=C+0x15160)throw new IllegalStateException("Wrong Azure object");effects.add(List.of(4L));e.countBoundary("Azure_discard_session");return;
  default:if(target>=0x400d0000L&&target<0x40120000L||target>=0x401bf000L&&target<0x401c0000L){e.invoke(target);return;}throw new IllegalStateException(String.format("Unreviewed remote dependency %08x",target));
  }
 }
 public void run()throws Exception{
  String[] args=getScriptArgs();JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();JsonObject layout=JsonParser.parseString(Files.readString(Path.of(args[1]))).getAsJsonObject();byte[] raw=Files.readAllBytes(Path.of(layout.get("input_path").getAsString()));if(!sha(raw).equals(Z119Execution.IMAGE_SHA))throw new IllegalStateException("Wrong image");
  for(JsonElement value:layout.getAsJsonArray("segments")){JsonObject s=value.getAsJsonObject();int size=s.get("size").getAsInt();byte[] loaded=new byte[size];if(size>0)currentProgram.getMemory().getBytes(toAddr(s.get("address").getAsLong()),loaded);int off=s.get("offset").getAsInt();if(!Arrays.equals(loaded,Arrays.copyOfRange(raw,off,off+size)))throw new IllegalStateException("Project bytes differ");}
  List<Object> results=new ArrayList<>(),functions=new ArrayList<>();try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary)){
   int index=0;for(JsonElement value:inputs){JsonObject c=value.getAsJsonObject();e.resetRegisters();e.clear(C,0x16000);e.clear(R,0x1000);effects.clear();now=c.get("clock").getAsLong();e.write(0x3ffc4654L,0x5a61c0deL,4);e.write(0x3ffc2e84L,7,4);e.write(C+0x14,c.get("quiet").getAsLong(),4);e.write(C+0x159a8,c.get("activity").getAsLong(),4);e.write(C+0x828,1,1);e.write(R+4,c.get("command").getAsLong(),4);e.write(R+0xb4,c.get("session").getAsLong(),4);e.write(R+0xbc,c.get("program").getAsLong(),4);e.write(R+0xc0,c.get("recipe").getAsLong(),4);byte[] name=c.get("name").getAsString().getBytes(StandardCharsets.UTF_8);e.emulator.writeMemory(e.address(R+0xc4),name);e.write(R+0xc4+name.length,0,1);JsonArray params=c.getAsJsonArray("parameters");int j=0;for(int i=0;i<3;i++)e.write(R+0x140+4*i,params.get(j++).getAsLong(),4);for(int off:new int[]{16,32,48,64})for(int i=0;i<3;i++)e.write(R+0x140+off+4*i,params.get(j++).getAsLong(),4);e.write(R+0x190,params.get(j).getAsLong(),1);
    e.register(10,C);e.register(11,R);try{e.invoke(0x401021b8L);}catch(Exception ex){throw new IllegalStateException("Case "+index+" "+c,ex);}results.add(row("effects",new ArrayList<>(effects),"assertion_count",e.read(0x3ffc2e84L,4),"use_azure",e.read(C+0x828,1)));index++;
   }
   for(String a:e.functions.keySet()){Function f=getFunctionAt(toAddr(Long.parseLong(a.substring(2),16)));if(f==null)throw new IllegalStateException("Undefined function "+a);List<Object> ranges=new ArrayList<>();for(AddressRange r:f.getBody().getAddressRanges()){byte[] b=new byte[(int)r.getLength()];currentProgram.getMemory().getBytes(r.getMinAddress(),b);ranges.add(row("address",r.getMinAddress().getOffset(),"size",b.length,"sha256",sha(b)));}functions.add(row("address",a,"calls",e.functions.get(a),"ranges",ranges));}
   Files.writeString(Path.of(args[2]),new GsonBuilder().setPrettyPrinting().create().toJson(row("input_sha256",Z119Execution.IMAGE_SHA,"cases",results,"original_functions",functions,"modeled_boundaries",e.boundaries,"hardware_tested",false,"full_cpu_emulation",false))+"\n");
  }println("Executed "+results.size()+" remote sessions");
 }
}
