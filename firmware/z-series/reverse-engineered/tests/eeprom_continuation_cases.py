"""Reuse the full established data/lifecycle matrix with wrapping diagnostics."""
from eeprom_cases import cases as legacy_cases
def cases():
 return [dict(case,counter=0xffffffff if index%2 else 7,clock=0xffffff80 if index%2 else 1000)for index,case in enumerate(legacy_cases())]
