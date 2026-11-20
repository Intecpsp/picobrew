// Original System::setup, console registration, and returning assertion helper.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.security.MessageDigest;
import java.io.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulateSystemSetup extends GhidraScript {
    private static final long SYSTEM=0x3ff30000L,SHELL=0x3ff35000L,BUZZER=0x3ff36000L;
    private long[] input;
    private long timestamp;
    private List<Long> events;
    private List<List<Object>> commands;
    private List<String> namespaces;
    private String text(Z119Execution e,long address)throws Exception {
        StringBuilder s=new StringBuilder();
        for(int i=0;i<256;i++){int c=(int)e.read(address+i,1);if(c==0)return s.toString();s.append((char)c);}
        throw new IllegalStateException("Unterminated original string");
    }
    private void event(long kind,long... args) {
        events.add(kind);for(int i=0;i<4;i++)events.add(i<args.length?args[i]&0xffffffffL:0L);
    }
    private void boundary(Z119Execution e,long target)throws Exception {
        if(e.arithmetic(target))return;
        switch((int)target) {
        case 0x4013742c:e.countBoundary("esp_ota_get_boot_partition");event(0);e.register(10,input[4]);return;
        case 0x40137360:e.countBoundary("esp_ota_get_running_partition");event(1);e.register(10,input[5]);return;
        case 0x40133fdc:e.countBoundary("esp_partition_find_first_factory");event(2,e.register(10),e.register(11),e.register(12));e.register(10,input[6]);return;
        case 0x40125764:e.countBoundary("SPI_setDataMode");event(3,e.register(11));return;
        case 0x40125738:e.countBoundary("SPI_setFrequency");event(4,e.register(11));return;
        case 0x40125680:e.countBoundary("SPI_begin");event(5,e.register(11),e.register(12),e.register(13),e.register(14));return;
        case 0x401258c0:e.countBoundary("Wire_begin");event(e.register(10)==0x3ff32000L?6:7,e.register(11),e.register(12),e.register(13));return;
        case 0x40112f04:e.countBoundary("System_scan_buses");event(8);return;
        case 0x40136b78:e.countBoundary("esp_task_wdt_init");event(9,e.register(10),e.register(11));e.register(10,input[2]);return;
        case 0x40136db4:e.countBoundary("esp_task_wdt_add");event(10,e.register(10));e.register(10,input[3]);return;
        case 0x400f7f48:event(11);e.invoke(target);return;
        case 0x400f5084:e.countBoundary("Shell_allocate_System");namespaces.add(text(e,e.register(11)));e.write(e.register(10),SHELL,4);return;
        case 0x400f7d74:e.countBoundary("shared_ptr_assign_shell");e.write(e.register(10),e.read(e.register(11),4),4);return;
        case 0x400f7d2c:case 0x401bf3c0:e.countBoundary("Cxx_temporary_destructor");return;
        case 0x400f6b18:
            e.countBoundary("Shell_register_command");
            commands.add(Arrays.asList(e.register(11),text(e,e.register(12)),text(e,e.register(13))));return;
        case 0x400efd40:
            e.countBoundary("Shell_register_child");namespaces.add(text(e,e.register(11)));
            if(e.register(12)!=0x3ff37000L)throw new IllegalStateException("Wrong buzzer shell");return;
        case 0x4008c2b4:
            e.countBoundary("diagnostic_basename");
            String path=text(e,e.register(10));e.register(10,e.register(10)+Math.max(path.lastIndexOf('\\'),path.lastIndexOf('/'))+1);return;
        case 0x400db014:e.invoke(target);return;
        case 0x40081388:e.countBoundary("millis");events.add(20L);events.add(timestamp);e.register(10,timestamp);return;
        case 0x400d21cc:
            e.countBoundary("assertion_log");
            if(e.register(10)!=0x3f403d1dL||!text(e,e.register(14)).equals("System.h")||
                !text(e,e.read(e.register(1),4)).equals("ESP_OK == ret"))throw new IllegalStateException("Unexpected assertion log");
            events.addAll(Arrays.asList(21L,e.register(15),e.register(12),e.register(13),e.read(0x3ffc2e84L,4)));return;
        case 0x401289a0:e.countBoundary("delay");events.addAll(Arrays.asList(22L,e.register(10),e.read(0x3ffc2e84L,4)));timestamp=(timestamp+e.register(10))&0xffffffffL;return;
        default:throw new IllegalStateException(String.format("Unreviewed System setup call %08x",target));
        }
    }
    private Map<String,Object> one(Z119Execution e,JsonObject item)throws Exception {
        input=new long[7];for(int i=0;i<input.length;i++)input[i]=item.getAsJsonArray("input").get(i).getAsLong();
        timestamp=input[0];events=new ArrayList<>();commands=new ArrayList<>();namespaces=new ArrayList<>();
        e.resetRegisters();e.clear(SYSTEM,0x1000);e.clear(BUZZER,0x100);
        e.write(SYSTEM+0x24,0x3ff32000L,4);e.write(SYSTEM+0x58,0x3ff33000L,4);
        e.write(SYSTEM+0x90,BUZZER,4);e.write(BUZZER+0x1c,0x3ff37000L,4);
        e.write(0x3ffc4654L,0x1195a5a5L,4); // Prescribed initialized SDK stack guard.
        e.write(0x3ffc2e84L,input[1],4);e.register(10,SYSTEM);e.invoke(0x40112f88L);
        Map<String,Object> result=new LinkedHashMap<>();
        result.put("output",Arrays.asList(timestamp,e.read(0x3ffc2e84L,4),e.read(SYSTEM+0xd0,4),
            e.read(SYSTEM+0xcc,4),e.read(SYSTEM+0xc8,4),input[2],input[3]));
        result.put("events",events);result.put("result",0);result.put("commands",commands);result.put("namespaces",namespaces);
        return result;
    }
    @Override public void run()throws Exception {
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Input JSON and output JSONL required");
        byte[] bytes=Files.readAllBytes(Path.of(args[0]));JsonArray cases=JsonParser.parseString(new String(bytes,java.nio.charset.StandardCharsets.UTF_8)).getAsJsonArray();
        Gson gson=new Gson();
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);
            BufferedWriter output=Files.newBufferedWriter(Path.of(args[1]))) {
            int index=0;for(JsonElement item:cases){try{output.write(gson.toJson(one(e,item.getAsJsonObject()))+"\n");}
                catch(Exception problem){throw new IllegalStateException("System setup case "+index,problem);}index++;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);
            report.put("input_file_sha256",HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes)));
            report.put("cases",index);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original System setup, console registration and returning assertion-helper instructions. Partition/SPI/I2C/watchdog SDK operations, bus scans, shell allocation/registration, C++ closure lifetime, arithmetic, diagnostic formatting and delay are explicit boundaries. Full ordered effects, assertion counter, float-derived timestamp bits and command descriptors are compared. No actual partition, bus, watchdog, interrupt, console invocation or complete-application execution claim.");
            Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+cases.size()+" original System setup cases");
    }
}
