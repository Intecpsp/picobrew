import { useEffect, useRef, useState, type ReactNode } from 'react'
import {
  AnnotationMode,
  GlobalWorkerOptions,
  getDocument,
  version,
  type PDFDocumentLoadingTask,
  type PDFDocumentProxy,
  type RenderTask,
} from 'pdfjs-dist/legacy/build/pdf.mjs'
import workerURL from 'pdfjs-dist/legacy/build/pdf.worker.min.mjs?url'
import { ArrowLeft, ArrowRight } from 'lucide-react'
import { apiResponse, errorMessage } from '../lib/api'
import {
  MAX_PDF_BYTES,
  MAX_PDF_CANVAS_PIXELS,
  pdfPageFromFragment,
  readPDFBytes,
} from '../lib/pdfPreview'
import { Button, ErrorBanner, Loading } from './ui'

GlobalWorkerOptions.workerSrc = workerURL
const resources = `/vendor/pdfjs-${version}/`

export default function PDFGuide({
  url,
  title,
  size,
  fragment,
  source,
}: {
  url: string
  title: string
  size: number
  fragment: string
  source: ReactNode
}) {
  const [pdf, setPDF] = useState<PDFDocumentProxy | null>(null)
  const [pageNumber, setPageNumber] = useState(1)
  const [readyPage, setReadyPage] = useState(0)
  const [pageText, setPageText] = useState('')
  const [error, setError] = useState('')
  const [attempt, setAttempt] = useState(0)
  const [width, setWidth] = useState(0)
  const host = useRef<HTMLDivElement>(null)
  const canvas = useRef<HTMLCanvasElement>(null)

  useEffect(() => {
    const controller = new AbortController()
    let loadingTask: PDFDocumentLoadingTask | undefined
    let closed = false
    setPDF(null)
    setError('')
    setReadyPage(0)
    setPageText('')
    const timeout = setTimeout(() => {
      if (closed) return
      controller.abort()
      void loadingTask?.destroy()
      setError('The PDF took too long to open. Try again or download the original.')
    }, 30000)
    void (async () => {
      if (size > MAX_PDF_BYTES)
        throw new Error('This PDF is too large to preview. Download the original to read it.')
      const response = await apiResponse(url, {
        signal: controller.signal,
        accept: 'application/pdf',
      })
      const bytes = await readPDFBytes(response, controller.signal)
      controller.signal.throwIfAborted()
      loadingTask = getDocument({
        data: bytes,
        cMapUrl: resources + 'cmaps/',
        standardFontDataUrl: resources + 'standard_fonts/',
        wasmUrl: resources + 'wasm/',
        useWorkerFetch: true,
        useWasm: false,
        disableFontFace: true,
        disableWorkerRendering: true,
        enableXfa: false,
        maxImageSize: 16 * 1024 * 1024,
        canvasMaxAreaInBytes: MAX_PDF_CANVAS_PIXELS * 4,
      })
      const document = await loadingTask.promise
      if (closed || controller.signal.aborted) return
      setPageNumber(pdfPageFromFragment(fragment, document.numPages))
      setPDF(document)
    })()
      .catch((error) => {
        if (!closed && !controller.signal.aborted) setError(errorMessage(error))
      })
      .finally(() => clearTimeout(timeout))
    return () => {
      closed = true
      clearTimeout(timeout)
      controller.abort()
      void loadingTask?.destroy()
    }
  }, [url, size, fragment, attempt])

  useEffect(() => {
    const element = host.current
    if (!element) return
    const measure = () => setWidth(Math.max(0, Math.floor(element.getBoundingClientRect().width)))
    measure()
    if (typeof ResizeObserver === 'undefined') {
      window.addEventListener('resize', measure)
      return () => window.removeEventListener('resize', measure)
    }
    const observer = new ResizeObserver(measure)
    observer.observe(element)
    return () => observer.disconnect()
  }, [])

  useEffect(() => {
    if (!pdf || !width || !canvas.current) return
    let closed = false
    let timedOut = false
    let rendering: RenderTask | undefined
    setReadyPage(0)
    setPageText('')
    setError('')
    const timeout = setTimeout(() => {
      if (closed) return
      timedOut = true
      rendering?.cancel()
      void pdf.loadingTask.destroy()
      setError('This page took too long to render. Try again or download the original.')
    }, 30000)
    void (async () => {
      const page = await pdf.getPage(pageNumber)
      if (closed) return
      try {
        const normal = page.getViewport({ scale: 1 })
        const viewport = page.getViewport({ scale: Math.min(width / normal.width, 2) })
        const ratio = Math.min(
          window.devicePixelRatio || 1,
          2,
          Math.sqrt(MAX_PDF_CANVAS_PIXELS / (viewport.width * viewport.height)),
          16384 / Math.max(viewport.width, viewport.height)
        )
        const target = canvas.current!
        target.width = Math.max(1, Math.floor(viewport.width * ratio))
        target.height = Math.max(1, Math.floor(viewport.height * ratio))
        rendering = page.render({
          canvas: target,
          viewport,
          transform: ratio === 1 ? undefined : [ratio, 0, 0, ratio, 0, 0],
          background: '#ffffff',
          annotationMode: AnnotationMode.DISABLE,
        })
        const [, text] = await Promise.all([rendering.promise, page.getTextContent()])
        if (closed) return
        setPageText(text.items.map((item) => ('str' in item ? item.str : '')).join(' '))
        setReadyPage(pageNumber)
      } finally {
        page.cleanup()
      }
    })()
      .catch((error) => {
        if (!closed && !timedOut) setError(errorMessage(error))
      })
      .finally(() => clearTimeout(timeout))
    return () => {
      closed = true
      clearTimeout(timeout)
      rendering?.cancel()
    }
  }, [pdf, pageNumber, width])

  return (
    <section className="reference-pdf-preview" aria-label={`${title} PDF reader`}>
      {pdf && (
        <div className="reference-pdf-navigation" aria-label="PDF pages">
          <Button
            variant="secondary"
            disabled={Boolean(error) || pageNumber <= 1}
            onClick={() => setPageNumber((page) => page - 1)}
          >
            <ArrowLeft size={16} /> Previous
          </Button>
          <span aria-live="polite">
            Page {pageNumber} of {pdf.numPages}
          </span>
          <Button
            variant="secondary"
            disabled={Boolean(error) || pageNumber >= pdf.numPages}
            onClick={() => setPageNumber((page) => page + 1)}
          >
            Next <ArrowRight size={16} />
          </Button>
        </div>
      )}
      <ErrorBanner message={error} retry={() => setAttempt((value) => value + 1)} />
      <div ref={host} className="reference-pdf-page" aria-busy={!error && readyPage !== pageNumber}>
        {!error && readyPage !== pageNumber && <Loading label="Rendering PDF page…" />}
        <canvas
          ref={canvas}
          role="img"
          aria-label={`${title}, page ${pageNumber}`}
          data-rendered-page={readyPage || undefined}
          hidden={Boolean(error) || readyPage !== pageNumber}
        />
      </div>
      {readyPage === pageNumber && !error && (
        <details className="reference-pdf-text">
          <summary>Page text</summary>
          <p>{pageText || 'This page contains images rather than selectable text.'}</p>
        </details>
      )}
      <p className="reference-pdf-source">Download the original above for printing, or {source}.</p>
    </section>
  )
}
