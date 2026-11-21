# Beer Crafter in the Hub

The shared recipe wizard follows PicoBrew’s grain, mash, boil, hop-bin and yeast workflow. Open **Recipes → Create recipe**. Review the grain and hop bills, predicted beer statistics and wort-temperature curve before saving. **Advanced editor** offers OG and IBU scaling and detailed hot- and cold-side planning. Existing ingredient recipes open directly in that editor.

| Original Crafter task | Where to do it |
| --- | --- |
| Pick a style, build a grain and hop bill, choose yeast | Start typing an ingredient or style in the wizard or editor. Choosing a reference fills its known properties. Custom names and measured ingredient properties are supported. Set recipe measurements to metric or US customary. |
| Compare the grain and hop bills | Each pie represents the bill by weight. Point at a slice or focus a legend item for the ingredient weight and see its percentage in the legend. |
| Fit a style or scale toward a target | Recipe analysis compares OG, FG, ABV, IBU and color with the selected historical style. Scaling preserves ratios. Raising OG can change predicted IBU. Transfer preview checks the destination profile. |
| Program mash rests, boil and hops | The wizard offers the model default, infusion, mash-out, high efficiency and custom rests. Each loaded hop slot can hold multiple varieties and receive successive timed visits. For first wort hopping, schedule an addition at the beginning of the boil and select the option to route heat-up through that slot. In the advanced schedule, edit individual targets (including boil temperature), holds, drains and routes. |
| Plan whirlpool, late additions and fermentation | Add a whirlpool hop stand from the advanced step menu. Record aroma or dry hops in the ingredient bill. Choose a yeast and edit attenuation, fermentation temperatures and durations. Plan chilling and carbonation separately. Cooling steps require a compatible controller or manual execution. |
| Estimate starting water and track amendments | Enter the pre-boil volume, measured absorption and equipment loss. Apply the estimate to the water ingredient. Multiple existing water sources can be edited individually. Record amendment quantities and water chemistry. |
| Review, keep, share or print | Review the planned temperature curve and print the brew sheet. The local library supports revisions, copies, JSON and BeerXML exchange, community recipe browsing and machine-specific transfer previews. Native programs retain their original schedule and have a curve and print view. |

## Reference data and estimates

The bundled reference contains 228 fermentables, 179 hops, 309 yeasts, 110 adjuncts, five amendments and 200 historical style entries. These are factual fields exposed by [PicoBrew Beer Crafter](https://picobrew.com/z_crafter/z_recipe). Style names retain their published guide and edition. The [Zymatic manual, recipe design section](https://picodevblob.blob.core.windows.net/content/zymatic/Zymatic_Manual.pdf) describes scaling, bin operation, mash selection and fermentation planning.

Gravity uses fermentable extract potential (46 gravity points per pound per US gallon for sucrose), finished volume and brewhouse efficiency. Sugars and extracts contribute without a mash-efficiency deduction. Final gravity uses the entered apparent attenuation. Mixed strains require a common attenuation to produce one estimate. Alcohol uses the gravity difference times 131.25. Color uses the Morey formula. Bitterness uses Tinseth for timed boil and first-wort hops with pre-boil gravity when the volume is known. Dry hops and whirlpool additions carry no assumed utilization. Missing ingredient properties leave dependent estimates blank. PicoBrew’s server-side brewing predictions and equipment loss calibration are unavailable. [Calculation reference](https://docs.brewfather.app/recipes/calculations)

The forecast starts at 55 °F and uses Crafter’s approximate 4.96 °C/min heating and 0.585 °C/min cooling rates unless the recipe specifies a ramp. Pauses have unknown wall-clock duration. Unheated stretches have unknown temperature. The live brew journal uses measured temperatures. Starting-water estimates use the brewer’s own measured losses.

Pico S, Pro and C use their PicoPak routing. Generic Z uses its canonical direct-valve schedule. Zymatic supports planning and export. PicoStill and PicoFerm retain their distillation and fermentation controls. Hosted account publishing and KegSmarts fermentation control are outside the local Hub’s supported integrations.
