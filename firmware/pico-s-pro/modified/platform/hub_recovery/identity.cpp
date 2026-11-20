extern "C" {
#include <utility/simplelink.h>
#include <utility/fs.h>
}
extern "C" bool pico_recovery_read_uid(char uid[33])
{
    const _u8 name[] = "sys/uid.txt";
    SlFsFileInfo_t info = {};
    _i32 handle = -1;
    if (sl_FsGetInfo(name, 0, &info) != 0 || (info.FileLen != 32 && info.FileLen != 33) ||
        sl_FsOpen(name, FS_MODE_OPEN_READ, 0, &handle) != 0) return false;
    int count = sl_FsRead(handle, 0, reinterpret_cast<_u8 *>(uid), info.FileLen);
    if (sl_FsClose(handle, 0, 0, 0) != 0 || count != static_cast<int>(info.FileLen) ||
        (count == 33 && uid[32] != 0)) return false;
    for (unsigned i=0; i<32; ++i)
        if (!(uid[i]>='0' && uid[i]<='9') && !(uid[i]>='a' && uid[i]<='f')) return false;
    uid[32] = 0;
    return true;
}
