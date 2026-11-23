"""Select a Zymatic application using the boot window verified for this unit."""

from functools import partial
from types import SimpleNamespace

import hashlib
import json
import re

import boot_evidence
import zymatic_build as build
import release_1_2
import usb_stock as stock

CONSERVATIVE = 0x1e000
MAX_APPLICATION = 0x1fc00


def identify(flash):
    if stock.is_original_app(flash[:stock.APP_SIZE]):
        return None
    legacy = release_1_2.recognize_private_application(flash)
    if legacy or build.VERSION == release_1_2.VERSION:
        return legacy
    for version in (build.VERSION, *reversed(build.ARCHIVED_MANIFESTS)):
        installed = build.recognize_private_application(flash, version=version)
        if installed:
            return installed
    return None


def for_version(version):
    if version == release_1_2.VERSION:
        return release_1_2
    if version in build.ARCHIVED_MANIFESTS:
        return SimpleNamespace(
            VERSION=version, template=partial(build.template, version=version),
            build_application=partial(build.build_application, version=version),
            recognize_private_application=partial(build.recognize_private_application, version=version),
        )
    if version == build.VERSION:
        return build
    raise ValueError("Unknown Zymatic application version")


def best_fit(limit):
    if build.VERSION != release_1_2.VERSION:
        high, _ = build.template()
        if build.START + len(high) <= limit:
            return build
    return release_1_2


def qualified_limit(directory, flash, installed):
    """Match a prior ISP measurement using the provisioned unit and exact boot bytes.

    A shared stock image and the same USB port cannot identify which unit
    supplied the fuses. Convert stock using the conservative layout first.
    Conflicting fuse observations use the smaller application window.
    """
    if not installed or len(flash) != boot_evidence.FLASH_BYTES:
        return CONSERVATIVE
    limits = []
    for path in directory.glob("isp-evidence-v1-*.json"):
        raw = stock.private_files.read_regular(path, 16384, private=True)
        if path.name != "isp-evidence-v1-" + hashlib.sha256(raw).hexdigest() + ".json":
            continue
        report = json.loads(raw)
        digest = report.get("flash_sha256", "")
        if (report.get("schema_version") != 1 or report.get("interface") != "atmelice_isp"
                or report.get("signature") != "1e9705" or not re.fullmatch("[a-f0-9]{64}", digest)
                or digest != report.get("usb_backup_sha256")):
            continue
        measured = stock.private_files.read_regular(directory / f"isp-flash-{digest}.bin",
                                                    boot_evidence.FLASH_BYTES, private=True)
        if len(measured) != len(flash) or hashlib.sha256(measured).hexdigest() != digest:
            continue
        prior = identify(measured)
        if not prior or prior["uid"] != installed["uid"]:
            continue
        hfuse = int(report["registers"]["hfuse"], 16)
        lock = int(report["registers"]["lock"], 16)
        size = boot_evidence.BOOT_BYTES[(hfuse >> 1) & 3]
        region = report["configured_boot_region"]
        same_boot = measured[-size:] == flash[-size:] and len(set(flash[-size:])) > 1
        readable = lock & 3 in (2, 3) and lock & 12 == 12
        if (not same_boot or not readable or hfuse & 1 or region["size_bytes"] != size
                or region["sha256"] != hashlib.sha256(flash[-size:]).hexdigest()):
            limits.append(CONSERVATIVE)
        else:
            limits.append(len(flash) - size)
    return min(limits, default=CONSERVATIVE)
