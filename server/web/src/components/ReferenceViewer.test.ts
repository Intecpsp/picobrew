// @vitest-environment jsdom
import { createElement as h, useState } from 'react'
import { afterAll, afterEach, beforeAll, describe, expect, it, vi } from 'vitest'
import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import {
  currentGuide,
  guideAsset,
  imageAsset,
  nextAsset,
  pdfAsset,
  referenceFixtures,
} from '../../tests/reference-fixtures'
import { referenceDocument } from '../lib/referenceLinks'
import {
  ReferenceContent,
  ReferenceLibrary,
  ReferenceMarkdown,
  scrollToGuideAnchor,
} from './ReferenceViewer'

const showModalDescriptor = Object.getOwnPropertyDescriptor(
  HTMLDialogElement.prototype,
  'showModal'
)
beforeAll(() => {
  Object.defineProperty(HTMLDialogElement.prototype, 'showModal', {
    configurable: true,
    value(this: HTMLDialogElement) {
      this.open = true
    },
  })
})
afterAll(() => {
  if (showModalDescriptor)
    Object.defineProperty(HTMLDialogElement.prototype, 'showModal', showModalDescriptor)
  else Reflect.deleteProperty(HTMLDialogElement.prototype, 'showModal')
})
afterEach(() => {
  cleanup()
  vi.restoreAllMocks()
  vi.unstubAllGlobals()
})

