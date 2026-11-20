// Original Core transition, Wi-Fi event, deferred start, mode and main tick.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.io.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;

public class EmulateCoreFlow extends GhidraScript {
    protected static final long CORE=0x3ff00000L,STRINGS=CORE+0x18000,STILL=CORE+0x19000,STILL_VTABLE=STILL+0x100,TEXT=STILL+0x200,TEXT_VTABLE=TEXT+0x100;
    protected static final long STILL_TICK=0x3ffe0200L,STILL_WIFI=0x3ffe0204L,TEXT_DISPATCH=0x3ffe0208L;
    protected static final int[] OFFSETS={0x734,0x738,0x73c,0x73d,0x73e,0x744,0x748,0x74c,0x3cf9,0x2efc,0x3cf8,0x15134,0x1515c,0x155b4,
        0x48c0,0x15994,-1,0xa0c,0xa18,0xc80,0xc90,0xca0,0xcb0,-1,0xa24,0x159a8,0x13534,0x831,0x832,0x975,-1,
        0x11b58,0x11b68,0x11338,0x15144,0x15150,0x3d00,0x4794,0x4800,0x47fc,-1,-1,-1,-1,-1,0x10,0x18,0xc};
    protected static final Set<Integer> BYTES=new HashSet<>(Arrays.asList(2,3,4,8,9,10,11,12,13,14,15,27,28,29,31,32,34,36,37));
    protected static final int[] PENDING={0x60c,0x1f0,0x570,0x38,0x154,0x6a0};
    protected List<List<Long>> events;
    protected Z119Execution active;
    protected long now,ethernetAddress,networkResult,lastAssertion,textToken;
    protected long[] initial;
    protected void event(long... values){List<Long> row=new ArrayList<>();for(long x:values)row.add(x);while(row.size()<4)row.add(0L);events.add(row);}
    protected void effect(long id,long... values){active.countBoundary("Core_effect_"+id);long[] row=new long[1+values.length];row[0]=id;System.arraycopy(values,0,row,1,values.length);event(row);}
    protected void tick(int stage){active.countBoundary("Core_tick_stage_"+stage);event(100,stage,now);}
    protected String string(Z119Execution e,long address)throws Exception{
        if(address==0)return "";StringBuilder s=new StringBuilder();for(int i=0;i<512;i++){int b=(int)e.read(address+i,1);if(b==0)return s.toString();s.append((char)b);}throw new IllegalStateException("Unbounded fixture string");
    }
    protected void string(Z119Execution e,long address,String text){byte[] b=(text+"\0").getBytes(java.nio.charset.StandardCharsets.US_ASCII);e.emulator.writeMemory(e.address(address),b);}
    protected void boundary(Z119Execution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        long object=e.register(10),arg=e.register(11);
        switch((int)target){
            case 0x40081388:e.countBoundary("clock_millis");e.register(10,now);return;
            case 0x4008c2b4:e.countBoundary("basename");return;
            case 0x400d21cc:e.countBoundary("diagnostic_log");return;
            case 0x400db014:e.countBoundary("returning_assertion");lastAssertion=arg;return;
            case 0x4011cbf4:case 0x4011ce5c:case 0x400e69f4:case 0x401bf3d4:case 0x401bf4e8:
            case 0x400ee3b4:case 0x400d1184:case 0x400edaec:case 0x400edbb0:case 0x400f4bf4:case 0x4011d1e4:
            case 0x401bf484:case 0x400f4424:case 0x400f33d4:case 0x400f077c:case 0x400f41ec:e.invoke(target);return;
            case 0x400d0e60:e.countBoundary("CString_equals");e.register(10,string(e,object).equals(string(e,arg))?1:0);return;
            case 0x401bf930:
                e.countBoundary("session_type_set_lookup");e.register(10,((arg>=6&&arg<=9)||(arg>=12&&arg<=14))?1:0);return;
            case 0x400f202c:
                e.countBoundary("SSID_copy");string(e,object,string(e,arg));e.write(object+0x24,string(e,arg).length(),4);return;
            case 0x40100954:
                e.countBoundary("password_copy");string(e,object,string(e,arg));e.write(object+0x44,string(e,arg).length(),4);return;
            case 0x400d598c:
                e.countBoundary("bounded_EEPROM_text_copy");int max=(int)arg;String value=string(e,e.register(12));
                string(e,object,value.substring(0,Math.min(value.length(),max-1)));return;
            case 0x400ee8dc:effect(0,arg);e.register(10,arg==0?1:networkResult);return;
            case 0x400d5bc0:effect(1,arg);return;
            case 0x400e6b84:effect(2,string(e,arg).isEmpty()?0:1,string(e,arg+0x30).isEmpty()?0:1);return;
            case 0x400fb070:effect(3);return;
            case 0x400d2160:effect(4,arg);return;
            case 0x4010ca44:effect(5);return;
            case 0x400f90d8:effect(6);return;
            case 0x400d5a88:effect(7,arg);return;
            case 0x400f4a44:effect(8);return;
            case 0x400d5b24:effect(9,arg);return;
            case 0x4011cadc:effect(10);return;
            case 0x400f4a64:effect(11);return;
            case 0x400f02e4:
                long type=e.read(arg+4,4);effect(12,type,type==0?e.read(arg+8,4):type==13?e.read(arg+0xde8,4):0,type==0?e.read(arg+12,4):0);return;
            case 0x400d6010:effect(13,arg);return;
            case 0x40124414:effect(14);e.write(object+4,ethernetAddress,4);return;
            case 0x400edb34:effect(15);return;
            case 0x400ed990:effect(16,arg);return;
            case 0x400f4b34:effect(17,arg);return;
            case 0x4011d010:effect(18,arg);return;
            case 0x400f20c8:effect(19,e.read(arg+4,4),e.read(arg+8,4),e.read(arg+12,4));return;
            case (int)STILL_WIFI:effect(20,e.read(arg+4,4),e.read(arg+8,4),e.read(arg+12,4));return;
            case 0x400d5c54:effect(21,arg);return;
            case 0x400f45b4:effect(22,e.read(arg+4,4),e.read(arg+8,4),e.read(arg+12,4));return;
            case 0x400eda88:effect(23);return;
            case 0x4011d32c:effect(24,e.read(arg+4,4));return;
            case (int)TEXT_DISPATCH:effect(25,textToken);return;
            case 0x400f4ccc:effect(26,e.read(arg+4,4),e.read(arg+8,4));return;
            case 0x400f3eb8:e.countBoundary("power_button_command_post");if(e.read(CORE+0x570,1)==0){e.write(CORE+0x570,1,1);e.write(CORE+0x284,arg,4);}return;
            case 0x400f0430:effect(12,arg);return;
            case 0x400dc1c4:tick(0);return;
            case 0x400dd908:tick(1);return;
            case 0x400dd738:e.register(10,0xffffffffL);return; // Explicit fixture: no valid ADC channel in orchestration tests.
            case 0x400d1720:tick(2);return;case 0x400d91ac:tick(3);return;
            case (int)STILL_TICK:tick(4);return;case 0x400e5e2c:tick(5);return;
            case 0x400d9510:tick(6);return;case 0x400f06a4:tick(7);return;
            case 0x40101bc4:tick(object==CORE+0x1130c?8:9);return;
            case 0x400fa21c:tick(10);return;case 0x400f27e0:tick(11);return;
            case 0x4011fc58:tick(12);return;case 0x40102120:tick(13);return;
            case 0x400f0804:tick(14);return;case 0x40109958:tick(15);return;
            case 0x4010b200:tick(16);return;case 0x4011c30c:tick(17);return;
            case 0x400efcfc:tick(18);return;case 0x400fb100:tick(19);return;
            case 0x4010ca58:tick(20);return;case 0x4011c928:tick(21);return;
            case 0x400f400c:tick(22);return;
            default:throw new IllegalStateException(String.format("Unmodeled Core call %08x",target));
        }
    }
    protected void load(Z119Execution e,JsonObject item)throws Exception{
        e.resetRegisters();e.clear(CORE,0x1a000);e.write(0x3ffc4654L,0x119c0eeL,4);e.write(0x3ffc2ea0L,0,4);
        JsonArray words=item.getAsJsonArray("words");if(words.size()!=64)throw new IllegalArgumentException("Unexpected Core record");
        initial=new long[64];for(int i=0;i<64;i++)initial[i]=words.get(i).getAsLong();events=new ArrayList<>();
        for(int i=0;i<OFFSETS.length;i++)if(OFFSETS[i]>=0)e.write(CORE+OFFSETS[i],initial[i],BYTES.contains(i)?1:4);
        now=initial[44];ethernetAddress=initial[42];networkResult=initial[43];lastAssertion=initial[63];textToken=initial[57];
        string(e,STRINGS,"fixture-error");e.write(CORE+0xed8,initial[16]!=0?STRINGS:0,4);
        String[] sensorErrors={"","WORT temp","HEX temp","DRAIN temp","AMBIENT temp","fixture-error"};
        string(e,STRINGS+0x100,sensorErrors[(int)initial[23]]);e.write(CORE+0xc70,initial[23]!=0?STRINGS+0x100:0,4);
        string(e,CORE+0x834,initial[30]!=0?"fixture-update":"");
        string(e,CORE+0x479c,initial[40]!=0?"saved-network":"");string(e,CORE+0x47bc,initial[61]!=0?"saved-password":"");
        string(e,CORE+0x2e78,initial[41]!=0?"pending-network":"");string(e,CORE+0x2ea8,initial[62]!=0?"pending-password":"");
        e.write(CORE+0x2e9c,initial[41]!=0?15:0,4);e.write(CORE+0x2eec,initial[62]!=0?16:0,4);
        for(int i=0;i<PENDING.length;i++)e.write(CORE+PENDING[i],(initial[48]>>>i)&1,1);
        e.write(CORE+0x600,initial[49],4);e.write(CORE+0x604,initial[50],4);e.write(CORE+0x608,initial[51],4);
        e.write(CORE+0x1e0,initial[52],4);e.write(CORE+0x1e4,initial[53],4);e.write(CORE+0x1e8,initial[54],4);
        e.write(CORE+0x284,initial[55],4);e.write(CORE+0x2c,initial[56],4);
        e.write(CORE+0x698,initial[58],4);e.write(CORE+0x69c,initial[59],4);
        e.write(CORE+0x5d58,STILL,4);e.write(STILL,STILL_VTABLE,4);e.write(STILL_VTABLE+16,STILL_TICK,4);e.write(STILL_VTABLE+28,STILL_WIFI,4);
        e.write(CORE+0x1c,TEXT,4);e.write(TEXT,TEXT_VTABLE,4);e.write(TEXT_VTABLE+8,TEXT_DISPATCH,4);
        e.write(CORE+0x15138,CORE+0x830,4);e.write(CORE+0x15130,CORE+0x13518,4);
    }
    protected Map<String,Object> snapshot(Z119Execution e,JsonObject item)throws Exception{
        List<Long> words=new ArrayList<>();for(long x:initial)words.add(x);
        for(int i=0;i<OFFSETS.length;i++)if(OFFSETS[i]>=0)words.set(i,e.read(CORE+OFFSETS[i],BYTES.contains(i)?1:4));
        String sensorError=string(e,e.read(CORE+0xc70,4));int code=Arrays.asList("","WORT temp","HEX temp","DRAIN temp","AMBIENT temp","fixture-error").indexOf(sensorError);
        if(code<0)throw new IllegalStateException("Unexpected original sensor error "+sensorError);words.set(23,(long)code);
        words.set(40,string(e,CORE+0x479c).isEmpty()?0L:1L);words.set(41,string(e,CORE+0x2e78).isEmpty()?0L:1L);
        words.set(61,string(e,CORE+0x47bc).isEmpty()?0L:1L);words.set(62,string(e,CORE+0x2ea8).isEmpty()?0L:1L);
        long mask=0;for(int i=0;i<PENDING.length;i++)mask|=(e.read(CORE+PENDING[i],1)!=0?1L:0L)<<i;
        words.set(48,mask);words.set(55,e.read(CORE+0x284,4));words.set(63,lastAssertion);
        Map<String,Object> result=new LinkedHashMap<>();result.put("id",item.get("id").getAsString());result.put("words",words);result.put("events",events);
        result.put("credentials",Arrays.asList(string(e,CORE+0x479c),string(e,CORE+0x47bc),string(e,CORE+0x2e78),string(e,CORE+0x2ea8)));return result;
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected input JSON and output JSONL");
        JsonArray inputs=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonArray();int count=0;Gson json=new Gson();
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){
            active=e;
            for(JsonElement element:inputs){JsonObject item=element.getAsJsonObject();load(e,item);String kind=item.get("kind").getAsString();long target;
                switch(kind){case "transition":target=0x4011cbf4L;break;case "wifi":target=0x4011ce5cL;break;
                    case "tick":target=0x40120120L;break;case "start":target=0x400f4bf4L;break;case "mode":target=0x4011d1e4L;break;
                    default:throw new IllegalArgumentException("Unknown Core scenario");}
                e.register(10,CORE);e.register(11,kind.equals("wifi")?CORE+0x5fc:kind.equals("mode")?CORE+0x280:kind.equals("tick")?now:initial[60]);
                e.invoke(target);out.write(json.toJson(snapshot(e,item))+"\n");count++;
            }
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);report.put("cases",count);
            report.put("scope","Original Core main tick/state loop, transition, Wi-Fi routing, deferred Runtime start and session-mode selection with nested predicates; explicit subsystem/clock/string/set boundaries and adapted ENTRY/RETW register windows; orchestration ADC fixture contains no valid channels");
            report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);report.put("full_cpu_or_hardware_emulation",false);report.put("hardware_tested",false);
            Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");
        }
        println("Executed "+count+" original Core flow scenarios");
    }
}
