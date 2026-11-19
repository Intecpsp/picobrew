import { describe, expect, it, vi } from 'vitest'
import { pdfPageFromFragment, readPDFBytes } from './pdfPreview'

describe('bounded PDF preview loading', () => {
  it('retains exact PDF bytes and reads no more than the size limit', async () => {
    const signal = new AbortController().signal
    expect(await readPDFBytes(new Response('%PDF-1.4'), signal, 8)).toEqual(
      new TextEncoder().encode('%PDF-1.4')
    )
    const cancel = vi.fn()
    const oversized = new ReadableStream({
      start(controller) {
        controller.enqueue(new Uint8Array(9))
      },
      cancel,
    })
    await expect(readPDFBytes(new Response(oversized), signal, 8)).rejects.toThrow('too large')
    expect(cancel).toHaveBeenCalledOnce()
    await expect(
      readPDFBytes(new Response('body', { headers: { 'content-length': '9' } }), signal, 8)
    ).rejects.toThrow('too large')
  })

  it('rejects empty and aborted loads', async () => {
    const controller = new AbortController()
    await expect(readPDFBytes(new Response(''), controller.signal)).rejects.toThrow('empty')
    controller.abort()
    await expect(readPDFBytes(new Response('PDF'), controller.signal)).rejects.toMatchObject({
      name: 'AbortError',
    })
  })

  it('bounds requested page fragments to the actual document', () => {
    expect(pdfPageFromFragment('page=3&zoom=page-fit', 10)).toBe(3)
    expect(pdfPageFromFragment('page=30', 10)).toBe(10)
    for (const value of ['', 'section', 'page=-1', 'page=abc', 'page=1.5'])
      expect(pdfPageFromFragment(value, 10)).toBe(1)
  })
})
