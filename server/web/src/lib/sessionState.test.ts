import { test, expect } from 'vitest'
import { sessionState } from './sessionState'

test('decodes the recorded model and preserves ambiguous and unknown values', () => {
  const brew = { model: 'pico-s', id: 'session', source: 'device' }
  const value = (model: string, field: string, raw: number) =>
    sessionState({ ...brew, model }, field, raw, 'device', true)
  expect(value('pico-s', 'flags', 11)).toBe(
    'Controller ready · Machine busy · Step filter inserted'
  )
  expect(value('pico-s', 'flags', 52)).toBe('Stopped · Sampling gap · Unknown bits 0x20')
  expect(value('pico-s', 'current_step', 1)).toBe('2')
  expect(value('pico-s', 'current_step', 255)).toBe('No step')
  expect(value('pico-pro', 'compartment', 5)).toBe('Adjunct 4 (5)')
  expect(value('pico-s', 'compartment', 6)).toBe('Adjunct 3 (6)')
  expect(value('z', 'compartment', 1)).toBe('Position 1')
  expect(value('pico-s', 'device_state', 0)).toBe('Code 0')
  expect(value('pico-s', 'device_state', 3)).toBe('Brewing (3)')
  expect(value('picostill', 'device_state', 0)).toBe('No program selected (0)')
  expect(value('picoferm', 'device_state', 0)).toBe('Initializing (0)')
  expect(value('z', 'device_state', 0)).toBe('Off (0)')
  expect(
    sessionState(
      { ...brew, id: 'demo-history-demo-pro', source: 'simulation' },
      'device_state',
      0,
      'simulation'
    )
  ).toBe('Not recorded')
})

test('an unheated Pico target is distinct from a missing or measured value', async () => {
  const { unheatedTarget } = await import('./history')
  expect(unheatedTarget('pico-s', 0)).toBe(true)
  expect(unheatedTarget('pico-pro', 152)).toBe(false)
  expect(unheatedTarget('pico-c', null)).toBe(false)
  expect(unheatedTarget('picoferm', 0)).toBe(false)
})
