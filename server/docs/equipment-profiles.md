# Equipment profiles and sources

Equipment profiles supply starting values for recipe planning. They are bundled
with the Hub and remain available without an internet connection. Opening a
source link is optional; the Hub does not scrape websites or call a cloud service
when selecting a profile or adapting a recipe.

## Pico C, S and Pro defaults

| Field | Default | Basis |
|---|---|---|
| Batch size | 5 L | Nominal batch size, supported by the manufacturer specifications for C/Pro and the Small Brew Companion C/S/Pro recipe guide |
| Planning efficiency | 50% | Small Brew Companion's approximate community estimate, not a measurement of the connected brewer |
| Grain planning limit | 2 kg | Rounded down from [Small Brew Companion’s recommendation to stay under 4.5 lb](https://www.smallbrewcompanion.com/docs/adjust-recipe-advanced-feature/) |

The [Small Brew Companion scaling guide](https://www.smallbrewcompanion.com/docs/scale-recipe-to-pico-c-s-pro-manual-steps/)
uses a 5 L batch and approximately 50% efficiency for C/S/Pro recipes. Its
[recipe-details guide](https://www.smallbrewcompanion.com/docs/recipe-details-overview/)
also identifies this as an estimated efficiency. These are community planning
references, not PicoBrew calibration specifications.

The manufacturer's [Pico Pro specifications](https://picobrew.com/store/products/pico)
and [Pico C specifications](https://picobrew.com/store/products/picoc) separately
specify a nominal 5 L batch. A brew keg's stated volume is not the recipe's batch
volume. Neither specification establishes the efficiency or maximum grain weight
used by a particular reusable basket or brew bag.

The word "Manual" in the SBC scaling guide's title refers to the conversion
steps in its interface. It does not limit that estimate to the appliance's
bag-based Manual Brew program. Its associated brewing-profile guide describes
PicoPak-style compartment routing. The actual setup, ingredients and process
still require individual calibration.

## Hop slots and working limits

The PicoBrew instruction books illustrate how the Hops Pak fits its cradle and how the lid holes align. The manufacturer describes four compartments for timed additions. [Pico S, p. 42](https://picobrewcontent.blob.core.windows.net/content/pico/Pico_Manual.pdf#page=42) · [Pico Pro, p. 40](https://picobrewcontent.blob.core.windows.net/content/picopro/PicoPro_Manual.pdf#page=40) · [Pico C, p. 40](https://picobrewcontent.blob.core.windows.net/content/picoc/PicoC_Manual.pdf#page=40) · [Pico Pro FAQ](https://picobrew.com/about/faq_picopro)

The C/S/Pro guide from Small Brew Companion explains that wort entering a later compartment cascades through earlier ones. For 60- and 15-minute additions, the first slot receives 45 minutes directly and another 15 minutes during the second hold. The Zymatic manual independently describes cascading hop cages. [C/S/Pro brewing profile](https://www.smallbrewcompanion.com/docs/pico-c-s-pro-brewing-profile/) · [Zymatic manual](https://picodevblob.blob.core.windows.net/content/zymatic/Zymatic_Manual.pdf)

Z and Zymatic use 9 lb of grain and 2.5 US gallons per brewing module as planning limits. The Hub applies a 42.5 g combined allowance per timed hop slot across brewing profiles, following the metric allowance in PicoBrew’s recipe crafter. This is a software planning limit. Actual usable mass depends on the insert and the form of the hops. Set a smaller working limit when appropriate. [Z specifications](https://picobrew.com/store/products/z) · [PicoBrew recipe crafter](https://picobrew.com/z_crafter/z_bib)

## Recipe wizard defaults

The Pico C and S/Pro firmware decoders take the step name, a single location, temperature in Fahrenheit, active minutes and drain minutes. The Hub derives these fields from the mash schedule and the hop countdowns. Grain and hop weights are used for planning and loading. The archived programs do not establish the physical weight limits. [S/Pro recipe reconstruction](../../firmware/pico-s-pro/reverse-engineered/docs/recipe-recovery.md) · [Pico C decoder](../../firmware/pico-c/reverse-engineered/src/recipe.c)

The bundled PicoPak archive contains 9,780 PicoPak records with a nonempty step list. In 6,348 records, the mash uses 110, 148, 156 and 176 °F for 7, 25, 20 and 7 minutes, with a two-minute final mash drain. The Hub uses this schedule as the PicoPak preset. Eleven of the twelve Z base recipes and eleven of the twelve Zymatic base recipes use a 152 °F, 90-minute infusion with an eight-minute mash drain. The custom mash option supports other schedules. Native programs retain their original routing, holds and drains, including drains between hop steps. For example, California Common routes directly to hop slots 1, 3, 4 and 2 in that order. The wizard schedules one direct inlet at a time. Return visits reuse the ingredients already loaded in the slot. Each additional visit consumes one machine step. Choose **Start from an original PicoPak** to copy every recorded step, including its individual drain. [California Common source](https://github.com/tmack8001/picopak-library/blob/93a9e104ba696a56f667050eae8e969e0b732e36/recipes/picopaks/1557.json) [Archived PicoPak](https://github.com/tmack8001/picopak-library/blob/93a9e104ba696a56f667050eae8e969e0b732e36/recipes/picopaks/1.json) · [Base recipe archive](https://github.com/chiefwigms/picobrew_pico/tree/176b9fa2e7245a8504120b50e327b51d96d1cf2b/app/static/support/base_recipes)

The archived records also contain ABV/IBU settings and some have per-step timing ranges. The S/Pro fetch request includes the selected adjustments, and the Z PicoPak parser interpolates durations for kits with applicable timing ranges. New guided recipes have explicit times. Ingredient names and weights alone do not supply kit-specific adjustment ranges. [S/Pro recipe request](../../firmware/pico-s-pro/reverse-engineered/src/network/api.c) · [Z program formats](../../firmware/z-series/reverse-engineered/analysis/program-recipes.md)

## Saved calibration

Settings accepts a usable batch volume, efficiency and lower grain or hop slot limits for each brewing model. A weight limit of zero restores the model default. The Hub caps existing overrides at the model planning ceiling when reading them. Recipes and revisions retain their saved contents. The optional calibration field `max_hop_slot_g` is expressed in grams. Older clients can omit it to preserve the current setting.

## What the machine supplies

The current standalone API reports its firmware/API version, UID, supported
commands, recipe/step capacities, history support and live controller readings.
It does not supply batch-volume, gravity, grain-mass or extraction-efficiency
measurements, or an S-versus-Pro model identifier. The Hub uses the model chosen
at machine registration.

Measured efficiency needs brewing observations beyond the current device
telemetry. User-supplied measurements should replace the planning estimate when
available; a published profile is a starting point for that process.
