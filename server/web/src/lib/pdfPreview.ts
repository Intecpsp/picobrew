export const MAX_PDF_BYTES = 32 * 1024 * 1024
export const MAX_PDF_CANVAS_PIXELS = 4 * 1024 * 1024

export async function readPDFBytes(
  response: Response,
  signal: AbortSignal,
  maximum = MAX_PDF_BYTES
): Promise<Uint8Array> {
  if (Number(response.headers.get('content-length')) > maximum) {
    await response.body?.cancel()
    throw new Error('This PDF is too large to preview. Download the original to read it.')
  }
  if (!response.body) throw new Error('The PDF response was empty.')
  const reader = response.body.getReader()
  const chunks: Uint8Array[] = []
  let length = 0
  try {
    while (true) {
      signal.throwIfAborted()
      const { value, done } = await reader.read()
      signal.throwIfAborted()
      if (done) break
      length += value.byteLength
      if (length > maximum)
        throw new Error('This PDF is too large to preview. Download the original to read it.')
      chunks.push(value)
    }
  } catch (error) {
    await reader.cancel().catch(() => {})
    throw error
  } finally {
    reader.releaseLock()
  }
  if (!length) throw new Error('The PDF response was empty.')
  const bytes = new Uint8Array(length)
  let offset = 0
  for (const chunk of chunks) {
    bytes.set(chunk, offset)
    offset += chunk.byteLength
  }
  return bytes
}

export function pdfPageFromFragment(fragment: string, pages: number) {
  const page = Number(new URLSearchParams(fragment).get('page'))
  return Number.isInteger(page) && page > 0 ? Math.min(page, pages) : 1
}
