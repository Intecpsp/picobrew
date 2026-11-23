# Recipe exchange and adaptation

Recipes are canonical brewing plans. A separate machine program contains the
small set of fields the standalone S/Pro firmware can execute. Importing a
recipe or making a planning preview does not operate a machine.

## Canonical units

Batch, boil and water volumes use liters. Temperatures use Celsius. Ingredient
addition times use minutes and retain fractional values. Machine-program step
durations use whole minutes; fractional BeerXML mash/boil durations are rejected
with an explanation instead of being rounded.

| Ingredient kind | Units |
| --- | --- |
| `fermentable` | `kg` |
| `hop` | `g` |
| `water` | `L` |
| `yeast`, `misc` | `g`, `mL`, or `count` |

`count` is preserved in JSON. BeerXML export requires a measured mass or volume
and does not guess the contents of a yeast packet. Fermentation durations are
days. Water ions are concentrations in mg/L; `ph` is dimensionless. Concentrations
and fermentation schedules do not change when the batch is scaled.

## Profiles and native programs

The Hub transfers recipes to standalone Pico S, Pro, C and generic Z brewers over
their local REST APIs. A stock or modified Zymatic fetches its recipe menu from
the Hub. Its records allow up to 25 steps, 207 °F, and 255 minutes per hold or
drain, all within a 511-byte record. Its valve numbers and operator pauses use a
separate encoder. The Z profile represents one brewing module. PicoStill and
PicoFerm have no hot-side recipe programs.

C/S/Pro defaults use 5 L and approximately 50% efficiency from Small Brew
Companion's recipe-scaling guide. PicoBrew's C/Pro specifications also establish
the nominal 5 L batch. The Hub sets a 2 kg grain planning ceiling for these three models.
Z/Zymatic retains its 2.5-US-gallon batch, 9 lb (4.08233133 kg) grain limit and
70% planning assumption. Efficiency defaults are not measurements of a machine.
Saved calibrations override the defaults. See the [profile sources and migration
notes](../../docs/equipment-profiles.md). The configured `BatchLiters` also bounds
the requested batch in the preview.

Automatic machine versions apply a single multiplier to the entire ingredient bill. The grain basket, the heaviest combined hop addition, the measured starting water and any saved usable batch limit each constrain that multiplier. Estimated yield equals the source batch volume times the multiplier. For a bill without a measured load to bound, the Hub falls back to the profile batch volume. An explicit batch request uses the requested volume divided by the source volume and must fit the same equipment.

The Pico S and Pro use the manufacturer’s brew-keg fill line of 1 US gallon plus 6 US cups. The Pico C line is 5.2 L. For a Z module or a Zymatic with the stock 5-US-gallon keg, the Hub uses a conservative 4.27-US-gallon planning ceiling documented in PicoBrew’s *Let It All Hang Out* recipe. The equipment profiles include the manuals and the source recipe. Applying the starting-water estimate in the editor marks the water with the use `starting keg`, which the Hub preserves in its BeerXML water extension. Archived brew-sheet fills and the single “Brewing water” row created by older Hub estimators are also recognized. Unassigned water profiles and sparge additions are separate from the starting charge.

Standalone machine steps use direct routes 1–6. For an ingredient recipe with
fully unmapped steps, the Hub can translate a supported mash, boil and timed
adjunct schedule. A boil-only extract recipe can skip the basket when all its
fermentables are soluble kettle additions. Partially mapped routes require
review. Every scheduled mash stage for a loaded grain basket must heat through
that basket before the boil. Decoction, infusion-volume and timed-ramp
instructions require review. Fermentation and carbonation are tracked in the
brew journal.

The native validator implements the actual firmware limits:

| Field | Limit |
| --- | --- |
| ID | 1–20 ASCII letters, digits, `_`, `-` |
| Recipe / step label | 1–31 / 1–21 printable ASCII bytes |
| Steps | 1–15 |
| Location | 1–6, explicitly provided |
| Temperature | Integer 0 (unheated), or 60–212°F |
| Active / drain duration | 0–599 / 0–120 minutes. Zero-minute steps require the firmware capability. |
| Serialized JSON request | At most 4095 bytes |

