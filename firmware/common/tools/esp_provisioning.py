"""Place one device key into the release's verified empty SPIFFS image."""
import hashlib
import re
import struct

import spiffs_format


# Exact empty images from the release formatters. Refuse to modify a populated
# or differently formatted filesystem, even if its logical directory is empty.
EMPTY = {
    "esp8266-2m": "18a484702761fe942d97a59918fabc46d3f375c6e7c33ad252fbf612797aff48",
    "esp32-4m": "5ab1eab8391a2fb3e71c21c348b2c283e356cad0bcb7881367860279357909a6",
}


def stage(empty, profile, key):
    if not isinstance(key, str) or not re.fullmatch("[0-9a-f]{64}", key) or key in ("0"*64, "f"*64):
        raise ValueError("USB setup needs a private, non-default device key")
    if hashlib.sha256(empty).hexdigest() != EMPTY.get(profile):
        raise ValueError("The release filesystem is not the verified empty image for this machine")
    image = bytearray(empty)
    value = key.encode("ascii")
    # The pinned formatter assigns object 1 its header in page 1 and its
    # first span in page 2. ESP32's mtime update finalizes a new object header
    # in page 3 and retires page 1. The other blocks retain their release bytes.
    page = 256
    file_id = 0x8001
    esp32 = profile == "esp32-4m"
    if esp32:
        struct.pack_into("<HHH", image, 0, 0, 1, file_id)
    else:
        struct.pack_into("<HH", image, 0, file_id, 1)
    def header(number, flags, metadata):
        start = page * number
        struct.pack_into("<HHBxxxIB32s", image, start,
                         file_id, 0, flags, len(value), 1, b"/pb/api.key")
        image[start+45:start+45+len(metadata)] = metadata
        struct.pack_into("<H", image, start+45+len(metadata), 2)
    if esp32:
        header(1, 0x78, b"\xff"*4)
        header(3, 0xf8, bytes(4))
    else:
        header(1, 0xf8, bytes(1))
    struct.pack_into("<HHB", image, 2*page, 1, 0, 0xfc)
    image[2*page+5:2*page+5+len(value)] = value
    result = bytes(image)
    spiffs_format.inspect_image(result, profile, {"/pb/api.key": value})
    return result


def matches_stored_esp32_key(image, key):
    """Read the live one-page key the device would select, without modifying SPIFFS."""
    if len(image) != 0x200000 or not isinstance(key, str) or not re.fullmatch("[0-9a-f]{64}", key):
        return False
    page_size, block_size = 256, 4096
    count = len(image) // block_size
    found = {}
    for block in range(count):
        base = block * block_size
        if struct.unpack_from("<H", image, base + 252)[0] != (0x529 ^ page_size ^ (count - block)):
            return False
        for entry in range(15):
            object_id = struct.unpack_from("<H", image, base + 2*entry)[0]
            if object_id in (0, 0xffff) or not object_id & 0x8000:
                continue
            offset = base + page_size * (entry + 1)
            owner, span, flags = struct.unpack_from("<HHB", image, offset)
            if owner != object_id or span or flags != 0xf8:
                continue
            name = bytes(image[offset+13:offset+45]).split(b"\0", 1)[0]
            if name not in (b"/pb/api.key", b"/pb/api.key.bak"):
                continue
            if name in found:
                return False
            size = struct.unpack_from("<I", image, offset+8)[0]
            page = struct.unpack_from("<H", image, offset+49)[0]
            if size not in (64, 65, 66) or page >= len(image)//page_size or page % 16 == 0 or image[offset+12] != 1:
                return False
            reference = page*page_size
            lookup = (page//16) * block_size + 2*(page % 16 - 1)
            if (struct.unpack_from("<HHB", image, reference) != (owner & 0x7fff, 0, 0xfc)
                    or struct.unpack_from("<H", image, lookup)[0] != (owner & 0x7fff)):
                return False
            found[name] = image[reference+5:reference+5+size]
    record = found.get(b"/pb/api.key", found.get(b"/pb/api.key.bak"))
    value = key.encode("ascii")
    return record in (value, value+b"\n", value+b"\r\n")
