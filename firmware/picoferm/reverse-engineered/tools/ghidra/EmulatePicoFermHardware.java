import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.util.*;
import java.util.zip.*;
import com.google.gson.*;

public class EmulatePicoFermHardware extends EmulatePicoFermCore {
    protected static final long ACQ=0x3ff00000L,ADC=ACQ+4,LED=ACQ+0x100,INPUT=ACQ+0x400;
    protected long now,analog;protected List<Long> reads;protected int readIndex;
    protected double doubleArg(PicoFermExecution e,int register){return Double.longBitsToDouble(e.register(register)|(e.register(register+1)<<32));}
    protected void doubleResult(PicoFermExecution e,double result){long bits=Double.doubleToRawLongBits(result);e.register(2,bits);e.register(3,bits>>>32);}
    @Override protected void boundary(PicoFermExecution e,long target)throws Exception{
        switch((int)target){
        case 0x40106870:e.countBoundary("Arduino_millis");events.add("millis:"+now);e.register(2,now);return;
        case 0x40201b04:e.countBoundary("Arduino_analogRead");events.add("analog:"+e.register(2));e.register(2,analog);return;
        case 0x401069d0:e.countBoundary("Arduino_digitalWrite");events.add("write:"+e.register(2)+":"+((e.register(3)&255)!=0?1:0));return;
        case 0x4020b44c:e.countBoundary("TwoWire_constructor");e.write(e.register(2),0,4);e.write(e.register(2)+4,0,4);e.write(e.register(2)+8,1000,4);return;
        case 0x4020b464:e.countBoundary("TwoWire_begin");events.add("wire_begin:"+e.register(3)+":"+e.register(4));return;
        case 0x4020b4f0:e.countBoundary("TwoWire_beginTransmission");events.add("wire_begin_transmission:"+e.register(3));return;
        case 0x4020b40c:e.countBoundary("TwoWire_write");events.add("wire_write:"+(e.register(3)&255));e.register(2,1);return;
        case 0x4020b548:e.countBoundary("TwoWire_endTransmission");events.add("wire_end");e.register(2,0);return;
        case 0x4020b4d0:e.countBoundary("TwoWire_requestFrom");events.add("wire_request:"+e.register(3)+":"+e.register(4));e.register(2,e.register(4));return;
        case 0x4020b374:{e.countBoundary("TwoWire_read");if(readIndex>=reads.size())throw new IllegalStateException("Exhausted I2C samples");long value=reads.get(readIndex++);events.add("wire_read:"+value);e.register(2,value);return;}
        case 0x40000100:e.countBoundary("AcquisitionEvents_measurement");events.add("measurement:"+bits(e.read(e.register(3),4))+":"+e.read(e.register(3)+4,4));return;
        case 0x40000104:e.countBoundary("LedEvents_animation_changed");events.add("animation:"+e.register(3));return;
        case 0x4020d074:e.countBoundary("Arduino_String_equals");e.register(2,e.string(e.register(2)).equals(e.string(e.register(3)))?1:0);return;
        case 0x4000e2f0:e.countBoundary("ROM_floatsidf");doubleResult(e,(double)(int)e.register(2));return;
        case 0x4000c8f0:e.countBoundary("ROM_muldf3");doubleResult(e,doubleArg(e,2)*doubleArg(e,4));return;
        case 0x4000c538:e.countBoundary("ROM_adddf3");doubleResult(e,doubleArg(e,2)+doubleArg(e,4));return;
        case 0x4000c688:e.countBoundary("ROM_subdf3");doubleResult(e,doubleArg(e,2)-doubleArg(e,4));return;
        case 0x4000cb94:e.countBoundary("ROM_divdf3");doubleResult(e,doubleArg(e,2)/doubleArg(e,4));return;
        case 0x4000cd5c:e.countBoundary("ROM_truncdfsf2");e.register(2,Float.floatToRawIntBits((float)doubleArg(e,2)));return;
        case 0x4000cdfc:e.countBoundary("ROM_extendsfdf2");doubleResult(e,(double)Float.intBitsToFloat((int)e.register(2)));return;
        case 0x4020e24c:e.countBoundary("newlib_errno_location");e.register(2,0x3ff00500L);return;
        case 0x4021484c:e.countBoundary("newlib_matherr_default");e.register(2,0);return;
        default:super.boundary(e,target);
        }
    }
    private Map<String,Object> hardware(PicoFermExecution e,JsonObject in)throws Exception{
        e.resetRegisters();e.clear(ACQ,8192);events=new ArrayList<>();reads=new ArrayList<>();for(JsonElement x:in.getAsJsonArray("reads"))reads.add(x.getAsLong());readIndex=0;
        long[] w=new long[24];for(int i=0;i<w.length;i++)w[i]=in.getAsJsonArray("words").get(i).getAsLong();now=w[4];analog=w[5];
        e.write(ADC+0x10,w[0],1);e.write(ADC+0x14,w[1],4);
        e.write(ACQ,ACQ+0x300,4);e.write(ACQ+0x300,ACQ+0x340,4);e.write(ACQ+0x340,0x40000100L,4);
        for(int i=0;i<4;i++)e.write(ACQ+0x1c+4*i,w[8+i],4);
        e.write(LED,4,1);e.write(LED+1,5,1);e.write(LED+2,12,1);e.write(LED+4,ACQ+0x380,4);e.write(LED+8,0x40000104L,4);
        for(int i=0;i<4;i++)e.write(LED+0x10+4*i,w[12+i],4);
        for(int i=0;i<3;i++)e.write(LED+0x20+i,w[16+i],1);
        for(int i=0;i<3;i++)e.write(LED+0x24+4*i,w[19+i],4);
        e.write(INPUT,now,4);byte[] string=in.get("text").getAsString().getBytes(StandardCharsets.ISO_8859_1);e.emulator.writeMemory(e.address(INPUT+8),string);e.write(INPUT+8+string.length,0,1);
        int op=in.get("op").getAsInt();long target=0,result=0;boolean hasResult=false;
        long[] addresses={0x402029acL,0x402029e8L,0x40202a28L,0x40202a7cL,0x40202b70L,0x40202ba8L,0x40202becL,0x40202c14L,0x40202c90L,0x40202cd8L,0x40202d30L,0x40202ad0L,0x40202b00L,0x40202b34L};
        if(op<14){target=addresses[op];call(e,target,ADC,w[2],w[3],w[6]);hasResult=op==1||op==8||op==9||op==10;}
        else switch(op){case 14:call(e,0x4020319cL,ACQ);break;case 15:call(e,0x40203210L,ACQ,INPUT);break;case 16:call(e,0x40202db0L);hasResult=true;break;case 17:call(e,0x402056a4L,LED,w[2]);break;case 18:call(e,0x4020565cL,LED,w[2]);break;case 19:call(e,0x402058acL,LED,INPUT+8);break;default:throw new IllegalArgumentException("Unknown hardware case");}
        result=e.register(2);w[0]=e.read(ADC+0x10,1);w[1]=e.read(ADC+0x14,4);
        for(int i=0;i<4;i++)w[8+i]=e.read(ACQ+0x1c+4*i,4);
        for(int i=0;i<4;i++)w[12+i]=e.read(LED+0x10+4*i,4);
        for(int i=0;i<3;i++)w[16+i]=e.read(LED+0x20+i,1);
        for(int i=0;i<3;i++)w[19+i]=e.read(LED+0x24+4*i,4);
        Map<String,Object> out=new LinkedHashMap<>();out.put("id",in.get("id").getAsString());out.put("words",w);out.put("events",events);if(hasResult)out.put("result",result);return out;
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected gzip input/output");JsonArray inputs;
        for(long address:new long[]{0x402029acL,0x402029e8L,0x40202a28L,0x40202a7cL,0x40202b70L,0x40202ba8L,0x40202becL,0x40202c14L,0x40202a68L,0x40202abcL,0x40202cd8L,0x40202d30L,0x402056a4L,0x4020565cL,0x402142d4L,0x402143d0L,0x40214288L,0x40214844L,0x40105e90L,0x40105ec0L})nested.add(address);
        try(GZIPInputStream in=new GZIPInputStream(Files.newInputStream(Path.of(args[0])))){inputs=JsonParser.parseReader(new InputStreamReader(in,StandardCharsets.UTF_8)).getAsJsonArray();}
        Map<String,Object> report=new LinkedHashMap<>();List<Map<String,Object>> cases=new ArrayList<>();
        try(PicoFermExecution e=new PicoFermExecution(currentProgram,monitor,this::boundary)){
            for(JsonElement input:inputs){try{cases.add(hardware(e,input.getAsJsonObject()));}catch(Exception ex){throw new IllegalStateException(input.getAsJsonObject().get("id").getAsString(),ex);}}
            report.put("input_sha256",PicoFermExecution.IMAGE_SHA);report.put("cases_input_gzip_sha256",sha(Path.of(args[0])));report.put("cases",cases);
            report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original ADC register/I2C command sequencing, acquisition state advances, complete thermistor arithmetic including original newlib log, battery conversion and LED selection. SDK I2C, GPIO, clock, string equality, diagnostics, callbacks and ROM arithmetic are explicit boundaries. Invalid ADC states and invalid animation indices are not successful OEM returning cases.");report.put("hardware_tested",false);
        }
        try(GZIPOutputStream out=new GZIPOutputStream(Files.newOutputStream(Path.of(args[1])))){out.write((new Gson().toJson(report)+"\n").getBytes(StandardCharsets.UTF_8));}
        println("Executed "+cases.size()+" original PicoFerm hardware cases");
    }
}
