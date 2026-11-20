// Execute Z119 factory and Ethernet transport bodies through their actual return.
// @category PicoBrew.Validation
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.io.*;
import java.util.*;
import com.google.gson.*;
import ghidra.app.script.GhidraScript;
public class EmulateNetworkClient extends GhidraScript {
    static final long FACTORY=0x3ff30000L,OBJECT=0x3ff40000L,BUFFER=0x3ff50000L,CLIENTS=0x3ff60000L,OUT=0x3ff70000L;
    long[] v;long now;int reads,writes,lastSocket;boolean phase;List<String> events;
    long allocation;
    String string(Z119Execution e,long address)throws Exception{StringBuilder s=new StringBuilder();for(int i=0;i<1024;i++){int c=(int)e.read(address+i,1);if(c==0)return s.toString();s.append((char)c);}throw new IllegalStateException("Unterminated string");}
    void event(int op,long socket,long argument,long second,long size){events.add("P|"+op+"|"+socket+"|"+(argument&0xffffffffL)+"|"+second+"|"+(size&0xffffffffL));}
    int status(int socket){
        if(v[0]==3){if(!phase)return socket==v[4]?(int)v[5]:0x17;int n=reads++;return v[10]==0?0x17:v[10]==1?(n==0?0x15:0x17):v[10]==2?0:(n==0?0x15:0);}
        if(v[0]==9)return reads++>=v[10]?0:0x17;return (int)v[10];
    }
    void fill(Z119Execution e,long destination,long size,long returned){if(destination!=0&&(int)size>0)for(int i=0;i<64&&i<size&&i<(int)returned;i++)e.write(destination+i,0x40+i%29,1);}
    void boundary(Z119Execution e,long target)throws Exception{
        if(e.arithmetic(target))return;
        if(target>=0x401d0000L&&target<0x401d0300L){int type=(int)(target-0x401d0000L)/0x100+1;int op=(int)target&255;
            if(op==0){events.add("S|"+type);e.countBoundary("client_stop");}
            else if(op==4){events.add("X|"+type);e.countBoundary("client_delete");}
            else if(op==8){events.add("N|"+type);e.countBoundary("client_connected");e.register(10,(v[5]>>(type-1))&1);}
            else throw new IllegalStateException("Unknown virtual call");return;
        }
        long socket=e.read(OBJECT+0x908,1);
        switch((int)target){
        case 0x400ed41c:case 0x400db014:e.invoke(target);return;
        case 0x40124b80:
            if(v[0]<3){events.add("N|1");e.countBoundary("Ethernet_client_connected");e.register(10,v[5]&1);}
            else e.invoke(target);return;
        case 0x4019d194:{e.countBoundary("operator_new");long size=e.register(10);int kind=size==0x910?1:size==0x28?2:size==0x4c?3:0;
            if(kind==0)throw new IllegalStateException("Unexpected allocation "+size);events.add("C|"+kind);allocation=CLIENTS+(kind-1)*0x1000;e.register(10,allocation);return;}
        case 0x40124974:e.invoke(target);installVtable(e,allocation,0);return;
        case 0x40121ec8:case 0x40125c7c:e.countBoundary("WiFi_client_constructor");installVtable(e,allocation,target==0x40121ec8?1:2);return;
        case 0x401bfed4:e.countBoundary("WiFi_secure_handshake_timeout");events.add("T|3|"+e.register(11));return;
        case 0x40081388:e.countBoundary("millis");e.register(10,now);return;
        case 0x401289a0:e.countBoundary("delay");events.add("D|"+e.register(10)+"|"+e.read(0x3ffc2e84L,4));now=(now+e.register(10))&0xffffffffL;return;
        case 0x4008c2b4:{e.countBoundary("diagnostic_basename");String s=string(e,e.register(10));e.register(10,e.register(10)+Math.max(s.lastIndexOf('\\'),s.lastIndexOf('/'))+1);return;}
        case 0x400d21cc:
            e.countBoundary("formatted_assertion");events.add("A|"+string(e,e.register(14))+"|"+e.register(15)+"|"+string(e,e.read(e.register(1),4))+"|"+e.register(12)+"|"+e.register(13)+"|"+e.read(0x3ffc2e84L,4));return;
        case 0x40124a7c:case 0x401249a8:case 0x401249d0:case 0x40124a28:case 0x40124a40:
        case 0x401249e0:case 0x40124a58:case 0x401246a4:case 0x40124bd0:e.invoke(target);return;
        case 0x40125384:{e.countBoundary("W5500_status_register");int index=((int)e.register(11)-8)/32;event(0,index,0,0,0);e.register(10,status(index));return;}
        case 0x4012501c:e.countBoundary("W5500_socket_open");event(1,e.register(10),e.register(12),0,0);e.register(10,1);return;
        case 0x40125070:e.countBoundary("W5500_socket_connect");event(2,e.register(10),e.read(e.register(11),4),e.register(12),0);phase=true;e.register(10,v[6]);return;
        case 0x401250d0:e.countBoundary("W5500_disconnect");lastSocket=(int)e.register(10);event(3,e.register(10),0,0,0);return;
        case 0x40124ffc:e.countBoundary("W5500_close");event(4,e.register(10),0,0,0);return;
        case 0x401250e0:e.countBoundary("W5500_send");event(6,e.register(10),0,0,e.register(12));e.register(10,v[11]);return;
        case 0x40125164:e.countBoundary("W5500_receive");event(7,e.register(10),0,0,e.register(12));fill(e,e.register(11),e.register(12),v[12]);e.register(10,v[12]);return;
        case 0x401254a0:e.countBoundary("W5500_receive_available");event(8,e.register(11),0,0,0);e.register(10,v[16]);return;
        case 0x401251c8:e.countBoundary("W5500_peek");event(9,e.register(10),0,0,0);e.write(e.register(11),v[17],1);return;
        case 0x401bfd8c:e.countBoundary("W5500_flush");event(10,e.register(10),0,0,0);return;
        case 0x40124760:e.countBoundary("mbedTLS_handshake_lifecycle");event(11,socket,0,0,0);e.write(OBJECT+0x10,OBJECT,4);e.register(10,v[15]);return;
        case 0x401246f4:e.countBoundary("TLS_stop_entry");event(5,lastSocket,0,0,0);event(12,8,0,0,0);e.invoke(target);return;
        case 0x401245a0:e.countBoundary("mbedTLS_context_release");return;
        case 0x40154fdc:e.countBoundary("mbedTLS_write");event(13,socket,0,0,e.register(12));long result=writes++<v[14]?(writes%2!=0?-0x6900:-0x6880):v[11];e.register(10,result);return;
        case 0x40155e04:e.countBoundary("mbedTLS_read");event(14,socket,0,0,e.register(12));fill(e,e.register(11),e.register(12),v[13]);e.register(10,v[13]);return;
        case 0x401c0c5c:e.countBoundary("mbedTLS_available");event(15,socket,0,0,0);e.register(10,v[16]);return;
        case 0x4012462c:e.countBoundary("TLS_error_and_release");event(16,socket,e.register(11),0,0);e.register(10,e.register(11));return;
        case 0x4012a76c:e.countBoundary("Serial_print_BAD");if(!string(e,e.register(11)).equals("BAD"))throw new IllegalStateException("Unexpected Serial string");event(17,socket,0,0,0);return;
        default:throw new IllegalStateException(String.format("Unreviewed network call %08x",target));
        }
    }
    void installVtable(Z119Execution e,long object,int kind){long table=0x3ff80000L+kind*0x100;long base=0x401d0000L+kind*0x100;
        e.write(object,table,4);e.write(table+4,base+4,4);e.write(table+0x2c,base,4);e.write(table+0x30,base+8,4);
    }
    void acquire(Z119Execution e,long network,long secure)throws Exception{e.register(10,FACTORY);e.register(11,OUT);e.register(12,OUT+4);e.register(13,network);e.register(14,secure);e.invoke(0x400ed4d4L);}
    Map<String,Object> one(Z119Execution e,JsonArray item)throws Exception{
        v=new long[20];for(int i=0;i<v.length;i++)v[i]=item.get(i).getAsLong();now=v[7];reads=0;writes=0;phase=false;lastSocket=0;events=new ArrayList<>();
        e.resetRegisters();e.clear(FACTORY,0x52000);e.write(0x3ffc2e84L,v[8],4);e.write(0x3ffc4654L,0x1195a5a5L,4);
        e.write(FACTORY,v[3],4);for(int i=0;i<3;i++){long object=CLIENTS+i*0x1000;if((v[4]&(1L<<i))!=0)e.write(FACTORY+4+i*4,object,4);installVtable(e,object,i);}
        e.write(CLIENTS+0x8f8,444,4);e.write(CLIENTS+0x900,1,1);e.write(CLIENTS+0x904,0x1234,4);
        e.write(OBJECT,0x3f4494ecL,4);e.write(OBJECT+0x908,v[1],1);e.write(OBJECT+0x900,v[2],1);e.write(OBJECT+0x10,v[3]!=0?OBJECT:0,4);e.write(OBJECT+4,v[19],4);
        e.write(0x3ffbde4eL,v[9],2);e.write(OUT+0x14,0x04030201,4);for(int i=0;i<64;i++)e.write(BUFFER+i,0xa5,1);
        e.register(10,OBJECT);e.register(11,BUFFER);e.register(12,v[18]);long returned=0,accepted=0;
        switch((int)v[0]){
        case 0:acquire(e,v[1],v[2]);accepted=e.register(10);break;
        case 1:e.register(10,FACTORY);e.invoke(0x400ed41cL);break;
        case 2:acquire(e,1,1);acquire(e,2,0);acquire(e,2,1);accepted=e.register(10);break;
        case 3:e.register(11,OUT+0x10);e.register(12,443);e.invoke(0x40124aa4L);returned=e.register(10);break;
        case 4:e.invoke(0x40124b80L);returned=e.register(10);break;
        case 5:e.invoke(0x401249e0L);returned=e.register(10);break;
        case 6:e.invoke(0x40124a58L);returned=e.register(10);break;
        case 7:e.invoke(0x401246a4L);returned=e.register(10);break;
        case 8:e.invoke(0x4012449cL);returned=e.register(10);break;
        case 9:e.invoke(0x40124bd0L);break;
        case 10:e.invoke(0x401244dcL);returned=e.register(10);break;
        case 11:e.invoke(0x401244c4L);break;
        }
        boolean factory=v[0]<3;long mask=0;for(int i=0;i<3;i++)if(e.read(FACTORY+4+i*4,4)!=0)mask|=1L<<i;
        long handle=factory?e.read(OUT,4):0;long hash=2166136261L;for(int i=0;i<64;i++)hash=((hash^e.read(BUFFER+i,1))*16777619L)&0xffffffffL;
        boolean ethernet=factory&&(mask&1)!=0;
        long[] output={returned,factory?0:e.read(OBJECT+0x908,1),factory?0:e.read(OBJECT+0x10,4)!=0?1:0,factory?0:e.read(OBJECT+4,4),factory?0:e.read(0x3ffbde4eL,2),
            factory?e.read(FACTORY,4):0,factory?mask:0,handle!=0?(handle-CLIENTS)/0x1000+1:0,factory?e.read(OUT+4,4):0,accepted,now,e.read(0x3ffc2e84L,4),hash,0,
            ethernet?e.read(CLIENTS+0x8f8,4):0,ethernet?e.read(CLIENTS+0x900,1):0,ethernet?e.read(CLIENTS+0x904,4):0};
        Map<String,Object> result=new LinkedHashMap<>();result.put("output",output);result.put("events",events);return result;
    }
    @Override public void run()throws Exception{
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Input/output required");byte[] inputs=Files.readAllBytes(Path.of(args[0]));JsonArray cases=JsonParser.parseString(new String(inputs,StandardCharsets.UTF_8)).getAsJsonArray();Gson gson=new Gson();
        try(Z119Execution e=new Z119Execution(currentProgram,monitor,this::boundary);BufferedWriter out=Files.newBufferedWriter(Path.of(args[1]))){int index=0;for(JsonElement item:cases){try{out.write(gson.toJson(one(e,item.getAsJsonArray()))+"\n");}catch(Exception failure){throw new IllegalStateException("Network case "+index,failure);}index++;}
            Map<String,Object> report=new LinkedHashMap<>();report.put("input_sha256",Z119Execution.IMAGE_SHA);report.put("input_file_sha256",HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(inputs)));report.put("cases",index);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original client factory/clear and custom Ethernet connect/status/read/write/available/peek/flush/stop through actual returns, with real returning assertion helper. SDK client allocation/virtual operations, W5500 socket/register IO, mbedTLS primitives and complete handshake lifecycle are explicit boundaries. Checks ordered calls, socket/client ownership, error codes, timeout/CA configuration, TLS retry policy, read data and counter/delay. Does not validate live network traffic, TLS handshake internals, DHCP/DNS packets, ISR concurrency or hardware.");
            Files.writeString(Path.of(args[1]+".json"),new GsonBuilder().setPrettyPrinting().create().toJson(report)+"\n");}
        println("Executed "+cases.size()+" original network client cases");
    }
}
