// Execute original state-dependent Core commands and session startup.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.io.*;
import java.util.*;
import com.google.gson.*;

public class EmulateCoreCommands extends EmulateCoreFlow {
    private static final long COMMAND=CORE+0x17800,STILL_COMMAND=0x3ffe020cL;
    private long[] extra;
    private List<List<Long>> posted;
    private JsonObject current;
    private int temperatureRead;
    private void parameters(Z119Execution e,long p,long id,boolean seed){
        e.clear(p,0x51);e.write(p,id,4);e.write(p+4,seed?7:0xffffffffL,4);e.write(p+8,seed?11:0xffffffffL,4);
        double[] values={600,9,4,70};int[] units={6,0,7,4};
        for(int i=0;i<4;i++){e.writeDouble(p+0x10+i*16,seed?values[i]:0);e.write(p+0x18+i*16,seed?units[i]:8,4);}e.write(p+0x50,seed?1:0,1);
    }
    private List<Long> params(Z119Execution e,long p)throws Exception{
        List<Long> out=new ArrayList<>();for(int i=0;i<3;i++)out.add(e.read(p+i*4,4));
        for(int i=0;i<4;i++)for(int j=0;j<3;j++)out.add(e.read(p+0x10+i*16+j*4,4));out.add(e.read(p+0x50,1));return out;
    }
    private List<Long> stats(Z119Execution e,long p)throws Exception{
        List<Long> out=new ArrayList<>();for(int offset:new int[]{0,4,8,16,20,24,28,32})out.add(e.read(p+offset,4));return out;
    }
    private void runtimePost(Z119Execution e,long p)throws Exception{
        List<Long> out=new ArrayList<>();for(int offset:new int[]{4,8,16,20,24,28})out.add(e.read(p+offset,4));
        out.add(e.read(p+32,1));out.add(e.read(p+36,4));for(int i=0;i<3;i++)for(int j=0;j<3;j++)out.add(e.read(p+40+i*16+j*4,4));out.add(e.read(p+88,4));
        posted.add(out);effect(34,out.get(0));
    }
    @Override protected void boundary(Z119Execution e,long target)throws Exception{
        long object=e.register(10),arg=e.register(11);
        switch((int)target){
            case 0x4011d010:case 0x400f0480:case 0x400ee254:case 0x400d5fd4:case 0x400edc80:case 0x400edcb0:
            case 0x400d0f80:case 0x400d0fb8:case 0x400ee998:case 0x400d7df8:case 0x401bf2cc:
            case 0x400ed15c:case 0x400defa4:case 0x400dee30:case 0x401bf40c:case 0x400e0f5c:case 0x400ec52c:case 0x400ec360:
            case 0x400d12b0:case 0x400dff0c:case 0x400dd708:case 0x400dd68c:case 0x400de27c:case 0x400d0094:
            case 0x400d106c:case 0x400d1040:case 0x400d0ffc:case 0x400f4a2c:case 0x400f4ab4:
            case 0x400f3f58:case 0x400e60b8:case 0x400e69cc:case 0x400dd9f4:e.invoke(target);return;
            case 0x4000c2c8:e.countBoundary("ROM_memcpy");e.emulator.writeMemory(e.address(object),e.emulator.readMemory(e.address(arg),(int)e.register(12)));return;
            case 0x4000c44c:e.countBoundary("ROM_memset");byte[] fill=new byte[(int)e.register(12)];Arrays.fill(fill,(byte)arg);e.emulator.writeMemory(e.address(object),fill);return;
            case 0x400de56c:e.countBoundary("duration_diagnostic_format");return;
            case 0x4012d42c:
                e.countBoundary("__fixunsdfsi_finite_fixture_domain");double real=e.number(10);
                if(!Double.isFinite(real)||real<0||real>=4294967296.0)throw new IllegalArgumentException("Unsigned conversion outside fixture domain");e.register(10,(long)real);return;
            case 0x400d5f94:effect(27);return;
            case 0x400fb1c0:effect(28,e.read(arg+4,4));return;
            case 0x400edaac:effect(29);return;
            case 0x400f51b8:effect(30,e.read(arg+4,4));return;
            case 0x400edc18:effect(31);return;
            case 0x400d5f38:effect(32);return;
            case 0x401021b8:effect(33,e.read(arg+4,4));return;
            case 0x400f4900:runtimePost(e,arg);return;
            case 0x400dc0a4:effect(35,arg);return;
            case (int)STILL_COMMAND:effect(36,arg,e.register(12));return;
            case 0x400daf64:effect(37,arg,(long)e.number(12));return;
            case 0x400daf04:effect(38);return;
            case 0x400eda64:effect(39);return;
            case 0x400dd908:effect(40);return;
            case 0x400dd770:
                e.countBoundary("previously_validated_calibrated_temperature");int[] channels={3,1,2,0,0},types={2,1,1,0,3};
                if(temperatureRead>=5||arg!=channels[temperatureRead]||e.register(12)!=types[temperatureRead])throw new IllegalStateException("Unexpected detector conversion");
                e.number(10,current.getAsJsonArray("detector_temperatures").get(temperatureRead++).getAsDouble());return;
            case 0x400ec7b4:
                e.countBoundary("previously_validated_coffee_builder");
                if(arg!=COMMAND+0x298||e.read(arg,4)!=extra[19]||e.register(12)!=CORE+0xc58||e.register(13)!=extra[20])throw new IllegalStateException("Wrong coffee builder inputs");
                e.register(10,extra[27]);return;
            case 0x400ef230:
                e.countBoundary("CoreCommand_constructor_fixture");e.clear(object,0x2f0);e.write(object,0x3f41f8c4L,4);e.write(object+4,arg,4);return;
            case 0x400f3d3c:
                e.countBoundary("CoreCommand_mailbox_previously_validated_first_wins");
                if(e.read(CORE+0x570,1)==0){e.emulator.writeMemory(e.address(CORE+0x280),e.emulator.readMemory(e.address(arg),0x2f0));e.write(CORE+0x570,1,1);}return;
            case 0x400f02e4:
                long type=e.read(arg+4,4);if(type==9){effect(12,type,e.read(arg+0xde0,4),e.read(arg+0xde4,4));return;}break;
        }
        super.boundary(e,target);
    }
    @Override protected void load(Z119Execution e,JsonObject item)throws Exception{
        super.load(e,item);current=item;extra=new long[40];JsonArray input=item.getAsJsonArray("extra");for(int i=0;i<28;i++)extra[i]=input.get(i).getAsLong();
        posted=new ArrayList<>();temperatureRead=0;
        e.write(STILL_VTABLE+20,STILL_COMMAND,4);e.write(CORE+0x4804,extra[7],4);e.write(CORE+0x17b0,extra[8],4);e.write(CORE+0x1515d,extra[9],1);
        e.write(CORE+0x4795,extra[20],1);e.write(CORE+0x48c4,extra[14],4);for(int i=0;i<4;i++)e.write(CORE+0x48d4+i*4,extra[10+i],4);
        e.write(CORE+0x288,extra[21],4);e.write(CORE+0x49c0,extra[25],4);e.write(CORE+0x49d8,extra[26],4);
        e.writeDouble(CORE+0xea8,item.getAsJsonArray("values").get(0).getAsDouble());e.writeDouble(CORE+0xed0,item.getAsJsonArray("values").get(1).getAsDouble());
        e.write(CORE+0x750,0xffffffffL,4);e.write(CORE+0x828,1,1);parameters(e,CORE+0x7d0,0,false);
        e.write(CORE+0x3cfc,CORE+0x17b8,4);e.write(CORE+0x17b8,extra[17]!=0?extra[15]:16,4);e.write(CORE+0x17bc,extra[17]!=0?extra[16]:0xffffffffL,4);
        string(e,STRINGS+0x200,extra[17]!=0?"Buffered fixture":"<EMPTY>");e.write(CORE+0x17c0,STRINGS+0x200,4);parameters(e,CORE+0x17d8,extra[18],true);
        e.writeDouble(CORE+0x1830,2);e.write(CORE+0x1838,321,4);e.writeDouble(CORE+0x1840,1234);e.writeDouble(CORE+0x1848,88);e.write(CORE+0x1850,456,4);
        e.write(COMMAND+4,initial[55],4);e.write(COMMAND+8,extra[0],4);e.write(COMMAND+12,extra[1],4);e.write(COMMAND+16,extra[2],4);e.write(COMMAND+20,extra[3],4);
        string(e,COMMAND+0x18,extra[5]!=0?"command-network":"");string(e,COMMAND+0x48,extra[6]!=0?"command-password":"");
        e.write(COMMAND+0x3c,extra[5]!=0?15:0,4);e.write(COMMAND+0x8c,extra[6]!=0?16:0,4);e.write(COMMAND+0xa0,extra[4],4);
        e.write(COMMAND+0x1a0,extra[23],1);e.write(COMMAND+0x1a4,extra[22],4);double[] v={70,150,210};int[] u={4,6,6};
        for(int i=0;i<3;i++){e.writeDouble(COMMAND+0x1a8+i*16,v[i]);e.write(COMMAND+0x1b0+i*16,u[i],4);}e.write(COMMAND+0x1d8,extra[24],4);
        e.write(COMMAND+0x1e0,8,4);parameters(e,COMMAND+0x298,extra[19],true);
    }
    @Override protected Map<String,Object> snapshot(Z119Execution e,JsonObject item)throws Exception{
        Map<String,Object> out=super.snapshot(e,item);List<Long> x=new ArrayList<>();for(long value:extra)x.add(value);
        x.set(7,e.read(CORE+0x4804,4));x.set(9,e.read(CORE+0x1515d,1));x.set(14,e.read(CORE+0x48c4,4));x.set(21,e.read(CORE+0x288,4));
        for(int i=0;i<12;i++)x.set(28+i,e.read(CORE+0x4980+i*4,4));out.put("extra",x);
        List<Long> session=new ArrayList<>(Arrays.asList(e.read(CORE+0x740,4),e.read(CORE+0x750,4),e.read(CORE+0x828,1)));
        session.addAll(params(e,CORE+0x7d0));session.addAll(stats(e,CORE+0x7a8));out.put("session",session);out.put("name",string(e,CORE+0x754));out.put("runtime_commands",posted);return out;
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input and output");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();int count=0;Gson json=new Gson();
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){active=e;
            for(JsonElement value:inputs){JsonObject item=value.getAsJsonObject();load(e,item);String kind=item.get("kind").getAsString();long target;
                switch(kind){case "command":target=0x4011d32cL;break;case "program":target=0x4011d010L;break;case "coffee":target=0x400f0480L;break;case "boiler":target=0x400ee254L;break;default:throw new IllegalArgumentException(kind);}
                e.register(10,CORE);e.register(11,kind.equals("program")?extra[0]:kind.equals("boiler")?COMMAND+4:COMMAND);
                try{e.invoke(target);}catch(Exception error){throw new IllegalStateException("Scenario "+item.get("id").getAsString(),error);}
                out.write(json.toJson(snapshot(e,item))+"\n");count++;
            }
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);report.put("cases",count);
            report.put("scope","Original Core dispatcher, session startup and static statistics, boiler selection/tunings, reset ordering and Runtime command construction; explicit previously validated coffee/thermistor/command-constructor/mailbox and external subsystem boundaries");
            report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);report.put("full_cpu_or_hardware_emulation",false);report.put("hardware_tested",false);
            Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }println("Executed "+count+" original Core command scenarios");
    }
}
