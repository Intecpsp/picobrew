import { createElement } from 'react'
import { renderToStaticMarkup } from 'react-dom/server'
import { expect, test } from 'vitest'
import type { BrewDetail, Sample } from '../types'
import { HistorySamples } from '../components/HistorySamples'
import { transitions } from './utils'
import { sessionState } from './sessionState'

test('Zymatic journal shows only recorded channels and labels keg, hops and the panel pause', () => {
  const empty: Sample = {
    zymatic_history: true,
    elapsed_ms: 0,
    wort_f: null,
    thermoblock_f: null,
    target_f: null,
    compartment: null,
    current_step: null,
    source: 'device',
  }
  const samples = [
    empty,
    { ...empty, elapsed_ms: 1000, compartment: 0, current_step: 0 },
    { ...empty, elapsed_ms: 61000, compartment: 2, current_step: 0 },
  ]
  const brew: BrewDetail = {
    id: 'a',
    device_id: 'a',
    device_name: 'Zymatic',
    model: 'zymatic',
    recipe_name: 'Pale ale',
    state: 'interrupted',
    command: 'brew',
    started_at: null,
    observed_at: '',
    duration_ms: 61000,
    device_history_id: 1,
    gap_count: 0,
    truncated: false,
    notes: '',
    rating: 0,
    sample_count: 3,
    source: 'device',
    samples,
    sample_fields: [
      'elapsed_ms',
      'wort_f',
      'thermoblock_f',
      'target_f',
      'compartment',
      'current_step',
    ],
  }
  const table = renderToStaticMarkup(createElement(HistorySamples, { brew, page: 0, unit: 'F' }))
  for (const title of ['Wort', 'Thermoblock', 'Target', 'Compartment', 'Step'])
    expect(table).toContain('<th>' + title + '</th>')
  for (const absent of ['Flags', 'Controller', 'State', 'Phase', 'Step time'])
    expect(table).not.toContain('<th>' + absent + '</th>')
  expect(table).toContain('Not recorded')
  expect(table).toContain('Keg')
  expect(table).toContain('Hop addition 1')
  expect(sessionState(brew, 'compartment', 6, 'device')).toBe('Operator pause')
  expect(transitions(samples)).toHaveLength(3)
})
