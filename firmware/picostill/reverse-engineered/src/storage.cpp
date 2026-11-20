#include "picostill/storage.hpp"
#include <cstring>

namespace picostill { namespace oem {
bool CredentialStorage::begin(std::uint32_t requested) {
    if(!requested)return false;
    int result=ports_.open(key,handle);
    if(result){ports_.error(0x20,result);return false;}
    std::size_t stored_size=0;
    result=ports_.get_blob(handle,key,nullptr,stored_size);
    if(result && result!=0x1102){ports_.error(0x27,result);return false;}
    ports_.key_size(static_cast<std::uint32_t>(stored_size));
    if(requested<stored_size) {
        auto* scratch=ports_.allocate(stored_size);
        if(!scratch){ports_.error(0x2e);return false;}
        ports_.get_blob(handle,key,scratch,stored_size);
        ports_.set_blob(handle,key,scratch,requested);ports_.commit(handle);ports_.release(scratch,false);
    } else if(stored_size<requested) {
        const auto extra=requested-stored_size;
        auto* scratch=ports_.allocate(extra);
        if(!scratch){ports_.error(0x3a);return false;}
        result=ports_.set_blob(handle,"expand",scratch,extra);
        if(result){ports_.error(0x3f,0,stored_size,requested);ports_.release(scratch,false);return false;}
        ports_.release(scratch,false);ports_.erase_key(handle,"expand");
        scratch=ports_.allocate(requested);
        if(!scratch){ports_.error(0x47);return false;}
        std::memset(scratch,0,requested);
        if(stored_size){ports_.get_blob(handle,key,scratch,stored_size);ports_.erase_key(handle,key);}
        ports_.commit(handle);ports_.set_blob(handle,key,scratch,requested);ports_.release(scratch,false);ports_.commit(handle);
    }
    if(buffer)ports_.release(buffer,true);
    buffer=ports_.allocate(requested);
    if(!buffer){ports_.error(0x5f);return false;}
    size=requested;ports_.get_blob(handle,key,buffer,size);ports_.ready();return true;
}
void CredentialStorage::initialize() {
    begin(requested_size);diagnostics_.check(sizeof(EepromData)<=requested_size,"sizeof(EepromData) <= Size");
}
void CredentialStorage::load(EepromData& destination) {
    if(size>=sizeof(destination) && buffer)std::memcpy(&destination,buffer,sizeof(destination));
    diagnostics_.require(destination.version==0,"EepromData.h",0x1e,"Version == 0");
}
void CredentialStorage::save(const EepromData& source) {
    // A missing allocation with a nonzero size is not a valid OEM object.
    if(size>=sizeof(source) && buffer){std::memcpy(buffer,&source,sizeof(source));dirty=true;}
    if(size && buffer && dirty){
        if(ports_.set_blob(handle,key,buffer,size)){ports_.error(0x77);return;}
        // The original commit wrapper does not call nvs_commit here.
        dirty=false;
    }
}
}} // namespace picostill::oem