Celsius values become integer Fahrenheit targets with explicit rounding warnings.
Zero Celsius in the step program is the unheated sentinel. Native labels replace
unsupported characters with `?` and truncate with explicit warnings; canonical
labels remain unchanged. Long/unsupported canonical IDs get a stable
`hub-<hash>` ID, also disclosed in the preview.

## Post-boil additions

The Hub preserves whirlpool and flameout additions in an ingredient recipe.
Each load needs a physical cage, a target temperature at first contact, a contact
time and a combined weight that fits the cage. A generic hold in the advanced
editor only specifies where to circulate and for how long. Until the recipe
model can bind that hold to its ingredients and validate the temperature at
first contact, these recipes remain available for planning and export.

A review of 39 original PicoBrew post-boil cage programs found 25 whose declared
stand targets, routes and times matched the stored machine steps. Eighteen of
those 25 ran an unheated balance step immediately before cooling. The stock
Zymatic 1.1.14 runner chooses its wait direction for later steps by comparing
the current target with the previous target. Replaying the original AVR
instructions at `0xeb10` and `0xf18a` shows that a 175°F step following an
unheated step can proceed at 183°F. After a 207°F target, the same stock step
waits at 176°F and proceeds at 175.99°F.

Pico S, Pro and C compare against measured wort and can end a cooling step
at 5°F above target. Standalone Z selects its wait direction from measured
wort and uses a 0.2°C tolerance. For a 175°F target followed by a cage hold,
each controller can begin wetting the hops at a different temperature.
These controller checks establish software behavior. They do not measure
actual wort temperatures, cooling rates or extraction during a brew.

## Exchange scope

JSON imports/exports the full canonical model, as one object or an array of
objects. Imports reject unknown fields, duplicate JSON keys, trailing documents,
excessive nesting, empty recipes and files larger than 1 MiB.

BeerXML 1 import/export supports recipe type, batch/boil volume and duration,
efficiency, style name, notes, fermentables, hops, yeast, miscellaneous additions,
water chemistry, mash holds/infusion/ramp details, up to three fermentation
phases, and carbonation method/volume/temperature. It preserves the standardized
kilogram/liter/Celsius/minute units through explicit conversions. UTF-8 and
ISO-8859-1 input are supported. DTDs/directives and external entities are rejected.

This is a documented exchange subset, not full fidelity for every BeerXML field.
Equipment profiles, full BJCP style statistics, brewer identity, advanced yeast
properties, priming-sugar details and other optional fields are not represented.
Hub descriptions/tags/revisions and custom fermentation stage names are preserved
by JSON, not by standard BeerXML. Multiple distinct water-chemistry profiles are
rejected because the canonical recipe has one common water profile.

BeerXML export requires explicit recipe type (`All Grain`, `Partial Mash` or
`Extract`), positive `boil_liters`/`boil_minutes`, ingredient subtypes, and yeast
form when applicable. It does not substitute finished batch volume for pre-boil
volume. Case-insensitive recognized types emit standard BeerXML casing.

A `HUB_STEPS` extension stores the canonical step array as escaped JSON, retaining
explicit locations and drain times on a hub round-trip. Other BeerXML applications
may ignore/remove that extension. Standard BeerXML imported without it starts
with unmapped locations and goes through the same recipe translation before transfer. Native
firmware JSON is a distinct schema; it is not silently treated as a complete
canonical recipe because it lacks batch size and efficiency.

## Source contracts

- [Standalone firmware API: recipe bounds and location semantics](../../../firmware/pico-s-pro/modified/docs/standalone-api.md#recipes-sixteen-records-fifteen-steps-each)
- [Standalone request parser](../../../firmware/pico-s-pro/modified/src/local_api.c)
- [BeerXML 1 specification](https://www.beerxml.com/beerxml.htm)
- [Pico Pro product specification](https://picobrew.com/store/products/pico)
- [Pico C product specification](https://picobrew.com/store/products/picoc)
- [Z product specification](https://picobrew.com/store/products/z)
- [Zymatic manual, brewing and ingredient capacities](https://picodevblob.blob.core.windows.net/content/zymatic/Zymatic_Manual.pdf)
