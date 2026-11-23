#include "picobrew/common_web.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

int main(int argc,char** argv) {
    assert(argc==2);
    assert(pico_common_web_assets_count==8);
    bool found_favicon=false;
    for(size_t i=0;i<pico_common_web_assets_count;++i) {
        const PicoHttpAsset& asset=pico_common_web_assets[i];
        if(std::strcmp(asset.path,"/favicon.ico")==0) {
            assert(std::strcmp(asset.content_type,"image/x-icon")==0);
            found_favicon=true;
        }
        assert(asset.data==nullptr&&asset.file_path&&asset.length>10);
        assert(std::strcmp(asset.content_encoding,"gzip")==0);
        int handle=pico_common_web_asset_io.open(nullptr,asset.file_path,asset.length);
        assert(handle>=0);
        assert(pico_common_web_asset_io.open(nullptr,asset.file_path,asset.length+1)==PICO_HTTP_IO_ERROR);
        std::vector<uint8_t> bytes(asset.length);
        for(size_t offset=0;offset<asset.length;) {
            size_t count=asset.length-offset;if(count>137)count=137;
            assert(pico_common_web_asset_io.read(nullptr,handle,offset,bytes.data()+offset,count)==int(count));offset+=count;
        }
        assert(bytes[0]==0x1f&&bytes[1]==0x8b);
        assert(pico_common_web_asset_io.read(nullptr,handle,asset.length,bytes.data(),1)==PICO_HTTP_IO_ERROR);
        assert(pico_common_web_asset_io.read(nullptr,handle,asset.length+1,bytes.data(),0)==PICO_HTTP_IO_ERROR);
        assert(pico_common_web_asset_io.read(nullptr,-1,0,bytes.data(),1)==PICO_HTTP_IO_ERROR);
        char path[1024];std::snprintf(path,sizeof(path),"%s/asset-%zu.gz",argv[1],i);
        FILE* file=std::fopen(path,"wb");assert(file);assert(std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size());assert(std::fclose(file)==0);
        pico_common_web_asset_io.close(nullptr,handle);
    }
    assert(found_favicon);
    assert(pico_common_web_asset_io.open(nullptr,"/missing",0)==PICO_HTTP_IO_ERROR);
    assert(pico_common_web_asset_io.open(nullptr,nullptr,0)==PICO_HTTP_IO_ERROR);
}
