import { expect, test } from 'vitest'
import type { Profile } from '../types'
import { guidedRecipe, guideProblem, initialGuide } from './guidedRecipe'

const profile: Profile = {
  id: 'pico-s',
  name: 'Pico S',
  batch_liters: 5,
  efficiency: 50,
  max_steps: 15,
  max_target_c: 100,
  max_grain_kg: 0,
  hop_slots: 0,
  control_supported: true,
  notes: '',
}
const draft = () => ({
  ...initialGuide(),
  name: 'Pale ale',
  grains: [{ name: 'Pale malt', amount: '1.2' }],
  hops: [
    { name: 'Cascade', amount: '10', minutes: '5' },
    { name: 'Centennial', amount: '20', minutes: '30' },
    { name: 'Amarillo', amount: '8', minutes: '5' },
  ],
})

test('hop countdowns become sequential holds and simultaneous varieties share a bin', () => {
  const d = draft()
  expect(guideProblem(d, profile, 3)).toBe('')
  const recipe = guidedRecipe(d, profile)
  expect(recipe.steps.map((s) => [s.location, s.minutes])).toEqual([
    [1, 0],
    [2, 60],
    [1, 30],
    [3, 25],
    [4, 5],
  ])
  expect(recipe.ingredients.map((i) => i.minutes)).toEqual([0, 5, 30, 5])
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
      'hop addition'
    )
  expect(
    guideProblem(
      { ...d, hops: [1, 2, 3, 4, 5].map((n) => ({ ...d.hops[0], minutes: String(n) })) },
      profile,
      3
    )
  ).toContain('4 different hop times')
  expect(guideProblem(d, { ...profile, max_steps: 4 }, 3)).toContain('step limit')
})
