import { expect, test } from 'vitest'
import { newRecipe } from './utils'
import {
  beerStats,
  ingredientBill,
  ingredientDefaults,
  renameIngredient,
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
test('the hop chart combines kettle and dry additions of the same variety', () => {
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

test('before-pitch dextrose raises pitching gravity without changing kettle utilization', () => {
  // Circle of Sun has 4 avoirdupois ounces for the original 3.2-US-gallon batch.
  const sugar: Ingredient = {
    ...grain,
    name: 'Corn Sugar (Dextrose)',
    type: 'Sugar',
    amount: 0.1133980925,
    yield: (45.65 / 46) * 100,
    color: 0,
    use: 'before pitching',
  }
  const source = { ...recipe, batch_liters: 3.2 * 3.785411784 }
  const base = beerStats(source)
  const withSugar = beerStats({ ...source, ingredients: [...source.ingredients, sugar] })
  expect(withSugar.og! - base.og!).toBeCloseTo((0.25 * 45.65) / 3.2 / 1000, 8)
  expect(withSugar.ibu).toBe(base.ibu)
  expect(withSugar.srm).toBe(base.srm)
  expect(withSugar.fg).toBeNull()
  expect(withSugar.abv).toBeNull()
  const ambiguous = beerStats({
    ...source,
    ingredients: [...source.ingredients, { ...sugar, use: 'fermentation' }],
  })
  expect(ambiguous.og).toBeNull()
  expect(ambiguous.ibu).toBe(base.ibu)
})

test('late lactose has no kettle or pitching extract and makes packaged gravity uncertain', () => {
  const base = beerStats(recipe)
  for (const milkSugar of [
    {
      ...grain,
      name: 'Lactose Milk Sugar',
      amount: 0.226796185,
      use: 'conditioning',
      type: 'Sugar',
      yield: 98,
      color: 1,
    },
    { ...grain, name: 'Lactose', kind: 'misc', unit: 'g', amount: 226.796185, use: 'conditioning' },
  ]) {
    const stats = beerStats({ ...recipe, ingredients: [...recipe.ingredients, milkSugar] })
    expect(stats).toEqual({ ...base, fg: null, abv: null })
  }
})

test('withholds single-gravity IBU for a timed late kettle fermentable', () => {
  const stats = beerStats({
    ...recipe,
    boil_minutes: 60,
    ingredients: [
      ...recipe.ingredients,
      { ...grain, name: 'Dry extract', type: 'Dry Extract', use: 'boil', minutes: 10 },
    ],
  })
  expect(stats.ibu).toBeNull()
  expect(stats.og).toBeGreaterThan(beerStats(recipe).og!)
})
