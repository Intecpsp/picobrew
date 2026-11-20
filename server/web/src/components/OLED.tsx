import { useEffect, useRef, useState } from 'react'
import type { Display } from '../types'
import { decodeDisplay } from '../lib/display'
export default function OLED({ display, error }: { display: Display | null; error?: string }) {
  const ref = useRef<HTMLCanvasElement>(null)
  const [invalid, setInvalid] = useState('')
  useEffect(() => {
    if (!display || !ref.current || error) return
    try {
      const pixels = decodeDisplay(display)
      const ctx = ref.current.getContext('2d')
      if (!ctx) throw new Error('Display rendering is unavailable.')
      const data = ctx.createImageData(display.width, display.height)
      pixels.forEach((value, i) => {
        const amount = value / (display.format === 'row-gray4-msb-hex' ? 15 : 1)
        const color = [28, 43, 35].map((base, index) =>
          Math.round(base + ([196, 219, 171][index] - base) * amount)
        )
        data.data.set([...color, 255], i * 4)
      })
      ctx.putImageData(data, 0, 0)
      setInvalid('')
    } catch (error) {
      setInvalid((error as Error).message)
    }
  }, [display, error])
  const live = !!display && !invalid && !error
  return (
    <div className="oled">
      <canvas
        ref={ref}
        hidden={!live}
        width={display?.width}
        height={display?.height}
        aria-label="Live Pico OLED display"
      />
      {live ? (
        <span>
          LIVE OLED · {display.width} × {display.height}
        </span>
      ) : (
        <div className="oled-placeholder">{error || invalid || 'Waiting for the front panel…'}</div>
      )}
    </div>
  )
}