describe('safe guide rendering', () => {
  it('renders Markdown structure, GFM tables and legacy HTML images without raw HTML execution', () => {
    const text = `# Guide

## Instructions

1. **Connect** the brewer.
2. Open \`firmware.hex\`.

| Part | Count |
| --- | ---: |
| Cable | 1 |

\`\`\`sh
echo ready
\`\`\`

<div markdown="1">

<img src="../common/flashButton.png" alt="Flash button" width="100" onerror="window.__unsafe = true" />

### Legacy section

</div>

<script>window.__unsafe = true</script>
<iframe src="https://evil.example/"></iframe>
<img src="data:image/svg+xml,evil" alt="Blocked image" />
<a href="javascript:alert(1)" onclick="alert(1)">Unsafe link</a>
<a href="https://example.com/" onclick="alert(1)">Safe external</a>`
    const { container } = render(
      h(ReferenceMarkdown, {
        text,
        current: currentGuide,
        references: referenceFixtures,
        onNavigate: vi.fn(),
        onAnchor: vi.fn(),
      })
    )
    expect(screen.getByRole('heading', { name: 'Guide', level: 1 })).toBeTruthy()
    expect(screen.getByRole('heading', { name: 'Legacy section', level: 3 })).toBeTruthy()
    expect(screen.getByRole('list').querySelectorAll('li')).toHaveLength(2)
    expect(screen.getByRole('table').textContent).toContain('Cable')
    expect(container.querySelector('pre code')?.textContent).toBe('echo ready\n')
    expect(screen.getByRole('img', { name: 'Flash button' }).getAttribute('src')).toBe(
      imageAsset.url
    )
    expect(
      container.querySelector('script, iframe, object, style, [onclick], [onerror]')
    ).toBeNull()
    expect(screen.queryByRole('link', { name: 'Unsafe link' })).toBeNull()
    expect(screen.queryByRole('img', { name: 'Blocked image' })).toBeNull()
    expect(screen.getByRole('link', { name: 'Safe external' }).getAttribute('rel')).toBe(
      'noopener noreferrer'
    )
    expect((window as unknown as { __unsafe?: boolean }).__unsafe).toBeUndefined()
  })

  it('routes internal links and fragments inside the reader without changing the app URL', async () => {
    const user = userEvent.setup()
    const navigate = vi.fn(),
      anchor = vi.fn()
    window.location.hash = '#/catalog?q=stout'
    const { container } = render(
      h(ReferenceMarkdown, {
        text: '# Steps\n\n[Next guide](Firmware.md#versions)\n\n[Jump](#steps)\n\n[Diagram](../common/flashButton.png)',
        current: currentGuide,
        references: referenceFixtures,
        onNavigate: navigate,
        onAnchor: anchor,
      })
    )
    await user.click(screen.getByRole('link', { name: 'Next guide' }))
    expect(navigate.mock.calls[0][0].asset.path).toBe(nextAsset.path)
    expect(navigate.mock.calls[0][1]).toBe('versions')
    await user.click(screen.getByRole('link', { name: 'Diagram' }))
    expect(navigate.mock.calls[1][0].asset.path).toBe(imageAsset.path)
    await user.click(screen.getByRole('link', { name: 'Jump' }))
    expect(anchor).toHaveBeenCalledWith('steps')
    scrollToGuideAnchor(container, 'steps')
    expect(document.activeElement).toBe(screen.getByRole('heading', { name: 'Steps' }))
    expect(window.location.hash).toBe('#/catalog?q=stout')
  })

  it('loads Markdown through the authenticated local API and can retry a failed fetch', async () => {
    const fetchMock = vi
      .fn()
      .mockResolvedValueOnce(
        new Response(JSON.stringify({ error: { message: 'Temporary failure' } }), { status: 503 })
      )
      .mockResolvedValueOnce(new Response('# Recovered guide'))
    vi.stubGlobal('fetch', fetchMock)
    render(
      h(ReferenceContent, {
        document: currentGuide,
        references: referenceFixtures,
        onNavigate: vi.fn(),
      })
    )
    await screen.findByText('Temporary failure')
    await userEvent.setup().click(screen.getByRole('button', { name: 'Try again' }))
    await screen.findByRole('heading', { name: 'Recovered guide' })
    expect(fetchMock.mock.calls[0][0]).toBe(guideAsset.url)
    expect(fetchMock.mock.calls[0][1].credentials).toBe('same-origin')
    expect(fetchMock.mock.calls[0][1].signal).toBeInstanceOf(AbortSignal)
  })

  it('ignores an old guide response after navigation', async () => {
    let resolveOld!: (response: Response) => void
    const fetchMock = vi
      .fn()
      .mockImplementationOnce(
        () =>
          new Promise<Response>((resolve) => {
            resolveOld = resolve
          })
      )
      .mockResolvedValueOnce(new Response('# New guide'))
    vi.stubGlobal('fetch', fetchMock)
    const view = render(
      h(ReferenceContent, {
        document: currentGuide,
        references: referenceFixtures,
        onNavigate: vi.fn(),
      })
    )
    view.rerender(
      h(ReferenceContent, {
        document: referenceDocument(nextAsset, referenceFixtures),
        references: referenceFixtures,
        onNavigate: vi.fn(),
      })
    )
    await screen.findByRole('heading', { name: 'New guide' })
    resolveOld(new Response('# Obsolete guide'))
    await waitFor(() => expect(fetchMock.mock.calls[0][1].signal.aborted).toBe(true))
    expect(screen.queryByRole('heading', { name: 'Obsolete guide' })).toBeNull()
  })

  it('offers an external source for unavailable optional PDFs without a broken local preview', () => {
    const fetchMock = vi.fn()
    vi.stubGlobal('fetch', fetchMock)
    const { container } = render(
      h(ReferenceContent, {
        document: referenceDocument(pdfAsset, referenceFixtures),
        references: referenceFixtures,
        onNavigate: vi.fn(),
      })
    )
    expect(screen.getByText('This guide isn’t installed on this Hub.')).toBeTruthy()
    expect(screen.getByRole('link', { name: 'Open archived PDF' }).getAttribute('href')).toBe(
      pdfAsset.download_url
    )
    expect(container.querySelector('iframe')).toBeNull()
    expect(fetchMock).not.toHaveBeenCalled()
  })

  it('previews installed PDFs in a titled sandbox and revokes the blob on close', async () => {
    const create = vi.fn((_blob: Blob) => 'blob:http://localhost/guide-pdf'),
      revoke = vi.fn()
    vi.stubGlobal(
      'URL',
      class extends URL {
        static createObjectURL = create
        static revokeObjectURL = revoke
      }
    )
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response('%PDF-1.4\n%%EOF')))
    const document = { ...referenceDocument(pdfAsset, referenceFixtures), available: true }
    const view = render(
      h(ReferenceContent, { document, references: referenceFixtures, onNavigate: vi.fn() })
    )
    const frame = await screen.findByTitle('Manual.pdf')
    expect(frame.tagName).toBe('IFRAME')
    expect(frame.getAttribute('sandbox')).toBe('allow-scripts allow-same-origin')
    expect(frame.getAttribute('src')).toBe('blob:http://localhost/guide-pdf')
    expect(create.mock.calls[0][0].type).toBe('application/pdf')
    expect(screen.getByRole('link', { name: 'Open archived PDF' })).toBeTruthy()
    view.unmount()
    expect(revoke).toHaveBeenCalledWith('blob:http://localhost/guide-pdf')
  })

  it('opens an image in the reader and keeps its attribution under one disclosure', () => {
    const { container } = render(
      h(ReferenceContent, {
        document: referenceDocument(imageAsset, referenceFixtures),
        references: referenceFixtures,
        onNavigate: vi.fn(),
      })
    )
    expect(screen.getByRole('img', { name: 'flashButton.png' }).getAttribute('src')).toBe(
      imageAsset.url
    )
    const credits = container.querySelector('details')!
    expect(credits.open).toBe(false)
    expect(credits.querySelector('summary')?.textContent).toBe('Sources & credits')
    expect(credits.textContent).toContain('Original guide contributor')
    expect(credits.textContent).toContain('Original documentation notice')
  })

  it('keeps guide navigation in one dialog, restores the list focus and handles modal cancellation', async () => {
    const user = userEvent.setup()
    vi.stubGlobal(
      'fetch',
      vi.fn(async (path: string) =>
        path.endsWith('/catalog/references')
          ? new Response(JSON.stringify(referenceFixtures))
          : new Response(
              path === guideAsset.url ? '# Installation\n\n[Next guide](Firmware.md)' : '# Versions'
            )
      )
    )
    function Harness() {
      const [open, setOpen] = useState(false)
      return h(
        'div',
        null,
        h('button', { onClick: () => setOpen(true) }, 'Machine guides'),
        open ? h(ReferenceLibrary, { model: 'zymatic', onClose: () => setOpen(false) }) : null
      )
    }
    render(h(Harness))
    const opener = screen.getByRole('button', { name: 'Machine guides' })
    await user.click(opener)
    const title = await screen.findByText('Firmware-Installation.md', { selector: 'strong' })
    await user.click(within(title.closest('article')!).getByRole('button', { name: 'Open guide' }))
    await screen.findByRole('heading', { name: 'Installation' })
    await user.click(screen.getByRole('link', { name: 'Next guide' }))
    await screen.findByRole('heading', { name: 'Versions' })
    expect(screen.getAllByRole('dialog')).toHaveLength(1)
    await user.click(screen.getByRole('button', { name: 'Back to previous guide' }))
    await screen.findByRole('heading', { name: 'Installation' })
    await user.click(screen.getByRole('button', { name: 'Back to guides' }))
    const restoredTitle = await screen.findByText('Firmware-Installation.md', {
      selector: 'strong',
    })
    expect(document.activeElement).toBe(
      within(restoredTitle.closest('article')!).getByRole('button', { name: 'Open guide' })
    )
    fireEvent(screen.getByRole('dialog'), new Event('cancel', { cancelable: true }))
    expect(screen.queryByRole('dialog')).toBeNull()
    expect(document.activeElement).toBe(opener)
  })
})
