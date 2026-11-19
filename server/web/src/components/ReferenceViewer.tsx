import { useEffect, useRef, useState } from 'react'
import Markdown from 'react-markdown'
import remarkGfm from 'remark-gfm'
import rehypeRaw from 'rehype-raw'
import rehypeSanitize from 'rehype-sanitize'
import { ArrowDownToLine, ArrowLeft, ArrowRight, ExternalLink } from 'lucide-react'
import { useResource } from '../context'
import { apiResponse, download, errorMessage } from '../lib/api'
import { externalURL } from '../lib/programs'
import {
  referenceDocument,
  referenceFormat,
  referenceImageURL,
  referenceLocalURL,
  referenceSourceURL,
  resolveReferenceLink,
  type ReferenceAsset,
  type ReferenceDocument,
} from '../lib/referenceLinks'
import type { CatalogReferences } from '../types'
import { Button, ErrorBanner, Loading, Modal } from './ui'
import './ReferenceViewer.scss'

interface HeadingNode {
  type: string
  tagName?: string
  value?: string
  properties?: Record<string, unknown>
  children?: HeadingNode[]
}

// Generate scoped heading anchors after sanitization. No source HTML IDs become
// app-global properties, and fragment clicks never change the app's hash route.
function guideAnchors() {
  return (tree: HeadingNode) => {
    const counts = new Map<string, number>()
    const text = (node: HeadingNode): string =>
      node.value || (node.children || []).map(text).join('')
    const visit = (node: HeadingNode) => {
      if (node.type === 'element' && /^h[1-6]$/.test(node.tagName || '')) {
        const base =
          text(node)
            .trim()
            .toLowerCase()
            .replace(/[^\p{L}\p{N}\p{M}\s_-]/gu, '')
            .replace(/\s/g, '-') || 'section'
        const count = counts.get(base) || 0
        counts.set(base, count + 1)
        const slug = count ? `${base}-${count}` : base
        node.properties = {
          ...node.properties,
          id: `guide-heading-${slug}`,
          dataGuideAnchor: slug,
          tabIndex: -1,
        }
      }
      node.children?.forEach(visit)
    }
    visit(tree)
  }
}

export function scrollToGuideAnchor(container: HTMLElement | null, fragment: string) {
  if (!container) return
  let decoded = fragment
  try {
    decoded = decodeURIComponent(fragment)
  } catch {
    /* Preserve malformed literal fragments. */
  }
  const target = Array.from(
    container.querySelectorAll<HTMLElement>('[data-guide-anchor], [id]')
  ).find((element) => element.dataset.guideAnchor === decoded || element.id === decoded)
  if (target) {
    target.scrollIntoView?.({ block: 'start' })
    target.focus({ preventScroll: true })
  }
}

function GuideImage({
  src,
  alt,
  title,
  width,
  current,
  references,
}: {
  src?: string
  alt?: string
  title?: string
  width?: number | string
  current: ReferenceDocument
  references: CatalogReferences
}) {
  const url = referenceImageURL(resolveReferenceLink(src, current, references))
  const [failed, setFailed] = useState(false)
  useEffect(() => setFailed(false), [url])
  if (!url || failed)
    return <span className="reference-image-fallback">{alt || 'Image unavailable'}</span>
  const size = Number(width)
  return (
    <img
      src={url}
      alt={alt || ''}
      title={title}
      width={size > 0 && size <= 1200 ? size : undefined}
      loading="lazy"
      referrerPolicy="no-referrer"
      onError={() => setFailed(true)}
    />
  )
}

export function ReferenceMarkdown({
  text,
  current,
  references,
  onNavigate,
  onAnchor,
}: {
  text: string
  current: ReferenceDocument
  references: CatalogReferences
  onNavigate: (document: ReferenceDocument, fragment?: string) => void
  onAnchor: (fragment: string) => void
}) {
  return (
    <Markdown
      remarkPlugins={[remarkGfm]}
      rehypePlugins={[rehypeRaw, rehypeSanitize, guideAnchors]}
      components={{
        a: ({ href, children, title }) => {
          const link = resolveReferenceLink(href, current, references)
          if (link.kind === 'blocked')
            return <span className="reference-unavailable-link">{children}</span>
          if (link.kind === 'anchor')
            return (
              <a
                href={`#${link.fragment}`}
                title={title}
                onClick={(event) => {
                  event.preventDefault()
                  onAnchor(link.fragment)
                }}
              >
                {children}
              </a>
            )
          if (link.kind === 'asset')
            return (
              <a
                href={link.document.asset.url + (link.fragment ? '#' + link.fragment : '')}
                title={title}
                onClick={(event) => {
                  event.preventDefault()
                  onNavigate(link.document, link.fragment)
                }}
              >
                {children}
              </a>
            )
          return (
            <a href={link.url} target="_blank" rel="noopener noreferrer" title={title}>
              {children}
            </a>
          )
        },
        img: ({ src, alt, title, width }) => (
          <GuideImage
            src={src}
            alt={alt}
            title={title}
            width={width}
            current={current}
            references={references}
          />
        ),
        table: ({ children }) => (
          <div className="reference-table-scroll">
            <table>{children}</table>
          </div>
        ),
      }}
    >
      {text}
    </Markdown>
  )
}

