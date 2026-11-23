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
