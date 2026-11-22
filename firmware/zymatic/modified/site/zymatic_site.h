/* Served directly from AVR flash. The password has no HTML name so even a
 * form submission with scripting disabled cannot put it in the URL. */
#pragma once
#ifdef __AVR__
#include <avr/pgmspace.h>
#else
#ifndef PROGMEM
#define PROGMEM
#endif
#endif

static const char zymatic_site[] PROGMEM =
"<!doctype html><meta name=viewport content='width=device-width'>"
"<title>Zymatic</title><style>:root{color-scheme:light dark;font:1em system-ui}</style>"
"<h1>PicoBrew Zymatic</h1><form onsubmit='statusOf(this);return false'>"
"<label>Device key <input type=password autocomplete=off required></label>"
"<button>View status</button> <output name=result></output></form>"
"<script>async function statusOf(f){try{"
"let r=await fetch('/api/v1/status',{headers:{Authorization:'Bearer '+f.elements[0].value}});"
"if(!r.ok)throw r.status;let s=await r.json();"
"f.elements.result.value=(s.busy?'Program running':'Monitoring')+(s.wort_f?' / '+s.wort_f+' F':'')"
"}catch(e){f.elements.result.value=e==401?'Check the device key.':'Status unavailable.'}}</script>";
