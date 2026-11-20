// Execute all Core component registry bodies with real Shell constructors.
// Generic Shell insertion is a typed capture boundary, independently replayed
// by EmulateConsole. No registered command body is replaced by a success here.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.io.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
public class EmulateApplicationConsole extends GhidraScript {
    private static final long CORE=0x3ff10000L,HEAP=0x3ff40000L;
    private long next,clock;private List<Object> entries,assertions;
    private Map<Long,Long> allocations=new LinkedHashMap<>();
    private Set<Long> registration=Set.of(0x400f65f8L,0x400f66f4L,0x400f6b18L,0x400f6c14L,0x400f72e4L,0x400f73e0L,0x400f74e0L,0x400f75dcL,0x400f76d8L,0x400f77d4L,0x400f78d0L,0x400f79ccL,0x400f7ac8L,0x400f7bc4L,0x40112160L);
    private Set<Long> children=Set.of(0x400efd40L,0x400f4284L,0x400f44bcL,0x400f5320L);
    private String text(Z119Execution e,long pointer)throws Exception {
        if(pointer==0)throw new IllegalStateException("Unexpected null string");
        ByteArrayOutputStream out=new ByteArrayOutputStream();for(int i=0;i<4096;i++){int b=(int)e.read(pointer+i,1);if(b==0)return out.toString(StandardCharsets.UTF_8);out.write(b);}throw new IllegalStateException("Unterminated string");
    }
    private String shell(Z119Execution e,long pointer)throws Exception {return text(e,e.read(pointer+4,4));}
    private void boundary(Z119Execution e,long target)throws Exception {
        if(e.arithmetic(target))return;
        if(registration.contains(target)) {
            e.countBoundary("Shell_add_command_capture");Map<String,Object> row=new LinkedHashMap<>();
            row.put("shell",shell(e,e.register(10)));row.put("exact",e.register(11)!=0);
            row.put("token",text(e,e.register(12)));row.put("description",text(e,e.register(13)));
            row.put("callback",String.format("0x%08x",e.read(e.register(14)+12,4)));entries.add(row);return;
        }
        if(children.contains(target)) {
            e.countBoundary("Shell_add_child_capture");Map<String,Object> row=new LinkedHashMap<>();
            row.put("shell",shell(e,e.register(10)));row.put("token",text(e,e.register(11)));
            row.put("child",shell(e,e.register(12)));entries.add(row);return;
        }
        switch((int)target) {
        case 0x4019d194:{e.countBoundary("Cxx_allocate");long size=e.register(10),pointer=next;next+=(size+15)&~15L;
            if(next>=HEAP+0x10000)throw new IllegalStateException("Fixture heap exhausted");e.clear(pointer,(int)size);allocations.put(pointer,size);e.register(10,pointer);return;}
        case 0x4019d15c:e.countBoundary("Cxx_deallocate");return;
        case 0x401bf3c0:e.countBoundary("functor_temporary_destruction");return;
        case 0x4000c44c:{e.countBoundary("ROM_memset");byte[] bytes=new byte[(int)e.register(12)];Arrays.fill(bytes,(byte)e.register(11));e.emulator.writeMemory(e.address(e.register(10)),bytes);return;}
        case 0x4000c2c8:e.countBoundary("ROM_memcpy");if(e.register(12)>0)e.emulator.writeMemory(e.address(e.register(10)),e.emulator.readMemory(e.address(e.register(11)),(int)e.register(12)));return;
        case 0x400db014:assertions.add(List.of(text(e,e.register(10)),e.register(11),text(e,e.register(12))));e.invoke(target);return;
        case 0x40081388:e.countBoundary("millis");e.register(10,clock);return;
        case 0x401289a0:e.countBoundary("assertion_delay_200ms");clock=(clock+e.register(10))&0xffffffffL;return;
        case 0x4008c2b4:{e.countBoundary("diagnostic_basename");String value=text(e,e.register(10));e.register(10,e.register(10)+Math.max(value.lastIndexOf('/'),value.lastIndexOf('\\'))+1);return;}
        case 0x400d21cc:e.countBoundary("diagnostic_output");return;
        default:
            // Constructors and ownership moves stay original instructions.
            if(target>=0x400d0000L&&target<=0x40113000L){e.invoke(target);return;}
            throw new IllegalStateException(String.format("Unreviewed registry call %08x",target));
        }
    }
    @Override public void run()throws Exception {
        String[] args=getScriptArgs();JsonArray cases=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))) {
            next=HEAP;clock=1000;entries=new ArrayList<>();assertions=new ArrayList<>();allocations.clear();e.clear(CORE,0x15b00);e.write(0x3ffc4654L,0x5a5a5a5aL,4);e.write(0x3ffc2e84L,7,4);
            e.write(CORE+0x4680+0x90,CORE+0x4660,4);
            for(JsonElement value:cases) {
                JsonObject c=value.getAsJsonObject();entries.clear();assertions.clear();e.resetRegisters();
                long address=Long.decode(c.get("function").getAsString());if(currentProgram.getListing().getInstructionAt(toAddr(address))==null)disassemble(toAddr(address));
                e.register(10,CORE+c.get("offset").getAsLong());e.invoke(address);
                Map<String,Object> result=new LinkedHashMap<>();result.put("component",c.get("component").getAsString());result.put("entries",entries);result.put("assertions",assertions);result.put("clock",clock);result.put("counter",e.read(0x3ffc2e84L,4));
                List<Object> shells=new ArrayList<>();for(Map.Entry<Long,Long> allocation:allocations.entrySet())shells.add(List.of(shell(e,allocation.getKey()),(allocation.getValue()-12)/32));
                result.put("shells",shells);out.write(new Gson().toJson(result)+"\n");out.flush();
            }
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);report.put("input_file_sha256",HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(Path.of(args[0])))));report.put("cases",cases.size());report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
    }
}
