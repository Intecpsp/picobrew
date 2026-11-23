import { describe, expect, it } from 'vitest'
import {
  brewerOutputsIdle,
  isAccessory,
  profileForModel,
  sensorValue,
  supports,
} from './capabilities'
import { historyChannels, sampleReading } from './history'
import type { Actuator, BrewDetail, Capabilities, DeviceStatus, Sample } from '../types'

const legacy: Capabilities = {
  api_version: 1,
  firmware_version: '1.0.0',
  commands: [],
  recipe_capacity: 16,
  step_capacity: 15,
  front_panel: true,
  history_persistent: true,
  telemetry_forwarding: true,
  dedicated_pause_resume: false,
}

describe('family capabilities and named measurements', () => {
  it('honors explicit false capabilities even when legacy fields suggest a brewer', () => {
    const caps = {
      ...legacy,
      recipe_capacity: 16,
      front_panel: true,
      capabilities: { recipes: false, brewing: false, front_panel: false, pressure: true },
    } as Capabilities
    expect(supports(caps, 'recipes')).toBe(false)
    expect(supports(caps, 'brewing')).toBe(false)
    expect(supports(caps, 'front_panel')).toBe(false)
    expect(supports(caps, 'pressure')).toBe(true)
    expect(isAccessory({ model: 'picostill', capabilities: caps })).toBe(true)
  })
  it('recognizes physical dial pairing from firmware discovery', () => {
    const actual = { ...legacy, model: 'pico-s', front_panel: true, pairing_v1: true }
    expect(supports(actual, 'pairing_v1')).toBe(true)
    expect(supports({ ...actual, capabilities: { pairing_v1: false } }, 'pairing_v1')).toBe(false)
    expect(supports(legacy, 'pairing_v1')).toBe(false)
  })
  it('retains older S/Pro discovery without assigning unsupported features', () => {
    const caps = legacy
    expect(supports(caps, 'brewing')).toBe(true)
    expect(supports(caps, 'pressure')).toBe(false)
    expect(supports(caps, 'calibration')).toBe(false)
    expect(profileForModel('z-series')).toBe('z')
    expect(profileForModel('picoferm')).toBe('picoferm')
  })
  it('uses declared units without guessing a unit for Ferm pressure', () => {
    expect(sensorValue({ value: 68, unit: 'F' }, 'C')).toBe('20.0°C')
    expect(sensorValue({ value: 532.125, unit: 'raw' }, 'F')).toMatch(/532\.13 raw/)
    expect(sensorValue({ value: 6.89, unit: 'kPa' }, 'F')).toBe('6.89 kPa')
    expect(sensorValue({ value: null, unit: 'V' }, 'F')).toBe('—')
  })
  it('plots archived named channels and omits fictitious accessory heating', () => {
    const sample = {
      step_seconds: 0,
      phase: 0,
      controller_status: 0,
      compartment: 0,
      current_step: 255,
      flags: 3,
      device_state: 2,
      source: 'device',
      elapsed_ms: 0,
      wort_f: null,
      thermoblock_f: null,
      target_f: null,
      temperature_f: 70,
      pressure_kpa: null,
      battery_v: 3.8,
      readings: { sensor_pressure_native: 532.125, sensor_temperature: 70 },
    } as Sample
    const brew = {
      model: 'picoferm',
      samples: [sample],
      sample_fields: [
        'elapsed_ms',
        'wort_f',
        'thermoblock_f',
        'target_f',
        'temperature_f',
        'pressure_kpa',
        'battery_v',
        'sensor_pressure_native',
        'sensor_temperature',
      ],
      field_metadata: {
        sensor_pressure_native: { label: 'Native pressure', unit: 'raw', kind: 'pressure' },
        sensor_temperature: { label: 'Probe', unit: 'F', kind: 'temperature' },
      },
    }
    const channels = historyChannels(brew)
    expect(channels.map((item) => item.field)).toEqual([
      'temperature_f',
      'battery_v',
      'sensor_pressure_native',
      'sensor_temperature',
    ])
    expect(channels.find((item) => item.field === 'sensor_pressure_native')?.metadata.unit).toBe(
      'raw'
    )
    expect(sampleReading(sample, 'sensor_pressure_native')).toBe(532.125)
    expect(sampleReading(sample, 'sensor_missing')).toBeNull()
  })
})

describe('brewer output lock label', () => {
  const now = Date.parse('2026-11-22T20:15:00Z')
  const device = {
    online: true,
    last_seen: new Date(now).toISOString(),
    status: { ready: true, busy: false, stopped: false, command: 'idle' } as DeviceStatus,
  }
  function idle(status: Partial<DeviceStatus>) {
    return brewerOutputsIdle({ ...device, status: { ...device.status, ...status } }, 0, now)
  }
  it('recognizes a Pico at the idle menu and a Z in idle or standby', () => {
    expect(idle({})).toBe(true)
    expect(idle({ state: 'StandBy', command: '' })).toBe(true)
    expect(idle({ state: 'Idle', command: '', target_f: 160, recipe_name: 'Previous brew' })).toBe(
      true
    )
  })
  it('treats operating, unrecognized, and incomplete status as possibly active', () => {
    for (const command of ['brew', 'manual', 'rinse', 'clean', 'front-panel', 'unknown'])
      expect(idle({ command })).toBe(false)
    for (const state of ['Paused', 'Running', 'Starting', 'unknown'])
      expect(idle({ state })).toBe(false)
    expect(idle({ ready: false })).toBe(false)
    expect(idle({ stopped: true })).toBe(false)
    expect(idle({ busy: true })).toBe(false)
    expect(idle({ command: '', state: '' })).toBe(false)
  })
  it('requires a fresh, connected observation taken after the last operation', () => {
    expect(brewerOutputsIdle({ ...device, online: false }, 0, now)).toBe(false)
    expect(brewerOutputsIdle({ ...device, status: null }, 0, now)).toBe(false)
    expect(brewerOutputsIdle({ ...device, last_seen: null }, 0, now)).toBe(false)
    expect(brewerOutputsIdle(device, now, now)).toBe(false)
    expect(brewerOutputsIdle(device, now - 1, now)).toBe(true)
    expect(brewerOutputsIdle(device, 0, now + 60_000)).toBe(false)
  })
  it('checks heater and pump activity without treating an indicator as an energized output', () => {
    const pump: Actuator = {
      id: 'hex-pump',
      label: 'HEX pump',
      kind: 'pump',
      enabled: false,
      level: null,
      rpm: null,
    }
    expect(idle({ actuators: [pump, { ...pump, kind: 'fan', enabled: true, rpm: 1200 }] })).toBe(
      true
    )
    for (const actuator of [
      { ...pump, enabled: true },
      { ...pump, enabled: null },
      { ...pump, rpm: 500 },
      { ...pump, kind: 'heater', level: 10 },
    ])
      expect(idle({ actuators: [actuator] })).toBe(false)
  })
})
