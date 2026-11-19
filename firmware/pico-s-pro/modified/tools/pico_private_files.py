"""Locate the shared privacy helper, also when Pico tools are copied standalone."""

import importlib.util
from pathlib import Path
import sys


def _load():
    here = Path(__file__).resolve().parent
    candidates = [here / "private_files.py"]
    # Repository: firmware/pico-s-pro/modified/tools -> root/tools.
    if len(here.parents) >= 4:
        candidates.append(here.parents[3] / "tools" / "private_files.py")
    for path in candidates:
        if path.is_file():
            name = "picobrew_shared_private_files"
            if name in sys.modules:
                return sys.modules[name]
            spec = importlib.util.spec_from_file_location(name, path)
            module = importlib.util.module_from_spec(spec)
            sys.modules[name] = module
            spec.loader.exec_module(module)
            return module
    raise ImportError(
        "Missing private_files.py. Run the Pico tools from the complete repository, "
        "or copy root tools/private_files.py and pico_private_files.py beside the "
        "standalone scripts. Native file protection is required on every platform.")


_helper = _load()
PrivateFileError = _helper.PrivateFileError
new_private_file = _helper.new_private_file
read_regular = _helper.read_regular
assert_private = _helper.assert_private
ensure_private_directory = _helper.ensure_private_directory
ensure_private_file = _helper.ensure_private_file
publish = _helper.publish
discard = _helper.discard
