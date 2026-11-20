import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;
import java.util.*;
import java.util.zip.*;
import com.google.gson.*;

public class EmulatePicoStillApplication extends EmulatePicoStillController {
    private static final long BASE=0x3ff00000L,ARG=BASE+0x2800,COUNTER=0x3ffc3078L;
    @Override protected void boundary(PicoStillExecution e,long target)throws Exception {
        switch((int)target){
        case 0x400d91a8:e.countBoundary("update_transport_poll");events.add("service:update");return;
        case 0x400d764c:e.countBoundary("console_receiver_poll");events.add("service:console_receiver");return;
        case 0x400d63b0:e.countBoundary("wifi_driver_poll");events.add("service:wifi");return;
        case 0x400d76e0:e.countBoundary("backend_http_poll");events.add("service:backend");return;
        case 0x400d780c:e.countBoundary("http_server_dispatch");events.add("service:http");return;
        case 0x400d8a00:e.countBoundary("console_command_dispatch");events.add("service:console_commands");return;
        case 0x400d2a80:e.countBoundary("integer_diagnostic");events.add("integer:"+string(e,e.register(10))+":"+string(e,e.register(11))+":"+e.register(12));return;
        case 0x400db464:e.countBoundary("WifiServer_begin");events.add("listen:"+e.register(11));return;
        case 0x400d5918:e.countBoundary("console_command_queue");events.add("queue_console:"+string(e,e.register(11)));return;
        case 0x400d3534:e.countBoundary("credential_persistence");events.add("save_credentials:"+hex(e,e.register(11),100));return;
        case 0x400d8864:e.countBoundary("firmware_metadata_request");events.add("request_firmware_address");return;
        case 0x400d897c:e.countBoundary("firmware_update_start");events.add("start_firmware_update");return;
        case 0x400d8834:e.countBoundary("error_report_request");
            if(!string(e,e.register(12)).equals("logError"))throw new IllegalStateException("Unexpected request boundary");events.add("send_error:"+string(e,e.register(14)));return;
        case 0x400e8004:e.countBoundary("NVS_partition_erase_after_update");events.add("erase_nvs_after_update");return;
        case 0x400dbac0:e.countBoundary("ESP_restart");events.add("restart");return;
        case 0x400ec248:{e.countBoundary("libc_state_transition_sprintf");if(!string(e,e.register(11)).equals("%s -> %s"))throw new IllegalStateException("Unexpected format");
            String s=string(e,e.register(12))+" -> "+string(e,e.register(13));e.emulator.writeMemory(e.address(e.register(10)),(s+"\0").getBytes(StandardCharsets.US_ASCII));e.register(10,s.length());return;}
        case 0x400014c0:e.countBoundary("ROM_strlen");e.register(10,string(e,e.register(10)).length());return;
        default:super.boundary(e,target);
        }
    }
    private Map<String,Object> application(PicoStillExecution e,JsonObject in)throws Exception {
        e.resetRegisters();e.clear(BASE,0x4000);e.write(COUNTER,0,4);e.write(0x3ffc4770L,0x12345678L,4);events=new ArrayList<>();clock=in.get("now").getAsLong();high=in.get("high").getAsInt()!=0;
        pressure=in.getAsJsonArray("pressure");adc=in.getAsJsonArray("adc");call(e,0x400d8688L);
        String raw=in.get("data").getAsString();for(int i=0;i<raw.length()/2;i++)e.write(BASE+i,Integer.parseInt(raw.substring(2*i,2*i+2),16),1);
        String op=in.get("op").getAsString();long arg=in.get("arg").getAsLong(),arg2=in.get("arg2").getAsLong();e.write(ARG,arg,4);
        switch(op){
        case "queue_button":call(e,0x400d4c98L,BASE,arg,arg2);break;
        case "queue_wifi":call(e,0x400d4c30L,BASE,arg,arg2);break;
        case "state":call(e,0x400d8e64L,BASE,arg);break;
        case "button":call(e,0x400d903cL,BASE);break;
        case "wifi":call(e,0x400d9120L,BASE,BASE+0x17c);break;
        case "menu":call(e,0x400d5df0L,BASE+0x1ef4,arg);break;
        case "request_program":call(e,0x40149facL,BASE,ARG);break;
        case "request_stop":call(e,0x400d2b18L,BASE);break;
        case "poll":call(e,0x400d95ccL,BASE);break;
        default:throw new IllegalArgumentException("Unknown app operation "+op);
        }
        Map<String,Object> row=new LinkedHashMap<>();row.put("id",in.get("id").getAsString());row.put("data",hex(e,BASE,0x2000));row.put("events",events);row.put("assertion_count",e.read(COUNTER,4));row.put("clock",clock);return row;
    }
    @Override public void run()throws Exception {
        nested.addAll(Arrays.asList(0x400d4c98L,0x400d4c30L,0x400d8e64L,0x400d903cL,0x400d9120L,0x400d5df0L,
            0x40149facL,0x400d2b18L,0x400d5f38L,0x400d2a64L,0x400d5734L,0x400d554cL,
            0x400d32a4L,0x400d32c4L,0x400d5bc4L,0x400d5cc4L,0x400d5ce0L,0x400d45c4L,0x400d5dbcL,0x400d5be0L,
            0x400d43d0L,0x400d4428L,0x400d4408L,0x400d446cL,0x400d7288L));
        String[] args=getScriptArgs();if(args.length!=2)throw new IllegalArgumentException("Expected gzip input/output");JsonArray inputs;
        try(GZIPInputStream stream=new GZIPInputStream(Files.newInputStream(Path.of(args[0])))){inputs=JsonParser.parseReader(new InputStreamReader(stream,StandardCharsets.UTF_8)).getAsJsonArray();}
        Map<String,Object> report=new LinkedHashMap<>();List<Map<String,Object>> cases=new ArrayList<>();
        try(PicoStillExecution e=new PicoStillExecution(currentProgram,monitor,this::boundary)){
            for(JsonElement input:inputs)cases.add(application(e,input.getAsJsonObject()));report.put("input_sha256",PicoStillExecution.IMAGE_SHA);report.put("cases_input_gzip_sha256",sha(Path.of(args[0])));
            report.put("cases",cases);report.put("original_functions_executed",e.functions);report.put("modeled_boundaries",e.boundaries);
            report.put("scope","Original top-level application state/event instructions, with real recovered controller, LEDs, button, sensor and actuator bodies nested. Update/Wi-Fi/console/HTTP polling, persistence, report transport and restart/NVS erase are explicit boundaries. No complete network application or physical qualification.");
            report.put("full_cpu_emulation",false);report.put("hardware_tested",false);
        }
        try(GZIPOutputStream out=new GZIPOutputStream(Files.newOutputStream(Path.of(args[1])))){out.write((new Gson().toJson(report)+"\n").getBytes(StandardCharsets.UTF_8));}
        println("Executed "+cases.size()+" original PicoStill application/event cases");
    }
}
