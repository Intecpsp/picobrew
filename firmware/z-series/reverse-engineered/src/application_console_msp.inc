// Direct original MSP/servo callback operations. The actual application binds
// msp.diagnostics for returning assertions; the null-sink legacy API retains
// its explicitly bounded older evidence.
switch(id) {
case 0x400dc1a4:case 0x400dc970:case 0x400dd268:case 0x400dd29c:
    return app.ports.msp_image&&msp_console_self_test(id,app.msp,*app.ports.msp_image,unit_test_msp_image,services,owner.hooks);
case 0x400dc0bc: return msp_send(100);
case 0x400dc0cc: return msp_send(101);
case 0x400dc3ac: return msp_servo(60,static_cast<std::uint8_t>(value));
case 0x400dc3c8: return msp_servo(61,static_cast<std::uint8_t>(value));
case 0x400dc3e4: return msp_servo(62,static_cast<std::uint8_t>(value));
case 0x400dc400: return msp_servo(63,static_cast<std::uint8_t>(value));
case 0x400dc41c: return msp_servo(64,static_cast<std::uint8_t>(value));
case 0x400dc438: return msp_servo(65,static_cast<std::uint8_t>(value));
case 0x400dc454: return msp_servo(66,static_cast<std::uint8_t>(value));
case 0x400dc470: return msp_servo(67,static_cast<std::uint8_t>(value));
case 0x400dc48c: return msp_servo(68,255);
case 0x400dc4a0: return msp_servo(72,0);
case 0x400dc4b0: return msp_servo(69,static_cast<std::uint8_t>(value));
case 0x400dc4cc: return msp_servo(70,static_cast<std::uint8_t>(value));
case 0x400dc4e8: return msp_servo(71,static_cast<std::uint8_t>(value));
case 0x400dc878: return information_msp();
case 0x400dc908: return z_msp_start_app(&app.msp)==Z_MSP_OK;
case 0x400dc0f4: return z_msp_switch_to_boot(&app.msp)==Z_MSP_OK;
case 0x400dc2c0: return msp_send(252);
case 0x400dc148: return z_msp_get_version(&app.msp)==Z_MSP_OK;
case 0x400dc1b4: return msp_send(14);
case 0x400dc2d0: return msp_send(253);
case 0x400dc1e8: return msp_send(17);
case 0x400dc1f8: return msp_send(20);
case 0x400dc208: return msp_send(16);
case 0x400dc218: return msp_send(19);
case 0x400dc228: return msp_send(15);
case 0x400dc238: return msp_send(18);
case 0x400dc248: return msp_send(52);
case 0x400dc258: return msp_send(53);
case 0x400dc9bc: return z_msp_move_valve(&app.msp,1)==Z_MSP_OK;
case 0x400dc9cc: return z_msp_move_valve(&app.msp,3)==Z_MSP_OK;
case 0x400dc9dc: return z_msp_move_valve(&app.msp,4)==Z_MSP_OK;
case 0x400dc9ec: return z_msp_move_valve(&app.msp,2)==Z_MSP_OK;
case 0x400dc9fc: return z_msp_move_valve(&app.msp,5)==Z_MSP_OK;
case 0x400dca0c: return z_msp_move_valve(&app.msp,6)==Z_MSP_OK;
case 0x400dca1c: return z_msp_move_valve(&app.msp,7)==Z_MSP_OK;
case 0x400dca2c: return z_msp_move_valve(&app.msp,8)==Z_MSP_OK;
case 0x400dca3c: return z_msp_move_valve(&app.msp,9)==Z_MSP_OK;
case 0x400dca4c: return z_msp_move_valve(&app.msp,5)==Z_MSP_OK;
case 0x400dca5c: return z_msp_move_valve(&app.msp,6)==Z_MSP_OK;
case 0x400dca6c: return z_msp_move_valve(&app.msp,7)==Z_MSP_OK;
case 0x400dca7c: return z_msp_move_valve(&app.msp,8)==Z_MSP_OK;
case 0x400dca8c: return z_msp_move_valve(&app.msp,9)==Z_MSP_OK;
case 0x400dc268: return z_msp_step_valve(&app.msp,true)==Z_MSP_OK;
case 0x400dc2e0: return z_msp_step_valve(&app.msp,false)==Z_MSP_OK;
case 0x400dd0e0: return app.ports.msp_image&&z_msp_setup(&app.msp,app.ports.msp_image)==Z_MSP_OK;
case 0x400dcb6c: return z_msp_erase_app(&app.msp)>=Z_MSP_OK;
case 0x400dccd0: return app.ports.msp_image&&z_msp_update(&app.msp,app.ports.msp_image)==Z_MSP_OK;
case 0x400dd13c: return app.ports.msp_image&&msp_console_self_test(0x400dd0f0,app.msp,*app.ports.msp_image,unit_test_msp_image,services,owner.hooks);
case 0x400dc280: return msp_send(203);
case 0x400dc290: return msp_send(204);
case 0x400dc2a0: return msp_send(201);
case 0x400dc2b0: return msp_send(202);
case 0x400dc5b0: return msp_send(200,static_cast<std::uint32_t>(value),4);
case 0x400dc8a4: {
    if(app.msp_state.id!=Z_MSP_BOOT_RUNNING&&!assertion("Msp.h",0x7a,"_mspState.id == MspStateId::BootRunning"))return false;
    if(z_msp_bsl_command(&app.msp,Z_MSP_BSL_VERSION)<Z_MSP_OK||!owner.hooks.write)return false;
    char result[32];const auto length=::snprintf(result,sizeof(result),"RESPONSE 0x%X\r\n",app.msp.last_bsl_response);
    return length>0&&owner.hooks.write(owner.hooks.context,result,static_cast<std::size_t>(length));
}
default:break;
}
