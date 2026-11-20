#include "storage_ports.hpp"
#include <esp_spiffs.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cassert>
#include <cstdarg>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
using namespace picobrew::standalone::esp32;
using namespace picobrew::standalone;
namespace {
bool mount_ok=false,stat_error=false,close_error=false;unsigned closes=0;std::string directory;
std::string translated(const char* name){assert(!std::strncmp(name,"/spiffs/pb/",11));return directory+"/"+(name+11);}
}
esp_err_t esp_vfs_spiffs_register(const esp_vfs_spiffs_conf_t* conf){assert(!conf->format_if_mount_failed);assert(!std::strcmp(conf->base_path,"/spiffs"));return mount_ok?ESP_OK:ESP_FAIL;}
bool esp_spiffs_mounted(const char*){return mount_ok;}
esp_err_t esp_spiffs_info(const char*,std::size_t* total,std::size_t* used){*total=1048576;*used=1234;return ESP_OK;}
extern "C" {
int __real_open(const char*,int,...);
int __real_close(int);
int __real_stat(const char*,struct stat*);
int __real_rename(const char*,const char*);
int __real_unlink(const char*);
int __wrap_open(const char* name,int flags,...){unsigned mode=0;if(flags&O_CREAT){va_list args;va_start(args,flags);mode=va_arg(args,unsigned);va_end(args);}return __real_open(translated(name).c_str(),flags,mode);}
int __wrap_close(int fd){++closes;int result=__real_close(fd);if(close_error){close_error=false;errno=EIO;return -1;}return result;}
int __wrap_stat(const char* path,struct stat* info){if(stat_error){errno=EIO;return -1;}return __real_stat(translated(path).c_str(),info);}
int __wrap_rename(const char* from,const char* to){return __real_rename(translated(from).c_str(),translated(to).c_str());}
int __wrap_unlink(const char* path){return __real_unlink(translated(path).c_str());}
}
int main(){
    char temp[]="/tmp/z-spiffs-port-XXXXXX";assert(mkdtemp(temp));directory=temp;
    SpiffsFiles files;FileIo io=files.io();std::size_t size=0,transferred=0;FileHandle handle=-1;
    assert(!files.mount());assert(io.stat(io.context,"/pb/absent",size)==StorageResult::Error);
    mount_ok=true;assert(files.mount());assert(io.stat(io.context,"/pb/absent",size)==StorageResult::Missing);
    stat_error=true;assert(io.stat(io.context,"/pb/absent",size)==StorageResult::Error);stat_error=false;
    assert(io.open_write(io.context,"/pb/data",handle)==StorageResult::Ok&&handle==0);
    const std::uint8_t bytes[]={1,2,3,4};assert(io.write(io.context,handle,bytes,4,transferred)==StorageResult::Ok&&transferred==4);
    const auto previous=closes;assert(io.sync(io.context,handle)&&closes==previous+1);
    // Sync's checked close/reopen must neither truncate nor lose the offset.
    assert(io.write(io.context,handle,bytes,1,transferred)==StorageResult::Ok&&transferred==1);assert(io.close(io.context,handle));assert(!io.close(io.context,handle));
    assert(io.stat(io.context,"/pb/data",size)==StorageResult::Ok&&size==5);
    assert(io.open_read(io.context,"/pb/data",handle)==StorageResult::Ok&&handle==0);
    std::uint8_t out[8];assert(io.seek(io.context,handle,3));assert(io.read(io.context,handle,out,8,transferred)==StorageResult::Ok&&transferred==2&&out[0]==4&&out[1]==1);assert(io.close(io.context,handle));
    assert(io.open_write(io.context,"/pb/second",handle)==StorageResult::Ok);assert(io.close(io.context,handle));
    assert(io.rename(io.context,"/pb/data","/pb/second")==StorageResult::Error);
    assert(io.rename(io.context,"/pb/data","/pb/promoted")==StorageResult::Ok);
    assert(io.remove(io.context,"/pb/data")==StorageResult::Missing);
    assert(io.open_write(io.context,"/pb/failure",handle)==StorageResult::Ok);close_error=true;const auto before=closes;
    assert(!io.sync(io.context,handle));assert(!io.close(io.context,handle));assert(closes==before+1);
    assert(io.open_write(io.context,"/pb/reused",handle)==StorageResult::Ok&&handle==0);assert(io.close(io.context,handle));
    std::size_t total,used;assert(files.capacity(total,used)&&total==1048576&&used==1234);
    for(const char* name:{"promoted","second","failure","reused"})assert(__real_unlink((directory+"/"+name).c_str())==0);
    assert(rmdir(directory.c_str())==0);
}
