import type { Capabilities, Device, Sensor, Unit } from '../types'
import { temperature } from './utils'

export function supports(info: Capabilities | null | undefined, name: string): boolean {
  if (typeof info?.capabilities?.[name] === 'boolean') return info.capabilities[name]
  if (name === 'front_panel') return info?.front_panel === true
  if (name === 'telemetry_forwarding') return info?.telemetry_forwarding === true
  if (name === 'history_persistent') return info?.history_persistent === true
  if (name === 'recipes') return (info?.recipe_capacity || 0) > 0
  return (
    ['brewing', 'heating', 'temperature', 'pump'].includes(name) &&
    (!info?.model || ['pico-s', 'pico-pro', 'pico-c'].includes(info.model)) &&
    (info?.recipe_capacity || 0) > 0
  )
}

export function isAccessory(device: Pick<Device, 'model' | 'capabilities'>): boolean {
  return (
    ['picoferm', 'picostill'].includes(device.model) ||
    device.capabilities?.capabilities?.brewing === false
  )
}

export function profileForModel(model: string): string {
  return model === 'z-series' ? 'z' : model
}

export function sensorValue(sensor: Pick<Sensor, 'value' | 'unit'>, unit: Unit): string {
  if (sensor.value == null || !Number.isFinite(sensor.value)) return '—'
  if (sensor.unit === 'F') return temperature(sensor.value, unit)
  const suffix = sensor.unit === 'raw' ? ' raw' : sensor.unit ? ` ${sensor.unit}` : ''
  return `${sensor.value.toLocaleString(undefined, { maximumFractionDigits: sensor.unit === 'rpm' ? 0 : 2 })}${suffix}`
}

export const commandLabels: Record<string, string> = {
  start: 'Start session',
  stop: 'Stop',
  'essential-oils': 'Essential oils',
  clean: 'Clean',
  calibrate: 'Calibrate pressure',
  sample: 'Read sensors now',
  'set-pump': 'Set pump',
  'set-fan': 'Set fan',
  'set-sampling-interval': 'Set sampling interval',
}

export const calibrationPrerequisite =
  'Remove PicoFerm from the keg or adapter and remove the GRAY seal. Confirm it is fully depressurized before calibrating the pressure sensor.'
