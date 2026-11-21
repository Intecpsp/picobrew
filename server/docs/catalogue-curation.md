# Catalog curation review

Reviewed all **5,282** previously browsable entries and their **11,164** associated original records. Removed **66 entries** representing **498 original records** from browsing. The resulting catalog has **5,216 entries**.

All **11,518** recipe/program source files remain preserved, including the **354** malformed or empty records already excluded before this review. Eight firmware starter programs and all legitimate Chiefwigms/ZPak records remain available. User-saved recipes, revisions and installed brewer programs are unaffected.

The review combines every entry's name, notes, actual ingredient/program content, grouped source variants and (where available) named successors. A name containing “test,” an experimental recipe, missing native ingredient quantities, or an unusual beer name is not sufficient evidence by itself.

[The machine-readable manifest](../tools/catalog-curation.json) contains the complete source paths, checksums and evidence for every decision. It is applied by the reproducible builder; all original bytes and attribution remain in the embedded archive.

## Removed entries

| Entry | Name | Source records | Reason |
| --- | --- | ---: | --- |
| `picopak-1108` | dfghdfg | 1 | keyboard or symbol placeholder |
| `picopak-1109` | fghjfghj | 2 | keyboard or symbol placeholder |
| `picopak-1110` | dfgh | 1 | keyboard or symbol placeholder |
| `picopak-1113` | dfsgdag | 1 | keyboard or symbol placeholder |
| `picopak-1139` | dsfsdf | 1 | keyboard or symbol placeholder |
| `picopak-1253` | Test #1 | 1 | explicit test or demo |
| `picopak-1285` | Test | 1 | explicit test or demo |
| `picopak-1386` | PicoPak test | 1 | explicit test or demo |
| `picopak-1441` | Blank Beer | 197 | explicit default placeholder |
| `picopak-1516` | Blank Beer | 139 | explicit default placeholder |
| `picopak-1592` | Blank Beer | 33 | explicit default placeholder |
| `picopak-1867` | Test1 | 2 | explicit test or demo |
| `picopak-1869` | Test1 | 1 | explicit test or demo |
| `picopak-200` | TEST | 1 | explicit test or demo |
| `picopak-2037` | Blank Beer | 33 | explicit default placeholder |
| `picopak-2340` | )))TestBrew(((:) | 1 | explicit test or demo |
| `picopak-2348` | MY BEER NAME | 1 | explicit default placeholder |
| `picopak-290` | Blank Beer | 1 | explicit default placeholder |
| `picopak-293` | tasdasdsdvxcv | 1 | keyboard or symbol placeholder |
| `picopak-307` | this is a test | 1 | explicit test or demo |
| `picopak-402` | Test-1 | 1 | explicit test or demo |
| `picopak-417` | test | 1 | explicit test or demo |
| `picopak-429` | test | 1 | explicit test or demo |
| `picopak-448` | test -no notification | 1 | developer notification test |
| `picopak-4798` | Test 2 | 4 | explicit test or demo |
| `picopak-5322` | Test Test Test | 1 | explicit test or demo |
| `picopak-589` | default test | 1 | explicit test or demo |
| `picopak-590` | test | 1 | explicit test or demo |
| `picopak-6150` | Test 2 | 1 | explicit test or demo |
| `picopak-622` | Test Product | 1 | explicit test or demo |
| `picopak-639` | test | 1 | explicit test or demo |
| `picopak-727` | Testbeer | 1 | explicit test or demo |
| `picopak-7353` | TestBeer | 1 | non brewing test fixture |
| `picopak-7387` | TestBeer | 7 | non brewing test fixture |
| `picopak-740` | asdftest | 1 | keyboard or symbol placeholder |
| `picopak-743` | ksjhdgf | 1 | keyboard or symbol placeholder |
| `picopak-749` | Test4561 | 1 | explicit test or demo |
| `picopak-750` | TestBeer877 | 1 | explicit test or demo |
| `picopak-751` | Test678678 | 2 | explicit test or demo |
| `picopak-758` | Test123 | 1 | explicit test or demo |
| `picopak-7636` | TestBeer | 18 | non brewing test fixture |
| `picopak-764` | testset56 | 1 | explicit test or demo |
| `picopak-773` | test | 1 | explicit test or demo |
| `picopak-7809` | New Recipe | 1 | explicit default placeholder |
| `picopak-817` | Test 1 | 1 | explicit test or demo |
| `picopak-819` | My Demo test Beer's | 2 | explicit test or demo |
| `picopak-823` | )(*&^%$#@!~:";'./>? | 1 | keyboard or symbol placeholder |
| `picopak-839` | jhsdgfjhsdg | 1 | keyboard or symbol placeholder |
| `picopak-840` | WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW | 1 | keyboard or symbol placeholder |
| `picopak-841` | okname | 1 | explicit default placeholder |
| `picopak-843` | Test | 1 | explicit test or demo |
| `picopak-8478` | testing v1 | 1 | explicit test or demo |
| `picopak-851` | test | 1 | explicit test or demo |
| `picopak-852` | okname2 | 1 | explicit default placeholder |
| `picopak-856` | okname3 | 1 | explicit default placeholder |
| `picopak-857` | fsdasdfdsads | 1 | keyboard or symbol placeholder |
| `picopak-866` | okname4 | 1 | explicit default placeholder |
| `picopak-876` | okname5 | 1 | explicit default placeholder |
| `picopak-898` | Test 1 | 2 | explicit test or demo |
| `picopak-924` | test | 4 | explicit test or demo |
| `picopak-928` | test | 1 | explicit test or demo |
| `picopak-935` | Test | 1 | explicit test or demo |
| `public-66e48d3d7e0a4a6bb4fef01133486ad1` | Water Test | 1 | diagnostic program |
| `public-a14bb3e5914543119aa44b735846ab9d` | Boil Test PT | 1 | diagnostic program |
| `public-b870b471238e4e5497a19a77dfcee8f6` | Standards Test | 1 | diagnostic program |
| `public-c245546c9e1a40ccaf9bdb6ecca6b31e` | New Recipe | 1 | superseded unnamed draft |

## Preserved cases

Named ingredient experiments, useful maintenance/extraction programs and ambiguous entries remain. Four of the five public records titled “New Recipe” contain distinct substantive recipes and were retained; the removed draft has a direct, byte-equivalent named successor. Native program variants are only grouped where every original field matches apart from its source ID. No good variant was discarded inside a reviewed group.
