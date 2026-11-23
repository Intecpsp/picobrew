import { modelLabel } from '../lib/utils'

export const usbModels = [
  'pico-s',
  'pico-pro',
  'pico-c',
  'z-series',
  'picostill',
  'picoferm',
  'zymatic',
]
export const usbLabel = (model: string) =>
  model === 'z-series' ? 'Pico Z module (Z1–Z4)' : modelLabel(model)

export type USBAction = 'install' | 'backup' | 'restore'
