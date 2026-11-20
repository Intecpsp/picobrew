// Original auxiliary sensor setup, register I/O and compensation execution.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.io.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulateAuxiliarySensors extends GhidraScript {
    private static final long OBJECT=0x3ff50000L,SENSORS=OBJECT+0x1000,WIRE=OBJECT+0x2000,VTABLE=OBJECT+0x3000;
    private static final long WRITE=0x3ffe0100L,READ=0x3ffe0104L;
    private List<List<Long>> events;
    private JsonObject replies;
    private JsonArray endResults,readBytes;
    private int endIndex,readIndex,reg,writes;
    private long now;
    private void event(long... values){List<Long> row=new ArrayList<>();for(long x:values)row.add(x);while(row.size()<4)row.add(0L);events.add(row);}
    private void boundary(Z119Execution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        switch((int)target){
            case 0x401c025c:
                e.countBoundary("__divdi3");long n=e.register(10)|(e.register(11)<<32),d=e.register(12)|(e.register(13)<<32);
                if(d==0)throw new IllegalStateException("Original division by zero");long quotient=n/d;
                e.register(10,quotient);e.register(11,quotient>>>32);return;
            case 0x401c0120:
                e.countBoundary("__floatdisf");long integer=e.register(10)|(e.register(11)<<32);
                e.register(10,Float.floatToRawIntBits((float)integer));return;
            case 0x40081388:e.countBoundary("clock_millis");e.register(10,now);return;
            case 0x4008c2b4:e.countBoundary("basename");return;
            case 0x400d21cc:e.countBoundary("diagnostic_log");return;
            case 0x400db014:e.countBoundary("returning_assertion_diagnostic");return;
            case 0x401367a0:throw new IllegalStateException("Original stack canary failed");
            case 0x401bfe3c:
                e.countBoundary("beginTransmission");event(0,e.register(11));writes=0;return;
            case (int)WRITE:
                e.countBoundary("write");long value=e.register(11)&255;if(writes++==0)reg=(int)value;
                event(1,value);e.register(10,1);return;
            case 0x40125ac8:
                e.countBoundary("endTransmission_default_stop");event(2,1);
                e.register(10,endIndex<endResults.size()?endResults.get(endIndex).getAsLong():0);endIndex++;return;
            case 0x40125ab4:
                e.countBoundary("requestFrom_default_stop");event(4,e.register(11),e.register(12),1);
                JsonObject reply=replies.getAsJsonObject(Integer.toString(reg));
                if(reply==null)throw new IllegalStateException("No original reply for register "+reg);
                readBytes=reply.getAsJsonArray("bytes");readIndex=0;e.register(10,reply.get("count").getAsLong());return;
            case (int)READ:
                e.countBoundary("read");if(readIndex>=readBytes.size())throw new IllegalStateException("Read exceeded fixture");
                long byteValue=readBytes.get(readIndex++).getAsLong();event(5,byteValue);e.register(10,byteValue);return;
            case 0x400d16b0:case 0x400d183c:case 0x400d1898:e.invoke(target);return;
            default:throw new IllegalStateException(String.format("Unmodeled auxiliary call %08x",target));
        }
    }
    private boolean lis(JsonObject item){return item.get("kind").getAsString().endsWith("lis");}
    private void load(Z119Execution e,JsonObject item)throws Exception{
        e.resetRegisters();e.clear(OBJECT,0x4000);e.write(0x3ffc4654L,0x11928055L,4); // Fixture-owned stack canary.
        events=new ArrayList<>();replies=item.getAsJsonObject("replies");
        endResults=item.getAsJsonArray("ends");endIndex=0;readIndex=0;writes=0;
        JsonArray words=item.getAsJsonArray("words"),values=item.getAsJsonArray("values");
        now=words.get(9).getAsLong();e.write(OBJECT+4,WIRE,4);e.write(OBJECT+8,words.get(0).getAsLong(),1);e.write(OBJECT+12,SENSORS,4);
        e.write(WIRE,VTABLE,4);e.write(VTABLE+8,WRITE,4);e.write(VTABLE+20,READ,4);
        if(lis(item)){
            for(int i=0;i<3;i++)e.write(OBJECT+0x10+i*2,words.get(2+i).getAsLong(),2);
            e.write(OBJECT+0x16,words.get(1).getAsLong(),1);
        }else{
            e.write(OBJECT+0x10,words.get(5).getAsLong(),8);e.write(OBJECT+0x18,words.get(6).getAsLong(),8);
            for(int i=0;i<12;i++)e.write(OBJECT+0x20+i*2,words.get(10+i).getAsLong(),2);
            e.write(OBJECT+0x38,words.get(7).getAsLong(),4);e.write(OBJECT+0x3c,words.get(1).getAsLong(),1);
        }
        for(int i=0;i<3;i++)e.writeDouble(SENSORS+0x260+i*8,values.get(i).getAsDouble());
        e.writeDouble(SENSORS+0x250,values.get(3).getAsDouble());e.write(SENSORS+0x258,words.get(8).getAsLong(),4);
        e.writeDouble(SENSORS+0x278,values.get(4).getAsDouble());
    }
    private Map<String,Object> snapshot(Z119Execution e,JsonObject item)throws Exception{
        List<Long> words=new ArrayList<>();for(JsonElement word:item.getAsJsonArray("words"))words.add(word.getAsLong());
        words.set(0,e.read(OBJECT+8,1));words.set(1,e.read(OBJECT+(lis(item)?0x16:0x3c),1));
        if(lis(item)){for(int i=0;i<3;i++)words.set(2+i,(long)(short)e.read(OBJECT+0x10+i*2,2));}
        else{
            words.set(5,e.read(OBJECT+0x10,8));words.set(6,e.read(OBJECT+0x18,8));words.set(7,(long)(int)e.read(OBJECT+0x38,4));
            for(int i=0;i<12;i++){long value=e.read(OBJECT+0x20+i*2,2);words.set(10+i,(i==0||i==3)?value:(long)(short)value);}
        }
        words.set(8,e.read(SENSORS+0x258,4));
        List<Double> values=new ArrayList<>();for(int i=0;i<3;i++)values.add(e.readDouble(SENSORS+0x260+i*8));
        values.add(e.readDouble(SENSORS+0x250));values.add(e.readDouble(SENSORS+0x278));
        Map<String,Object> result=new LinkedHashMap<>();result.put("id",item.get("id").getAsString());result.put("words",words);result.put("values",values);result.put("events",events);return result;
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input JSON and output JSONL");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();int count=0;Gson json=new Gson();
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){
            for(JsonElement element:inputs){JsonObject item=element.getAsJsonObject();load(e,item);String kind=item.get("kind").getAsString();long target;
                switch(kind){case "setup_lis":target=0x400de078L;break;case "tick_lis":target=0x400d1720L;break;
                    case "setup_bmp":target=0x400de10cL;break;case "calibration_bmp":target=0x400d1898L;break;
                    case "tick_bmp":target=0x400d91acL;break;default:throw new IllegalArgumentException("Unknown auxiliary scenario");}
                e.register(10,OBJECT);e.invoke(target);out.write(json.toJson(snapshot(e,item))+"\n");count++;
            }
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);report.put("cases",count);
            report.put("scope","Original LIS3DHTR/BMP280 setup and tick, nested register-read and calibration bodies; explicit TwoWire, compiler arithmetic, clock and diagnostic boundaries; register windows adapted at ENTRY/RETW");
            report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);report.put("full_cpu_or_hardware_emulation",false);report.put("hardware_tested",false);
            Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+count+" original auxiliary sensor scenarios");
    }
}
