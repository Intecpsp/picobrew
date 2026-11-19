import type { CatalogReferences } from '../types'
import { externalURL } from './programs'

export type ReferenceAsset = CatalogReferences['assets'][number]
export type ReferenceItem = CatalogReferences['items'][number]
export interface ReferenceDocument {
  asset: ReferenceAsset
  reference?: ReferenceItem
  available: boolean
}
export type ReferenceLink =
  | { kind: 'asset'; document: ReferenceDocument; fragment: string }
  | { kind: 'anchor'; fragment: string }
  | { kind: 'external'; url: string }
  | { kind: 'blocked' }

const assetPrefix = '/api/v1/catalog/assets/'
const relativeOrigin = 'https://reference.invalid'
const legacyChiefRevision = '176b9fa2e7245a8504120b50e327b51d96d1cf2b'

function pathValue(value: string): string | undefined {
  try {
    const path = decodeURIComponent(value)
    if (/[\\\u0000-\u001f\u007f]/.test(path)) return undefined
    return path
  } catch {
    return undefined
  }
}

export function referenceDocument(
  asset: ReferenceAsset,
  references: CatalogReferences
): ReferenceDocument {
  const reference = references.items.find((item) => item.asset_path === asset.path)
  return { asset, reference, available: reference?.available ?? asset.bundled }
}

export function referenceLocalURL(document: ReferenceDocument): string | undefined {
  const { asset } = document
  // Only a manifest-listed path can become a local request. Never fetch arbitrary Markdown URLs.
  if (!document.available || !asset.url.startsWith(assetPrefix)) return undefined
  const pathname = pathValue(asset.url.slice(assetPrefix.length))
  return pathname === asset.path &&
    !pathname.split('/').some((part) => part === '.' || part === '..')
    ? asset.url
    : undefined
}

export function referenceSourceURL(document: ReferenceDocument): string | undefined {
  const supplied = externalURL(document.reference?.source_url)
  if (supplied) return supplied
  const raw = externalURL(document.asset.download_url)
  if (!raw) return undefined
  const url = new URL(raw)
  if (url.hostname === 'raw.githubusercontent.com') {
    const [owner, repository, commit, ...parts] = url.pathname.slice(1).split('/')
    if (owner && repository && commit && parts.length)
      return `https://github.com/${owner}/${repository}/blob/${commit}/${parts.join('/')}`
  }
  return raw
}

function sourceFileURL(document: ReferenceDocument, sourcePath: string): string | undefined {
  const source = referenceSourceURL(document)
  if (!source) return undefined
  const url = new URL(source)
  const path = pathValue(url.pathname)
  if (!path || !path.endsWith('/' + document.asset.source_path)) return undefined
  const root = path.slice(0, -document.asset.source_path.length)
  url.pathname = root + sourcePath
  url.hash = ''
  url.search = ''
  return url.href
}

function findAsset(path: string, references: CatalogReferences): ReferenceAsset | undefined {
  return references.assets.find((asset) => asset.path === path)
}

