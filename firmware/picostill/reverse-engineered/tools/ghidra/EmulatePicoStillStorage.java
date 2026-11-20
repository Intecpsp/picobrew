import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.util.*;
import java.util.zip.*;
import com.google.gson.*;

public class EmulatePicoStillStorage extends EmulatePicoStillController {
    private static final long BASE=0x3ff00000L,RECORD=BASE+0x100,KEY=BASE+0x200,INITIAL=BASE+0x1000,COUNTER=0x3ffc3078L;
    private JsonObject input;private byte[] blob;private boolean missing;private int allocCount;
    private Map<Long,Integer> allocations;
    private int setting(String key){return input.get(key).getAsInt();}
    private String bytes(byte[] data){StringBuilder s=new StringBuilder();for(byte b:data)s.append(String.format("%02x",b&255));return s.toString();}
    @Override protected void boundary(PicoStillExecution e,long target)throws Exception {
        switch((int)target){
        case 0x400e811c:{e.countBoundary("nvs_open");if(e.register(11)!=1)throw new IllegalStateException("Wrong open mode");events.add("open:"+string(e,e.register(10)));
            int result=setting("opened");if(result==0)e.write(e.register(12),7,4);e.register(10,result);return;}
        case 0x400e83a0:{e.countBoundary("nvs_get_blob");long pointer=e.register(12),length=e.register(13);int capacity=(int)e.read(length,4);
            events.add("get:"+e.register(10)+":"+string(e,e.register(11))+":"+(pointer==0?"size":"data")+":"+capacity);
            int result=setting(pointer==0?"query":"read");if(result==0 && missing)result=0x1102;
            if(result==0){if(pointer!=0 && capacity<blob.length)result=0x110c;else{if(pointer!=0)e.emulator.writeMemory(e.address(pointer),blob);e.write(length,blob.length,4);}}
            e.register(10,result);return;}
        case 0x400e82e8:{e.countBoundary("nvs_set_blob");String key=string(e,e.register(11));byte[] data=e.emulator.readMemory(e.address(e.register(12)),(int)e.register(13));
            events.add("set:"+e.register(10)+":"+key+":"+bytes(data));boolean temp=key.equals("expand");int result=setting(temp?"expand":"write");
            if(result==0&&!temp){blob=data;missing=false;}e.register(10,result);return;}
        case 0x400e81e0:{e.countBoundary("nvs_erase_key");String key=string(e,e.register(11));events.add("erase:"+e.register(10)+":"+key);
            if(key.equals("eeprom")){blob=new byte[0];missing=true;}e.register(10,0);return;}
        case 0x400e82ac:e.countBoundary("nvs_commit");events.add("commit:"+e.register(10));e.register(10,0);return;
        case 0x4000bea0:{e.countBoundary("ROM_malloc");int size=(int)e.register(10);allocCount++;events.add("allocate:"+size+":"+allocCount);
            if(allocCount==setting("allocation")){e.register(10,0);return;}long address=BASE+0x3000+allocCount*0x1000;
            byte[] data=new byte[size];Arrays.fill(data,(byte)0xa5);e.emulator.writeMemory(e.address(address),data);allocations.put(address,allocCount);e.register(10,address);return;}
        case 0x4000beb8:case 0x4012cce4:{e.countBoundary(target==0x4000beb8L?"ROM_free":"operator_delete_array");long address=e.register(10);
            if(!allocations.containsKey(address))throw new IllegalStateException("Unknown allocation");events.add("release:"+allocations.remove(address)+":"+(target==0x4012cce4L?1:0));return;}
        case 0x400de168:{e.countBoundary("storage_error_format_output");long line=e.register(12),status=0,oldSize=0,newSize=0;
            if(line==0x20||line==0x27)status=e.register(14);else if(line==0x3f){oldSize=e.register(14);newSize=e.register(15);}
            events.add("storage_error:"+line+":"+status+":"+oldSize+":"+newSize);return;}
        case 0x400d2abc:e.countBoundary("storage_size_diagnostic");events.add("key_size:"+e.register(12));return;
        default:super.boundary(e,target);
        }
    }
    private Map<String,Object> storage(PicoStillExecution e,JsonObject in)throws Exception {
        input=in;events=new ArrayList<>();allocations=new HashMap<>();allocCount=0;clock=1000;e.resetRegisters();e.clear(BASE,0xa000);e.write(COUNTER,0,4);
        missing=setting("blob")<0;blob=new byte[Math.max(setting("blob"),0)];for(int i=0;i<blob.length;i++)blob[i]=(byte)(i*17+3);
        e.write(BASE,setting("requested"),4);e.write(BASE+4,9,4);e.write(BASE+12,setting("size"),4);e.write(BASE+16,setting("dirty"),1);e.write(BASE+20,KEY,4);
        e.emulator.writeMemory(e.address(KEY),"eeprom\0".getBytes(StandardCharsets.US_ASCII));
        if(setting("present")!=0){e.write(BASE+8,INITIAL,4);allocations.put(INITIAL,0);for(int i=0;i<setting("size");i++)e.write(INITIAL+i,i*13+5,1);}
        for(int i=0;i<100;i++)e.write(RECORD+i,i*7+2,1);e.write(RECORD,in.get("version").getAsLong(),4);
        String op=in.get("op").getAsString();long result=0;
        if(op.equals("begin")){call(e,0x400d3350L,BASE+4,setting("requested"));result=e.register(10);}
        else if(op.equals("initialize"))call(e,0x400d4130L,BASE);
        else if(op.equals("load"))call(e,0x400d4830L,BASE,RECORD);
        else if(op.equals("save"))call(e,0x400d3534L,BASE,RECORD);
        else throw new IllegalArgumentException("Unknown operation");
        long size=e.read(BASE+12,4),buffer=e.read(BASE+8,4);
        Map<String,Object> out=new LinkedHashMap<>();out.put("id",in.get("id").getAsString());out.put("values",Arrays.asList(result,size,e.read(BASE+4,4),e.read(BASE+16,1),missing?1:0));
        out.put("bytes",Arrays.asList(bytes(blob),buffer==0||size==0?"":hex(e,buffer,(int)size),hex(e,RECORD,100)));out.put("events",events);out.put("assertion_count",e.read(COUNTER,4));out.put("clock",clock);return out;
    }
    @Override public void run()throws Exception {
        nested.add(0x400d3350L);String[] args=getScriptArgs();JsonArray inputs;
        try(GZIPInputStream stream=new GZIPInputStream(Files.newInputStream(Path.of(args[0])))){inputs=JsonParser.parseReader(new InputStreamReader(stream,StandardCharsets.UTF_8)).getAsJsonArray();}
        Map<String,Object> report=new LinkedHashMap<>();List<Map<String,Object>> cases=new ArrayList<>();
        try(PicoStillExecution e=new PicoStillExecution(currentProgram,monitor,this::boundary)){
            for(JsonElement in:inputs)cases.add(storage(e,in.getAsJsonObject()));report.put("input_sha256",PicoStillExecution.IMAGE_SHA);report.put("cases_input_gzip_sha256",sha(Path.of(args[0])));report.put("cases",cases);
            report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original MemoryHandler begin/initialize/load/save instructions, actual returning assertion bodies. NVS, heap allocation (deterministic 0xa5 fill), diagnostic formatting, ROM memory and clock/delay are explicit boundaries. The owned ESP32 allocator zeroes temporary capacity-probe bytes, an intentional difference from original uninitialized malloc contents.");
            report.put("hardware_tested",false);report.put("full_cpu_emulation",false);
        }
        try(GZIPOutputStream out=new GZIPOutputStream(Files.newOutputStream(Path.of(args[1])))){out.write((new Gson().toJson(report)+"\n").getBytes(StandardCharsets.UTF_8));}
        println("Executed "+cases.size()+" original PicoStill storage cases");
    }
}
