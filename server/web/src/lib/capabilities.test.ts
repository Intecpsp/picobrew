import { describe, expect, it } from 'vitest'
import { isAccessory, profileForModel, sensorValue, supports } from './capabilities'
import { historyChannels, sampleReading } from './history'
import type { BrewDetail, Capabilities, Sample } from '../types'

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
