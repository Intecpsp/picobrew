"""Use the original 1.3.0 overlay to identify and reproduce previously provisioned brewers."""
from functools import partial

import zymatic_build as build

VERSION = "1.3.0"
template = partial(build.template, version=VERSION)
build_application = partial(build.build_application, version=VERSION)
recognize_private_application = partial(build.recognize_private_application, version=VERSION)
