import type { Display } from '../types'
// Pico's framebuffer is page-major, bit 0 at the top of each 8-pixel page.
// Older S/Pro firmware omits rotation and draws with a 180-degree orientation.
// C SSD1309 exposes 32 logical rows while retaining 64 rows of raw storage.
export function decodeDisplay(display: Display): Uint8Array {
  const { width, height } = display
  if (
    !Number.isInteger(width) ||
    !Number.isInteger(height) ||
    width < 1 ||
    height < 1 ||
    width > 512 ||
    height > 512
  )
    throw new Error('Invalid OLED dimensions')
  const gray = display.format === 'row-gray4-msb-hex'
  if (display.format !== 'page-lsb-hex' && !gray)
    throw new Error(`Unsupported display format: ${display.format}`)
  const hex = display.data.replace(/\s/g, '')
  if (gray && (width !== 128 || height !== 64 || display.rotation == null))
    throw new Error('Invalid grayscale OLED geometry')
  const size = gray ? (width * height) / 2 : width * Math.ceil(height / 8)
  const storage = display.storage_bytes ?? size
  const rotation = display.rotation ?? 2
  const transfer = display.transfer_bytes ?? storage
  if (rotation !== 0 && rotation !== 2) throw new Error('Unsupported OLED rotation')
  if (
    !Number.isInteger(storage) ||
    storage < size ||
    storage > 32768 ||
    !Number.isInteger(transfer) ||
    transfer < size ||
    transfer > storage
  )
    throw new Error('Invalid OLED storage geometry')
  if (!/^[0-9a-f]*$/i.test(hex) || hex.length !== storage * 2)
    throw new Error('Incomplete OLED frame')
  const bytes = Uint8Array.from({ length: size }, (_, i) =>
    parseInt(hex.slice(i * 2, i * 2 + 2), 16)
  )
  const pixels = new Uint8Array(width * height)
  for (let y = 0; y < height; y++)
    for (let x = 0; x < width; x++)
      pixels[rotation === 2 ? (height - 1 - y) * width + (width - 1 - x) : y * width + x] = gray
        ? (bytes[y * (width / 2) + (x >> 1)] >> (x % 2 ? 0 : 4)) & 15
        : (bytes[(y >> 3) * width + x] >> (y & 7)) & 1
  return pixels
}