function ReferenceCredits({ document }: { document: ReferenceDocument }) {
  const source = referenceSourceURL(document)
  const { asset } = document
  return (
    <details className="reference-credits">
      <summary>Sources & credits</summary>
      <dl>
        <div>
          <dt>Attribution</dt>
          <dd>{asset.attribution}</dd>
        </div>
        <div>
          <dt>License</dt>
          <dd>{asset.license}</dd>
        </div>
        <div>
          <dt>Archived file</dt>
          <dd>{asset.source_path}</dd>
        </div>
        <div>
          <dt>SHA-256</dt>
          <dd>
            <code>{asset.sha256}</code>
          </dd>
        </div>
      </dl>
      {source && (
        <a href={source} target="_blank" rel="noopener noreferrer">
          View archived source <ExternalLink size={14} />
        </a>
      )}
    </details>
  )
}

function ArchiveFallback({ document }: { document: ReferenceDocument }) {
  const url =
    externalURL(document.asset.download_url || document.reference?.download_url) ||
    referenceSourceURL(document)
  return url ? (
    <a className="reference-source-link" href={url} target="_blank" rel="noopener noreferrer">
      {referenceFormat(document.asset) === 'pdf' ? 'Open archived PDF' : 'Open archived file'}{' '}
      <ExternalLink size={16} />
    </a>
  ) : null
}

export function ReferenceContent({
  document,
  references,
  fragment = '',
  onNavigate,
}: {
  document: ReferenceDocument
  references: CatalogReferences
  fragment?: string
  onNavigate: (document: ReferenceDocument, fragment?: string) => void
}) {
  const localURL = referenceLocalURL(document)
  const format = referenceFormat(document.asset)
  const [text, setText] = useState('')
  const [pdfURL, setPDFURL] = useState('')
  const [loading, setLoading] = useState(Boolean(localURL && format !== 'image'))
  const [error, setError] = useState('')
  const [attempt, setAttempt] = useState(0)
  const content = useRef<HTMLDivElement>(null)
  useEffect(() => {
    const controller = new AbortController()
    let objectURL = ''
    setError('')
    setText('')
    setPDFURL('')
    if (!localURL || format === 'image') {
      setLoading(false)
      return () => controller.abort()
    }
    setLoading(true)
    void apiResponse(localURL, { signal: controller.signal, accept: document.asset.content_type })
      .then(async (response) => {
        if (format === 'pdf') {
          const bytes = await response.arrayBuffer()
          if (controller.signal.aborted) return
          objectURL = URL.createObjectURL(new Blob([bytes], { type: 'application/pdf' }))
          setPDFURL(objectURL)
        } else {
          const value = await response.text()
          if (!controller.signal.aborted) setText(value)
        }
      })
      .catch((error) => {
        if (!controller.signal.aborted) setError(errorMessage(error))
      })
      .finally(() => {
        if (!controller.signal.aborted) setLoading(false)
      })
    return () => {
      controller.abort()
      if (objectURL) URL.revokeObjectURL(objectURL)
    }
  }, [localURL, format, document.asset.content_type, attempt])
  useEffect(() => {
    if (loading) return
    content.current?.closest('dialog')?.scrollTo?.({ top: 0 })
    content.current?.focus({ preventScroll: true })
    if (fragment) scrollToGuideAnchor(content.current, fragment)
  }, [loading, text, fragment])

  async function downloadLocal() {
    if (!localURL) return
    try {
      await download(localURL, document.asset.source_path.split('/').pop() || 'guide')
    } catch (error) {
      setError(errorMessage(error))
    }
  }
  return (
    <div className="reference-document">
      <div className="reference-document-actions">
        {localURL && (
          <Button variant="secondary" onClick={() => void downloadLocal()}>
            <ArrowDownToLine size={16} />
            Download original
          </Button>
        )}
      </div>
      <div
        ref={content}
        tabIndex={-1}
        className="reference-document-content"
        aria-label={document.asset.title}
      >
        {!localURL ? (
          <div className="reference-fallback" role="status">
            <h3>This guide isn’t installed on this Hub.</h3>
            <p>You can open the archived source. An internet connection is needed.</p>
            <ArchiveFallback document={document} />
          </div>
        ) : loading ? (
          <Loading label="Opening the guide…" />
        ) : error ? (
          <div className="reference-fallback">
            <ErrorBanner message={error} retry={() => setAttempt((value) => value + 1)} />
            <ArchiveFallback document={document} />
          </div>
        ) : format === 'markdown' ? (
          <article className="reference-markdown">
            <ReferenceMarkdown
              text={text}
              current={document}
              references={references}
              onNavigate={onNavigate}
              onAnchor={(value) => scrollToGuideAnchor(content.current, value)}
            />
          </article>
        ) : format === 'image' ? (
          <figure className="reference-image-preview">
            <img
              src={localURL}
              alt={document.asset.title}
              referrerPolicy="no-referrer"
              onError={() => setError('This image could not be loaded.')}
            />
          </figure>
        ) : format === 'pdf' && pdfURL ? (
          <div className="reference-pdf-preview">
            <iframe
              title={document.asset.title}
              src={pdfURL + (fragment ? '#' + fragment : '')}
              sandbox="allow-scripts allow-same-origin"
              referrerPolicy="no-referrer"
            />
            <p>
              If your browser cannot display this PDF, use{' '}
              <button onClick={() => void downloadLocal()}>Download original</button> or{' '}
              <ArchiveFallback document={document} />.
            </p>
          </div>
        ) : (
          <pre className="reference-text">{text}</pre>
        )}
      </div>
      <ReferenceCredits document={document} />
    </div>
  )
}

