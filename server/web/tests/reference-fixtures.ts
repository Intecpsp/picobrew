import type { CatalogReferences } from '../src/types'
import { referenceDocument, type ReferenceAsset } from '../src/lib/referenceLinks'

const commit = 'a'.repeat(40)
export function fixtureAsset(
  sourcePath: string,
  type = 'text/plain; charset=utf-8',
  bundled = true,
  source = 'support'
): ReferenceAsset {
  return {
    path: `${source}/${sourcePath}`,
    url: `/api/v1/catalog/assets/${source}/${sourcePath}`,
    title: sourcePath.split('/').pop()!,
    kind: 'reference-text',
    models: ['zymatic'],
    source_id: source,
    source_path: sourcePath,
    sha256: 'b'.repeat(64),
    bytes: 100,
    content_type: type,
    attribution: 'Original guide contributor',
    license: 'Original documentation notice',
    bundled,
    download_url: `https://raw.githubusercontent.com/example/${source}/${commit}/${sourcePath}`,
  }
}
export const guideAsset = fixtureAsset('legacy/zymatic/Firmware-Installation.md')
export const nextAsset = fixtureAsset('legacy/zymatic/Firmware.md')
export const imageAsset = fixtureAsset('legacy/common/flashButton.png', 'image/png')
export const pdfAsset = fixtureAsset('legacy/zymatic/Manual.pdf', 'application/pdf', false)
export const baseAsset = fixtureAsset(
  'app/static/support/legacy/pico-s/Base-Recipes.md',
  undefined,
  true,
  'chiefwigms'
)
export const beerAsset = fixtureAsset(
  'app/static/img/beer-styles/amber-ale.png',
  'image/png',
  true,
  'chiefwigms'
)
export const referenceFixtures: CatalogReferences = {
  assets: [guideAsset, nextAsset, imageAsset, pdfAsset, baseAsset, beerAsset],
  items: [guideAsset, nextAsset, imageAsset, pdfAsset, baseAsset].map((asset, i) => ({
    id: `ref-${i}`,
    title: asset.title,
    description: 'Archived guide',
    models: asset.models,
    source_id: asset.source_id,
    asset_path: asset.path,
    url: asset.url,
    content_type: asset.content_type,
    source_url: asset
      .download_url!.replace('raw.githubusercontent.com', 'github.com')
      .replace(`/${commit}/`, `/blob/${commit}/`),
    bundled: asset.bundled,
    available: asset.bundled,
    download_url: asset.download_url,
    sha256: asset.sha256,
  })),
}
export const currentGuide = referenceDocument(guideAsset, referenceFixtures)
