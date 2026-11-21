import { expect, test } from 'vitest'
import { newRecipe } from './utils'
import {
  beerStats,
  ingredientBill,
  ingredientDefaults,
  renameIngredient,
  scaleBill,
  styleInfo,
  wortCurve,
} from './beerCrafter'
import type { Ingredient, Recipe } from '../types'
const grain: Ingredient = {
  name: 'Malt',
  kind: 'fermentable',
  type: 'Grain',
  unit: 'kg',
  use: 'mash',
  minutes: 0,
  amount: 1,
  yield: 80,
  color: 2,
}
const hops: Ingredient = {
  name: 'Cascade',
  kind: 'hop',
  unit: 'g',
  use: 'boil',
  minutes: 60,
  amount: 12,
  alpha: 6,
}
const recipe: Recipe = {
  ...newRecipe(),
  batch_liters: 5,
  boil_liters: 6,
  efficiency: 70,
  ingredients: [
    grain,
    hops,
    { ...hops, use: 'dry-hop', amount: 4 },
    {
      kind: 'yeast',
      name: 'Yeast',
      amount: 5,
      unit: 'g',
      minutes: 0,
      use: 'fermentation',
      attenuation: 75,
    },
  ],
}
test('beer estimates use the entered yield, preboil gravity, color and attenuation', () => {
  const s = beerStats(recipe)
  expect(s.og).toBeCloseTo(1.043, 3)
  expect(s.fg).toBeCloseTo(1.011, 3)
  expect(s.abv).toBeCloseTo(4.23, 1)
  expect(s.srm).toBeCloseTo(3.42, 1)
  expect(s.ibu).toBeGreaterThan(30)
  const missing = beerStats({
    ...recipe,
    ingredients: [{ ...grain, yield: undefined, color: undefined }, hops],
  })
  expect(missing).toEqual({ og: null, fg: null, abv: null, srm: null, ibu: null })
})
test('scaling hits the target, preserves dry hops and leaves the source bill untouched', () => {
  for (const [metric, target] of [
    ['og', 1.061],
    ['ibu', 27],
  ] as const) {
    const ingredients = scaleBill(recipe, metric, target)!
    expect(beerStats({ ...recipe, ingredients })[metric]).toBeCloseTo(target, 6)
    expect(ingredients[2]).toBe(recipe.ingredients[2])
  }
  expect(scaleBill(recipe, 'og', 0.9)).toBeNull()
  expect(ingredientBill(recipe.ingredients, 'hop')).toEqual([{ name: 'Cascade', amount: 16 }])
  expect(recipe.ingredients[0].amount).toBe(1)
})
test('library defaults clear on renaming a reference and preserve explicit custom properties', () => {
  const defaults = ingredientDefaults('fermentable', 'American Black Barley')
  expect(defaults).toMatchObject({ color: 530, type: 'Grain' })
  expect(
    renameIngredient(
      { ...grain, ...defaults, name: 'American Black Barley', color: 450 },
      'My barley'
    )
  ).toMatchObject({ yield: undefined, name: 'My barley' })
  expect(styleInfo('American Light Lager')?.og).toEqual([1.028, 1.04])
})
test('temperature forecast respects timed ramps, drains, native routing and unknown stretches', () => {
  const s = {
    name: 'Mash',
    kind: 'mash',
    target_c: 65,
    minutes: 30,
    location: 2,
    drain_minutes: 2,
    ramp_minutes: 10,
  }
  const curve = wortCurve({
    ...recipe,
    steps: [s, { ...s, name: 'Drain', target_c: 0, minutes: 3, drain_minutes: 0, ramp_minutes: 0 }],
  })
  expect(curve.minutes).toBe(45)
  expect(curve.points.at(-1)?.c).toBeNull()
  const program = {
    id: '1',
    name: 'Hop or pause',
    steps: [{ name: 'Last slot', target_f: 190, minutes: 10, location: 6, drain_minutes: 0 }],
  }
  expect(wortCurve({ ...recipe, source_model: 'z', native_program: program }).minutes).toBe(0)
  expect(
    wortCurve({ ...recipe, source_model: 'pico-s', native_program: program }).minutes
  ).toBeGreaterThan(10)
})
