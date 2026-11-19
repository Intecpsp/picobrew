// @vitest-environment jsdom
import { createElement as h } from 'react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { cleanup, render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import PDFGuide from './PDFGuide'

const pdfjs = vi.hoisted(() => ({ getDocument: vi.fn() }))
vi.mock('pdfjs-dist/legacy/build/pdf.mjs', () => ({
  AnnotationMode: { DISABLE: 0 },
  GlobalWorkerOptions: {},
  getDocument: pdfjs.getDocument,
  version: '6.5.438',
}))

beforeEach(() => {
  vi.spyOn(HTMLElement.prototype, 'getBoundingClientRect').mockReturnValue({
    x: 0,
    y: 0,
    width: 320,
    height: 480,
    top: 0,
    left: 0,
    right: 320,
    bottom: 480,
    toJSON: () => ({}),
  })
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response('%PDF-1.4')))
})
afterEach(() => {
  cleanup()
  vi.restoreAllMocks()
  vi.unstubAllGlobals()
  pdfjs.getDocument.mockReset()
})

const properties = {
  url: '/api/v1/catalog/assets/support/Manual.pdf',
  title: 'Manual',
  size: 8,
  fragment: '',
  source: h('a', { href: 'https://example.com/manual.pdf' }, 'Open archived PDF'),
}
function fakeDocument(renderPromise: Promise<void> = Promise.resolve()) {
  const cancel = vi.fn()
  const renderPage = vi.fn(() => ({ promise: renderPromise, cancel }))
  const cleanupPage = vi.fn()
  const document = {
    numPages: 2,
    getPage: vi.fn(async (page: number) => ({
      getViewport: ({ scale }: { scale: number }) => ({ width: 200 * scale, height: 300 * scale }),
      render: renderPage,
      getTextContent: async () => ({ items: [{ str: `Page ${page} instructions` }] }),
      cleanup: cleanupPage,
    })),
  }
  const destroy = vi.fn(async () => {})
  pdfjs.getDocument.mockReturnValue({ promise: Promise.resolve(document), destroy })
  return { document, cancel, renderPage, cleanupPage, destroy }
}

describe('PDF page reader', () => {
  it('renders one fitted page, exposes page text and navigates without refetching the PDF', async () => {
    const lifecycle = fakeDocument()
    const user = userEvent.setup()
    const view = render(h(PDFGuide, properties))
    const canvas = await screen.findByRole('img', { name: 'Manual, page 1' })
    expect(canvas.getAttribute('data-rendered-page')).toBe('1')
    expect(canvas.getAttribute('width')).toBe('320')
    expect(screen.getByText('Page 1 of 2')).toBeTruthy()
    await user.click(screen.getByText('Page text', { exact: true }))
    expect(screen.getByText('Page 1 instructions')).toBeTruthy()
    await user.click(screen.getByRole('button', { name: 'Next' }))
    await screen.findByRole('img', { name: 'Manual, page 2' })
    expect(screen.getByRole('button', { name: 'Next' }).hasAttribute('disabled')).toBe(true)
    expect(fetch).toHaveBeenCalledOnce()
    expect(pdfjs.getDocument.mock.calls[0][0]).toMatchObject({
      cMapUrl: '/vendor/pdfjs-6.5.438/cmaps/',
      standardFontDataUrl: '/vendor/pdfjs-6.5.438/standard_fonts/',
      useWasm: false,
      disableFontFace: true,
    })
    view.unmount()
    expect(lifecycle.destroy).toHaveBeenCalledOnce()
    expect(lifecycle.cleanupPage).toHaveBeenCalled()
  })

  it('cancels unfinished rendering and the authenticated byte request on close', async () => {
    const lifecycle = fakeDocument(new Promise(() => {}))
    const view = render(h(PDFGuide, properties))
    await waitFor(() => expect(lifecycle.renderPage).toHaveBeenCalledOnce())
    expect(screen.queryByRole('img', { name: 'Manual, page 1' })).toBeNull()
    const request = vi.mocked(fetch).mock.calls[0][1]!
    expect(request.credentials).toBe('same-origin')
    view.unmount()
    expect(request.signal?.aborted).toBe(true)
    expect(lifecycle.cancel).toHaveBeenCalledOnce()
    expect(lifecycle.destroy).toHaveBeenCalledOnce()
  })

  it('cancels a superseded page and does not let its late result replace the current page', async () => {
    let finishOld!: () => void
    const lifecycle = fakeDocument(new Promise((resolve) => (finishOld = resolve)))
    render(h(PDFGuide, properties))
    await waitFor(() => expect(lifecycle.renderPage).toHaveBeenCalledOnce())
    lifecycle.renderPage.mockReturnValue({ promise: Promise.resolve(), cancel: vi.fn() })
    await userEvent.setup().click(screen.getByRole('button', { name: 'Next' }))
    await screen.findByRole('img', { name: 'Manual, page 2' })
    expect(lifecycle.cancel).toHaveBeenCalledOnce()
    finishOld()
    await waitFor(() => expect(lifecycle.cleanupPage).toHaveBeenCalledTimes(2))
    expect(
      screen.getByRole('img', { name: 'Manual, page 2' }).getAttribute('data-rendered-page')
    ).toBe('2')
    expect(screen.queryByText('Page 1 instructions')).toBeNull()
  })

  it('shows a recoverable error and retains original-source access when parsing fails', async () => {
    const failed = {
      promise: Promise.reject(new Error('Invalid PDF structure.')),
      destroy: vi.fn(),
    }
    pdfjs.getDocument.mockReturnValue(failed)
    render(h(PDFGuide, properties))
    await screen.findByText('Invalid PDF structure.')
    expect(screen.getByRole('link', { name: 'Open archived PDF' })).toBeTruthy()
    fakeDocument()
    vi.mocked(fetch).mockResolvedValue(new Response('%PDF-1.4'))
    await userEvent.setup().click(screen.getByRole('button', { name: 'Try again' }))
    await screen.findByRole('img', { name: 'Manual, page 1' })
    expect(failed.destroy).toHaveBeenCalledOnce()
  })
})
