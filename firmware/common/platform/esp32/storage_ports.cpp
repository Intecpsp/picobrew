#include "storage_ports.hpp"
#include <esp_spiffs.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>

namespace picobrew { namespace standalone { namespace esp32 {
using namespace picobrew::standalone;
namespace {
StorageResult error_result(){return errno==ENOENT?StorageResult::Missing:StorageResult::Error;}
}
bool SpiffsFiles::mount(){
    esp_vfs_spiffs_conf_t configuration{};configuration.base_path="/spiffs";
    configuration.partition_label=nullptr;configuration.max_files=6;configuration.format_if_mount_failed=false;
    const auto result=esp_vfs_spiffs_register(&configuration);
    mounted_=result==ESP_OK||(result==ESP_ERR_INVALID_STATE&&esp_spiffs_mounted(nullptr));
    return mounted_;
}
bool SpiffsFiles::capacity(std::size_t& total,std::size_t& used) const {
    return mounted_&&esp_spiffs_info(nullptr,&total,&used)==ESP_OK;
}
bool SpiffsFiles::path(const char* logical,char(&out)[48]) const noexcept {
    if(!mounted_||!logical||logical[0]!='/'||std::strlen(logical)>=32||std::strstr(logical,".."))return false;
    const int count=::snprintf(out,sizeof out,"/spiffs%s",logical);
    return count>0&&static_cast<std::size_t>(count)<sizeof out;
}
SpiffsFiles::OpenFile* SpiffsFiles::file(FileHandle handle) noexcept {
    return handle>=0&&handle<6&&open_[handle].used?&open_[handle]:nullptr;
}
StorageResult SpiffsFiles::open(const char* logical,bool writing,FileHandle& handle){
    handle=invalid_file_handle;char name[48];if(!path(logical,name))return StorageResult::Error;
    for(FileHandle i=0;i<6;++i)if(!open_[i].used){
        const int fd=::open(name,writing?(O_WRONLY|O_CREAT|O_TRUNC):O_RDONLY,0600);
        if(fd<0)return writing?StorageResult::Error:error_result();
        auto& slot=open_[i];slot.descriptor=fd;slot.used=true;slot.writing=writing;std::strcpy(slot.path,name);handle=i;return StorageResult::Ok;
    }
    return StorageResult::Error;
}
FileIo SpiffsFiles::io() noexcept {
    FileIo io;io.context=this;
    io.stat=[](void* p,const char* logical,std::size_t& size){char path[48];
        if(!static_cast<SpiffsFiles*>(p)->path(logical,path))return StorageResult::Error;
        struct stat information{};
        if(::stat(path,&information)<0)return error_result();
        if(information.st_size<0||!S_ISREG(information.st_mode))return StorageResult::Error;
        size=static_cast<std::size_t>(information.st_size);return StorageResult::Ok;
    };
    io.open_read=[](void* p,const char* logical,FileHandle& handle){return static_cast<SpiffsFiles*>(p)->open(logical,false,handle);};
    io.open_write=[](void* p,const char* logical,FileHandle& handle){return static_cast<SpiffsFiles*>(p)->open(logical,true,handle);};
    io.seek=[](void* p,FileHandle handle,std::size_t offset){auto* slot=static_cast<SpiffsFiles*>(p)->file(handle);
        if(!slot||slot->descriptor<0||offset>static_cast<std::size_t>(std::numeric_limits<off_t>::max()))return false;
        return ::lseek(slot->descriptor,static_cast<off_t>(offset),SEEK_SET)==static_cast<off_t>(offset);
    };
    io.read=[](void* p,FileHandle handle,std::uint8_t* data,std::size_t size,std::size_t& transferred){
        transferred=0;auto* slot=static_cast<SpiffsFiles*>(p)->file(handle);if(!slot||slot->descriptor<0)return StorageResult::Error;
        const ssize_t count=::read(slot->descriptor,data,size);if(count<0)return StorageResult::Error;
        transferred=static_cast<std::size_t>(count);return StorageResult::Ok;
    };
    io.write=[](void* p,FileHandle handle,const std::uint8_t* data,std::size_t size,std::size_t& transferred){
        transferred=0;auto* slot=static_cast<SpiffsFiles*>(p)->file(handle);if(!slot||slot->descriptor<0||!slot->writing)return StorageResult::Error;
        const ssize_t count=::write(slot->descriptor,data,size);if(count<0)return StorageResult::Error;
        transferred=static_cast<std::size_t>(count);return StorageResult::Ok;
    };
    io.sync=[](void* p,FileHandle handle){auto* slot=static_cast<SpiffsFiles*>(p)->file(handle);
        if(!slot||slot->descriptor<0)return false;
        if(!slot->writing)return true;
        // This historical SPIFFS VFS has no fsync callback. Its checked close
        // calls SPIFFS_close and flushes the write cache. Reopen without truncate
        // and restore the offset, retaining the same logical FileIo handle.
        const off_t offset=::lseek(slot->descriptor,0,SEEK_CUR);if(offset<0)return false;
        const int fd=slot->descriptor;slot->descriptor=-1;if(::close(fd)!=0)return false;
        slot->descriptor=::open(slot->path,O_WRONLY);if(slot->descriptor<0)return false;
        return ::lseek(slot->descriptor,offset,SEEK_SET)==offset;
    };
    io.close=[](void* p,FileHandle handle){auto* slot=static_cast<SpiffsFiles*>(p)->file(handle);if(!slot)return false;
        const int fd=slot->descriptor;slot->descriptor=-1;slot->used=false;return fd>=0&&::close(fd)==0;};
    io.rename=[](void* p,const char* old_name,const char* new_name){char old_path[48],new_path[48];auto& self=*static_cast<SpiffsFiles*>(p);
        if(!self.path(old_name,old_path)||!self.path(new_name,new_path))return StorageResult::Error;
        // FileIo forbids implicit replacement, even when the VFS permits it.
        struct stat existing{};if(::stat(new_path,&existing)==0||errno!=ENOENT)return StorageResult::Error;
        if(::rename(old_path,new_path)<0)return error_result();
        return StorageResult::Ok;
    };
    io.remove=[](void* p,const char* name){char path[48];if(!static_cast<SpiffsFiles*>(p)->path(name,path))return StorageResult::Error;
        if(::unlink(path)<0)return error_result();
        return StorageResult::Ok;};
    return io;
}
}}}
