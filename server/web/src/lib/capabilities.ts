import type { Capabilities, Device, Sensor, Unit } from '../types'
import { temperature } from './utils'

export function supports(info: Capabilities | null | undefined, name: string): boolean {
  if (typeof info?.capabilities?.[name] === 'boolean') return info.capabilities[name]
  if (name === 'z_program_maker') return info?.z_program_maker === true
  if (name === 'pairing_v1') return info?.pairing_v1 === true
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

export function preferenceFields(
  device?: Pick<Device, 'adapter' | 'model' | 'capabilities'>
): string[] {
  const declared = device?.capabilities?.settings_fields
  if (declared != null) return declared
  // CC3200 discovery predates the settings_fields list.
  if (
    device &&
    (device.adapter === 'simulator' || ['pico-s', 'pico-pro', 'pico-c'].includes(device.model))
  )
    return ['temperature_unit', 'language', 'detailed_status']
  return []
}

export function isAccessory(device: Pick<Device, 'model' | 'capabilities'>): boolean {
  return (
    ['picoferm', 'picostill'].includes(device.model) ||
    device.capabilities?.capabilities?.brewing === false
  )
}

export function brewerOutputsIdle(
  device: Pick<Device, 'online' | 'last_seen' | 'status'>,
  lastCommandAt: number,
  now = Date.now()
): boolean {
  const status = device.status
  const seen = Date.parse(device.last_seen ?? '')
  // The Hub normally polls every three seconds. An older reading cannot establish current activity.
  if (
    !device.online ||
    !status?.ready ||
    status.busy !== false ||
    status.stopped !== false ||
    !Number.isFinite(seen) ||
    seen <= lastCommandAt ||
    Math.abs(now - seen) > 15_000
  )
    return false

  const state = status.state?.toLowerCase() || ''
  const command = status.command || ''
  // S/Pro/C identify the idle menu by command. Z reports its core state instead.
  if (
    (!state && !command) ||
    !['', 'idle', 'standby'].includes(state) ||
    !['', 'idle'].includes(command)
  )
    return false
  return !(status.actuators || []).some(
    (item) =>
      ['heater', 'pump'].includes(item.kind) &&
      (item.enabled !== false || (item.level ?? 0) > 0 || (item.rpm ?? 0) > 0)
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
  'ack-clean-condenser': 'Mark condenser as cleaned',
  'ack-sanitize-seal': 'Mark seal as sanitized',
  'set-pump': 'Set pump',
  'set-fan': 'Set fan',
  'set-sampling-interval': 'Set sampling interval',
}

export const calibrationPrerequisite =
  'Remove PicoFerm from the keg or adapter and remove the GRAY seal. Confirm it is fully depressurized before calibrating the pressure sensor.'