const modelNames: Record<string, string> = {
  'pico-s': 'Pico S',
  'pico-pro': 'Pico Pro',
  'pico-c': 'Pico C',
  z: 'Pico Z',
  zymatic: 'Zymatic',
  picoferm: 'PicoFerm',
  picostill: 'PicoStill',
  kegsmarts: 'KegSmarts',
}

export function ReferenceLibrary({ model, onClose }: { model: string; onClose: () => void }) {
  // Keep the complete manifest while filtering the list, so cross-machine guide
  // links and common images still resolve to their bundled files.
  const resource = useResource<CatalogReferences>('/catalog/references')
  const [trail, setTrail] = useState<{ document: ReferenceDocument; fragment?: string }[]>([])
  const list = useRef<HTMLDivElement>(null)
  const lastOpenedPath = useRef('')
  const selected = trail[trail.length - 1]
  function navigate(document: ReferenceDocument, fragment = '') {
    setTrail((previous) => [...previous, { document, fragment }])
  }
  function open(asset: ReferenceAsset) {
    if (!resource.data) return
    lastOpenedPath.current = asset.path
    navigate(referenceDocument(asset, resource.data))
  }
  useEffect(() => {
    if (!selected && lastOpenedPath.current)
      Array.from(list.current?.querySelectorAll<HTMLButtonElement>('[data-reference-path]') || [])
        .find((button) => button.dataset.referencePath === lastOpenedPath.current)
        ?.focus()
  }, [selected])
  const images =
    resource.data?.assets.filter(
      (asset) =>
        asset.bundled &&
        asset.content_type.startsWith('image/') &&
        (asset.kind.includes('machine') || asset.kind.includes('device')) &&
        (!model || !asset.models.length || asset.models.includes(model))
    ) || []
  const items =
    resource.data?.items.filter(
      (item) => !model || !item.models.length || item.models.includes(model)
    ) || []
  return (
    <Modal
      title={selected?.document.asset.title || 'Machine guides & artwork'}
      onClose={onClose}
      wide
    >
      {selected && resource.data ? (
        <>
          <Button variant="ghost" onClick={() => setTrail((previous) => previous.slice(0, -1))}>
            <ArrowLeft size={16} />
            {trail.length > 1 ? 'Back to previous guide' : 'Back to guides'}
          </Button>
          <ReferenceContent
            key={selected.document.asset.path}
            document={selected.document}
            references={resource.data}
            fragment={selected.fragment}
            onNavigate={navigate}
          />
        </>
      ) : (
        <div ref={list} className="reference-library">
          <p>
            Read bundled guides here. Manuals that aren’t installed link to their archived source.
          </p>
          <ErrorBanner message={resource.error} retry={resource.reload} />
          {resource.loading ? (
            <Loading />
          ) : (
            <>
              <div className="machine-artwork">
                {images.map((asset) => (
                  <button
                    className="reference-artwork-button"
                    key={asset.path}
                    data-reference-path={asset.path}
                    onClick={() => open(asset)}
                  >
                    <img src={referenceLocalURL(referenceDocument(asset, resource.data!))} alt="" />
                    <span>{asset.title}</span>
                  </button>
                ))}
              </div>
              <div className="reference-list">
                {items.map((item) => {
                  const asset = resource.data?.assets.find(
                    (asset) => asset.path === item.asset_path
                  )
                  return (
                    <article key={item.id}>
                      <div>
                        <strong>{item.title}</strong>
                        <p>{item.description}</p>
                        <small>
                          {item.models.map((model) => modelNames[model] || model).join(' · ')}
                        </small>
                      </div>
                      {asset ? (
                        <Button
                          variant="ghost"
                          data-reference-path={asset.path}
                          onClick={() => open(asset)}
                        >
                          {item.available ? 'Open guide' : 'View options'}
                          <ArrowRight size={16} />
                        </Button>
                      ) : (
                        <span className="muted">Unavailable</span>
                      )}
                    </article>
                  )
                })}
              </div>
            </>
          )}
        </div>
      )}
    </Modal>
  )
}