export function resolveReferenceLink(
  value: string | undefined,
  current: ReferenceDocument,
  references: CatalogReferences
): ReferenceLink {
  if (!value || /[\\\u0000-\u001f\u007f]/.test(value)) return { kind: 'blocked' }
  const href = value.trim()
  if (!href) return { kind: 'blocked' }
  if (href.startsWith('#')) return { kind: 'anchor', fragment: href.slice(1) }
  if (href.startsWith('//')) return { kind: 'blocked' }

  if (/^[a-z][a-z\d+.-]*:/i.test(href)) {
    if (/^mailto:/i.test(href)) return { kind: 'external', url: href }
    const safe = externalURL(href)
    if (!safe) return { kind: 'blocked' }
    const target = new URL(safe)
    const fragment = target.hash.slice(1)
    target.hash = ''
    target.search = ''
    if (
      typeof location !== 'undefined' &&
      target.origin === location.origin &&
      target.pathname.startsWith(assetPrefix)
    ) {
      const path = pathValue(target.pathname.slice(assetPrefix.length))
      const asset = path && findAsset(path, references)
      if (asset) return { kind: 'asset', document: referenceDocument(asset, references), fragment }
    }
    const asset = references.assets.find((asset) => {
      const document = referenceDocument(asset, references)
      return [asset.download_url, referenceSourceURL(document)].some((url) => {
        if (!url) return false
        try {
          const candidate = new URL(url)
          candidate.hash = ''
          candidate.search = ''
          return candidate.href === target.href
        } catch {
          return false
        }
      })
    })
    return asset
      ? { kind: 'asset', document: referenceDocument(asset, references), fragment }
      : { kind: 'external', url: safe }
  }

  let target: URL
  try {
    let relative = href
    // The pinned base-recipe guides link to a directory and Hazy filename that
    // do not exist upstream. Resolve these verified historical aliases without
    // rewriting the archived documents or guessing for future source revisions.
    if (
      current.asset.source_id === 'chiefwigms' &&
      current.asset.download_url?.includes(`/${legacyChiefRevision}/`) &&
      relative.startsWith('/static/support/base_recipes/zseries/') &&
      [
        'app/static/support/legacy/zymatic/Base-Recipes.md',
        'app/static/support/z-series/Base-Recipes.md',
      ].includes(current.asset.source_path)
    ) {
      const model = current.asset.source_path.includes('/legacy/zymatic/') ? 'zymatic' : 'z-series'
      relative = relative
        .replace('/base_recipes/zseries/', `/base_recipes/${model}/`)
        .replace('/Base_Recipe_-_Hazy.json', '/Base_Recipe_-_Hazy_IPA_.json')
    }
    // These archived Zymatic links already contain their source-root directory.
    // Resolve them at that root instead of duplicating legacy/zymatic twice.
    const sourceDirectory = current.asset.source_path.slice(
      0,
      current.asset.source_path.lastIndexOf('/') + 1
    )
    if (sourceDirectory && relative.startsWith(sourceDirectory)) relative = '/' + relative
    if (relative.startsWith('/static/') && current.asset.source_id === 'chiefwigms')
      relative = '/chiefwigms/app' + relative
    else if (relative.startsWith('/') && !relative.startsWith(assetPrefix))
      relative = '/' + current.asset.source_id + relative
    target = new URL(relative, `${relativeOrigin}/${current.asset.path}`)
  } catch {
    return { kind: 'blocked' }
  }
  const path = pathValue(target.pathname)
  if (!path) return { kind: 'blocked' }
  const assetPath = path.startsWith(assetPrefix) ? path.slice(assetPrefix.length) : path.slice(1)
  const asset = findAsset(assetPath, references)
  if (asset)
    return {
      kind: 'asset',
      document: referenceDocument(asset, references),
      fragment: target.hash.slice(1),
    }

  // Unbundled firmware ZIPs and recipe JSON links stay on their pinned source,
  // never on an invented local asset endpoint or an unrelated app route.
  const prefix = current.asset.source_id + '/'
  if (
    !assetPath.startsWith(prefix) ||
    assetPath.split('/').some((part) => part === '.' || part === '..')
  )
    return { kind: 'blocked' }
  const source = sourceFileURL(current, assetPath.slice(prefix.length))
  if (!source) return { kind: 'blocked' }
  const url = new URL(source)
  url.hash = target.hash
  return { kind: 'external', url: url.href }
}

export function referenceImageURL(link: ReferenceLink): string | undefined {
  if (link.kind === 'asset' && link.document.asset.content_type.startsWith('image/'))
    return referenceLocalURL(link.document)
  if (link.kind !== 'external') return undefined
  const url = new URL(link.url)
  // The archived video guides use these public thumbnails; the CSP allows the
  // same two hosts. Other remote images retain their alt text without requests.
  if (!['img.youtube.com', 'i.ytimg.com'].includes(url.hostname)) return undefined
  url.protocol = 'https:'
  return url.href
}

export function referenceFormat(asset: ReferenceAsset): 'markdown' | 'image' | 'pdf' | 'text' {
  if (/\.md(?:own)?$/i.test(asset.path)) return 'markdown'
  if (asset.content_type.startsWith('image/')) return 'image'
  if (asset.content_type === 'application/pdf') return 'pdf'
  return 'text'
}
