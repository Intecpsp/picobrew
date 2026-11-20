import { describe, it, expect } from 'vitest'
import { decodeDisplay } from './display'
describe('Pico OLED decoder', () => {
  it('preserves all sixteen Z grayscale levels with the high nibble at even x', () => {
    const frame = {
      width: 128,
      height: 64,
      rotation: 0,
      format: 'row-gray4-msb-hex',
      data: '0123456789abcdef' + '00'.repeat(4088),
    }
    const pixels = decodeDisplay(frame)
    expect(Array.from(pixels.slice(0, 16))).toEqual(Array.from({ length: 16 }, (_, index) => index))
    expect(pixels[128]).toBe(0)
    expect(pixels).toHaveLength(8192)
    for (const invalid of [{ rotation: undefined }, { height: 32 }, { data: frame.data.slice(2) }])
      expect(() => decodeDisplay({ ...frame, ...invalid })).toThrow()
  })
  it('decodes page-major LSB pixels and rotates both axes 180 degrees', () => {
    const pixels = decodeDisplay({ width: 2, height: 8, format: 'page-lsb-hex', data: '01 80' })
    expect(Array.from(pixels)).toEqual([1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1])
  })
  it('addresses successive vertical pages correctly', () => {
    const pixels = decodeDisplay({ width: 1, height: 16, format: 'page-lsb-hex', data: '0001' })
    expect(pixels[7]).toBe(1)
    expect(pixels.reduce((sum, pixel) => sum + pixel, 0)).toBe(1)
  })
  it('renders the C SSD1309 viewport without rotation or hidden buffer pages', () => {
    const pixels = decodeDisplay({
      width: 128,
      height: 32,
      rotation: 0,
      display_variant: 'ssd1309',
      storage_bytes: 1024,
      transfer_bytes: 1024,
      format: 'page-lsb-hex',
      data: '01' + '00'.repeat(511) + 'ff'.repeat(512),
    })
    expect(pixels).toHaveLength(128 * 32)
    expect(pixels[0]).toBe(1)
    expect(pixels.reduce((sum, pixel) => sum + pixel, 0)).toBe(1)
  })
  it('honors explicit C SSD1325 rotation', () => {
    const pixels = decodeDisplay({
      width: 128,
      height: 64,
      rotation: 2,
      display_variant: 'ssd1325',
      storage_bytes: 1024,
      transfer_bytes: 1024,
      format: 'page-lsb-hex',
      data: '01' + '00'.repeat(1023),
    })
    expect(pixels[128 * 64 - 1]).toBe(1)
    expect(pixels.reduce((sum, pixel) => sum + pixel, 0)).toBe(1)
  })
  it('rejects inconsistent storage, transfer and rotation metadata', () => {
    const frame = { width: 128, height: 32, format: 'page-lsb-hex', data: '00'.repeat(512) }
    for (const invalid of [
      { storage_bytes: 511 },
      { storage_bytes: 1024 },
      { storage_bytes: 32769 },
      { storage_bytes: 512.5 },
      { transfer_bytes: 511 },
      { transfer_bytes: 1024 },
      { rotation: 1 },
      { rotation: -1 },
    ])
      expect(() => decodeDisplay({ ...frame, ...invalid })).toThrow()
  })
  it('rejects truncated, malformed or oversized frames', () => {
    for (const data of ['', 'zz', '001122'])
      expect(() => decodeDisplay({ width: 1, height: 8, format: 'page-lsb-hex', data })).toThrow()
    expect(() =>
      decodeDisplay({ width: 10000, height: 8, format: 'page-lsb-hex', data: '00' })
    ).toThrow()
    expect(() => decodeDisplay({ width: 1, height: 8, format: 'unknown', data: '00' })).toThrow()
  })
})
