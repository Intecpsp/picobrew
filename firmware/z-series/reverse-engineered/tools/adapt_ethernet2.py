"""Build-copy adaptation; never edit the acquired upstream source cache."""
import json
from pathlib import Path
import acquire_ethernet2


def prepare(source, output):
    source, output = Path(source).resolve(), Path(output).resolve()
    if source == output or source in output.parents or output in source.parents:
        raise ValueError('Ethernet2 source and adapted directories must be independent')
    acquire_ethernet2.verify_source(source)
    for name in json.loads(acquire_ethernet2.PIN.read_text())['files']:
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((source / name).read_bytes())

    def replace(name, old, new, count=1):
        path = output / name
        text = path.read_text()
        if text.count(old) != count:
            raise RuntimeError('Unexpected Ethernet2 adaptation context: ' + name)
        path.write_text(text.replace(old, new))

    replace('src/utility/w5500.h', '  void init(uint8_t ss_pin = 10);',
            '  void setSPI(SPIClass& bus);\n  void init(uint8_t ss_pin = 10);')
    replace('src/utility/w5500.cpp', 'uint8_t SPI_CS;',
            'uint8_t SPI_CS;\nstatic SPIClass* z_spi = &SPI;\nvoid W5500Class::setSPI(SPIClass& bus) { z_spi = &bus; }')
    replace('src/utility/w5500.cpp', '  SPI.begin();',
            '  // Z System has already initialized this SPI bus and its physical pins.')
    path = output / 'src/utility/w5500.cpp'
    text = path.read_text()
    if text.count('SPI.') != 30:
        raise RuntimeError('Unexpected Ethernet2 SPI operation count')
    path.write_text(text.replace('SPI.', 'z_spi->'))
    replace('src/Ethernet2.h', '  int begin(uint8_t *mac_address);',
            '  int begin(uint8_t *mac_address, unsigned long timeout = 60000, unsigned long response_timeout = 4000);')
    replace('src/Ethernet2.cpp', 'int EthernetClass::begin(uint8_t *mac_address)',
            'int EthernetClass::begin(uint8_t *mac_address, unsigned long timeout, unsigned long response_timeout)')
    # Only the explicit-MAC overload is used by Z; the WIZ550io variant remains original.
    path = output / 'src/Ethernet2.cpp'
    text = path.read_text(); prefix, tail = text.split('#else\n', 1)
    old = '_dhcp->beginWithDHCP(mac_address)'
    if tail.count(old) != 1: raise RuntimeError('Unexpected Ethernet2 DHCP context')
    path.write_text(prefix + '#else\n' + tail.replace(old, '_dhcp->beginWithDHCP(mac_address, timeout, response_timeout)'))
    return output
