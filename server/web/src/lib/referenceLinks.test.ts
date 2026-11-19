import { describe, expect, it } from 'vitest'
import {
  baseAsset,
  currentGuide,
  guideAsset,
  imageAsset,
  nextAsset,
  pdfAsset,
  referenceFixtures,
} from '../../tests/reference-fixtures'
import {
  referenceDocument,
  referenceImageURL,
  referenceLocalURL,
  resolveReferenceLink,
} from './referenceLinks'
import { fixtureAsset } from '../../tests/reference-fixtures'

describe('archived guide link resolution', () => {
  it('resolves sibling Markdown and parent-directory images through the complete asset manifest', () => {
    const next = resolveReferenceLink('Firmware.md#versions', currentGuide, referenceFixtures)
    expect(next.kind).toBe('asset')
    if (next.kind === 'asset') {
      expect(next.document.asset.path).toBe(nextAsset.path)
      expect(next.fragment).toBe('versions')
    }
    const image = resolveReferenceLink('../common/flashButton.png', currentGuide, referenceFixtures)
    expect(referenceImageURL(image)).toBe(imageAsset.url)
    expect(resolveReferenceLink(nextAsset.download_url, currentGuide, referenceFixtures).kind).toBe(
      'asset'
    )
  })

  it('resolves the old app /static paths to bundled images and pinned recipe sources', () => {
    const current = referenceDocument(baseAsset, referenceFixtures)
    expect(
      referenceImageURL(
        resolveReferenceLink('/static/img/beer-styles/amber-ale.png', current, referenceFixtures)
      )
    ).toBe('/api/v1/catalog/assets/chiefwigms/app/static/img/beer-styles/amber-ale.png')
    const recipe = resolveReferenceLink(
      '/static/support/base_recipes/pico/Amber.json',
      current,
      referenceFixtures
    )
    expect(recipe.kind).toBe('external')
    if (recipe.kind === 'external')
      expect(recipe.url).toContain('/app/static/support/base_recipes/pico/Amber.json')
  })

  it('keeps optional PDFs in the reader without pretending they are installed', () => {
    const target = resolveReferenceLink('Manual.pdf', currentGuide, referenceFixtures)
    expect(target.kind).toBe('asset')
    if (target.kind === 'asset') {
      expect(target.document.available).toBe(false)
      expect(referenceLocalURL(target.document)).toBeUndefined()
      expect(target.document.asset.path).toBe(pdfAsset.path)
    }
  })

  it('repairs only the verified broken Z and Zymatic recipe links in the pinned guides', () => {
    for (const [folder, expected] of [
      ['legacy/zymatic', 'zymatic'],
      ['z-series', 'z-series'],
    ]) {
      const asset = fixtureAsset(
        `app/static/support/${folder}/Base-Recipes.md`,
        undefined,
        true,
        'chiefwigms'
      )
      asset.download_url = asset.download_url!.replace(
        'a'.repeat(40),
        '176b9fa2e7245a8504120b50e327b51d96d1cf2b'
      )
      const current = referenceDocument(asset, { items: [], assets: [asset] })
      const target = resolveReferenceLink(
        '/static/support/base_recipes/zseries/Base_Recipe_-_Hazy.json',
        current,
        { items: [], assets: [asset] }
      )
      expect(
        target.kind === 'external' &&
          target.url.endsWith(
            `/app/static/support/base_recipes/${expected}/Base_Recipe_-_Hazy_IPA_.json`
          )
      ).toBe(true)
    }
  })

  it('uses pinned external source URLs for unbundled downloads, including the historical root-relative typo', () => {
    for (const href of ['Zymatic_1_1_14.zip', 'legacy/zymatic/Zymatic_1_1_14.zip']) {
      const target = resolveReferenceLink(href, currentGuide, referenceFixtures)
      expect(target.kind).toBe('external')
      if (target.kind === 'external')
        expect(target.url).toMatch(/\/blob\/a{40}\/legacy\/zymatic\/Zymatic_1_1_14.zip$/)
    }
    const target = resolveReferenceLink('../common/PicoFlash.zip', currentGuide, referenceFixtures)
    expect(target.kind === 'external' && target.url.endsWith('/legacy/common/PicoFlash.zip')).toBe(
      true
    )
  })

  it('blocks executable schemes, traversal outside the source and invented local endpoints', () => {
    for (const href of [
      'javascript:alert(1)',
      'data:text/html,hello',
      'blob:https://example.com/1',
      '//evil.example/a',
      '../../../outside.md',
      '..%2f..%2f..%2fsecret',
      'file:///etc/passwd',
      'bad\\path.md',
      'java\nscript:alert(1)',
    ])
      expect(resolveReferenceLink(href, currentGuide, referenceFixtures), href).toEqual({
        kind: 'blocked',
      })
    const tampered = referenceDocument({ ...guideAsset, url: '/api/v1/devices' }, referenceFixtures)
    expect(referenceLocalURL(tampered)).toBeUndefined()
    expect(resolveReferenceLink('#versions', currentGuide, referenceFixtures)).toEqual({
      kind: 'anchor',
      fragment: 'versions',
    })
  })

  it('upgrades only the supported public thumbnail hosts and does not request arbitrary remote images', () => {
    expect(
      referenceImageURL({ kind: 'external', url: 'http://img.youtube.com/vi/abc/0.jpg' })
    ).toBe('https://img.youtube.com/vi/abc/0.jpg')
    expect(
      referenceImageURL({ kind: 'external', url: 'https://tracker.example/image.jpg' })
    ).toBeUndefined()
    expect(referenceImageURL({ kind: 'external', url: 'mailto:info@example.com' })).toBeUndefined()
  })
})
