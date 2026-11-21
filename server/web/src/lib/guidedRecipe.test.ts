import { expect, test } from 'vitest'
import type { Profile } from '../types'
import { guidedRecipe, guideProblem, initialGuide, slotTimes } from './guidedRecipe'

const profile: Profile = {
  id: 'pico-s',
  name: 'Pico S',
  batch_liters: 5,
  efficiency: 50,
  max_steps: 15,
  max_target_c: 100,
  max_grain_kg: 2,
  max_hop_slot_g: 42.5,
  hop_slots: 4,
  control_supported: true,
  notes: '',
}
const draft = () => ({
  ...initialGuide(),
  name: 'Pale ale',
  grains: [{ name: 'Pale malt', amount: '1.2' }],
  hops: [
    {
      slot: 4,
      minutes: '5',
      varieties: [
        { name: 'Cascade', amount: '10' },
        { name: 'Amarillo', amount: '8' },
      ],
    },
    { slot: 2, minutes: '30', varieties: [{ name: 'Centennial', amount: '20' }] },
  ],
})

test('hop countdowns become sequential holds and simultaneous varieties share a bin', () => {
  const d = draft()
  expect(guideProblem(d, profile, 3)).toBe('')
  const recipe = guidedRecipe(d, profile)
  expect(recipe.steps.map((s) => [s.location, s.minutes])).toEqual([
    [1, 0],
    [2, 7],
    [2, 25],
    [2, 20],
    [2, 7],
    [1, 30],
    [4, 25],
    [6, 5],
  ])
  expect(recipe.ingredients.map((i) => i.minutes)).toEqual([0, 5, 5, 30])
  expect(recipe.efficiency).toBe(50)
  expect(guidedRecipe({ ...d, hops: [] }, profile).steps.at(-1)).toMatchObject({
    name: 'Boil',
    minutes: 60,
    location: 1,
  })
})

test('Z uses its configured batch and grain limit, Zymatic keeps a planning schedule', () => {
  const z = {
    ...profile,
    id: 'z',
    batch_liters: 9.46352946,
    efficiency: 70,
    max_grain_kg: 4.08,
    hop_slots: 4,
  }
  const d = draft(),
    recipe = guidedRecipe(d, z)
  expect(recipe.batch_liters).toBe(z.batch_liters)
  expect(recipe.steps[1].drain_minutes).toBe(8)
  expect(recipe.steps[2].target_c).toBeLessThan(94.45)
  expect(guideProblem({ ...d, grains: [{ name: 'Malt', amount: '5' }] }, z, 1)).toContain(
    'grain bill exceeds'
  )
  expect(guidedRecipe(d, { ...z, id: 'zymatic' }).steps.every((s) => s.location === 0)).toBe(true)
})

test('cannot finish a schedule outside the supported timing and equipment limits', () => {
  const d = draft()
  for (const minutes of ['', '0', '2.5', '61'])
    expect(guideProblem({ ...d, hops: [{ ...d.hops[0], minutes }] }, profile, 3)).toContain(
      'within the boil'
    )
  expect(
    guideProblem(
      { ...d, hops: [1, 2, 3, 4, 5].map((n) => ({ ...d.hops[0], slot: n, minutes: String(n) })) },
      profile,
      3
    )
  ).toContain('4 hop slots')
  expect(guideProblem(d, { ...profile, max_steps: 4 }, 3)).toContain('step limit')
})

test('the standard PicoPak mash matches archived temperatures, active times and drains', () => {
  const d = draft()
  const fields = (model: string) =>
    guidedRecipe(d, { ...profile, id: model })
      .steps.filter((s) => s.location === 2)
      .map((s) => [Math.round(s.target_c * 1.8 + 32), s.minutes, s.drain_minutes])
  for (const id of ['pico-s', 'pico-c', 'pico-pro'])
    expect(fields(id)).toEqual([
      [110, 7, 0],
      [148, 25, 0],
      [156, 20, 0],
      [176, 7, 2],
    ])
  expect(fields('z')).toEqual([[152, 90, 8]])
  const custom = guidedRecipe(
    { ...d, customMash: true, rests: [{ celsius: 65, minutes: '38' }] },
    profile
  )
  expect(custom.steps[1]).toMatchObject({
    location: 2,
    minutes: 38,
    target_c: 65,
    drain_minutes: 2,
  })
})

test('PicoPak routing uses one direct inlet at each time and preserves nonnumeric slot order', () => {
  const d = {
    ...draft(),
    boil: '45',
    hops: [
      [1, 45],
      [3, 30],
      [4, 25],
      [2, 15],
    ].map(([slot, minutes]) => ({
      slot,
      minutes: String(minutes),
      varieties: [{ name: 'Hop', amount: '5' }],
    })),
  }
  for (const id of ['pico-s', 'pico-pro', 'pico-c']) {
    const p = { ...profile, id }
    expect(slotTimes(d.hops, 2, 45)).toContain(15)
    expect(slotTimes(d.hops, 2, 45)).not.toContain(25)
    expect(guideProblem(d, p, 3)).toBe('')
    expect(
      guidedRecipe(d, p)
        .steps.filter((s) => s.kind === 'hop')
        .map((s) => [s.location, s.minutes])
    ).toEqual([
      [3, 15],
      [5, 5],
      [6, 10],
      [4, 15],
    ])
    expect(
      guideProblem(
        { ...d, hops: d.hops.map((h) => (h.slot === 2 ? { ...h, minutes: '25' } : h)) },
        p,
        3
      )
    ).toContain('separate available minute')
  }
})

test('eight sequenced visits consume four ingredient loads and exactly one boil', () => {
  const d = {
    ...draft(),
    hops: [
      { slot: 1, minutes: '60', returns: ['12'], varieties: [{ name: 'Cascade', amount: '20' }] },
      { slot: 2, minutes: '52', returns: ['29'], varieties: [{ name: 'Citra', amount: '10' }] },
      { slot: 3, minutes: '45', returns: ['5'], varieties: [{ name: 'Mosaic', amount: '15' }] },
      { slot: 4, minutes: '38', returns: ['20'], varieties: [{ name: 'Amarillo', amount: '8' }] },
    ],
  }
  for (const id of ['pico-s', 'pico-c', 'pico-pro', 'z', 'zymatic']) {
    const p = { ...profile, id }
    expect(guideProblem(d, p, 3)).toBe('')
    const program = guidedRecipe(d, p)
    expect(program.ingredients.filter((i) => i.kind === 'hop')).toHaveLength(4)
    const visits = program.steps.filter((s) => s.kind === 'hop')
    expect(visits.map((s) => [s.name, s.minutes])).toEqual(
      [
        [1, 8],
        [2, 7],
        [3, 7],
        [4, 9],
        [2, 9],
        [4, 8],
        [1, 7],
        [3, 5],
      ].map(([slot, time]) => ['Hop slot ' + slot, time])
    )
    expect(visits.reduce((sum, s) => sum + s.minutes, 0)).toBe(60)
    expect(guideProblem(d, { ...p, max_steps: 5 }, 3)).toContain('step limit')
  }
  expect(slotTimes(d.hops, 1, 60, 1)).not.toContain(29)
  expect(slotTimes(d.hops, 1, 60, 1)).toContain(12)
})
