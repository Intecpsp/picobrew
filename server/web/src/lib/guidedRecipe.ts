import type { Profile, Recipe, RecipeStep } from '../types'
import { newRecipe } from './utils'

export type Grain = { name: string; amount: string }
export type Hop = Grain & { minutes: string }
export type MashRest = { celsius: number; minutes: string }
export type GuidedDraft = {
  name: string
  batch: string
  grains: Grain[]
  hops: Hop[]
  rests: MashRest[]
  boil: string
}

export function initialGuide(): GuidedDraft {
  return {
    name: '',
    batch: '',
    grains: [{ name: '', amount: '' }],
    hops: [],
    rests: [{ celsius: (152 - 32) / 1.8, minutes: '60' }],
    boil: '60',
  }
}

export function additionTimes(hops: Hop[]) {
  return [...new Set(hops.map((h) => Number(h.minutes)))].sort((a, b) => b - a)
}

// Canonical standalone schedules. Z local HTTP lists 1 = keg, 2 = mash,
// 3–6 = adjuncts; PicoPak starter schedules use the same recipe numbering.
// Archived ZPak instructions and controller telemetry use separate encodings.
export function guidedRecipe(draft: GuidedDraft, profile: Profile): Recipe {
  const grains = draft.grains.filter((g) => g.name || g.amount)
  const hops = draft.hops.filter((h) => h.name || h.amount)
  const times = additionTimes(hops)
  const z = profile.id === 'z'
  const steps: RecipeStep[] = []
  function add(
    name: string,
    kind: string,
    target_c: number,
    minutes: number,
    location: number,
    drain_minutes = 0
  ) {
    steps.push({
      name,
      kind,
      target_c,
      minutes,
      location: profile.id === 'zymatic' || location > 6 ? 0 : location,
      drain_minutes,
    })
  }
  add('Heat water', 'heat', draft.rests[0].celsius, 0, 1)
  draft.rests.forEach((rest, i) =>
    add(
      draft.rests.length > 1 ? 'Mash rest ' + (i + 1) : 'Mash',
      'mash',
      rest.celsius,
      Number(rest.minutes),
      2,
      i === draft.rests.length - 1 ? (z ? 8 : 2) : 0
    )
  )
  const boil = Number(draft.boil)
  const beforeHops = boil - (times[0] ?? 0)
  // Z limits keg passthrough to 94.45 °C. 202 °F stays below that ceiling.
  if (beforeHops > 0) add('Boil', 'boil', z ? (202 - 32) / 1.8 : 95, beforeHops, 1)
  times.forEach((remaining, i) =>
    add(
      'Hop addition ' + (i + 1),
      'hop',
      95,
      remaining - (times[i + 1] ?? 0),
      i + 3,
      i === times.length - 1 ? 5 : 0
    )
  )
  return {
    ...newRecipe(),
    name: draft.name.trim(),
    source_model: profile.id,
    batch_liters: draft.batch === '' ? profile.batch_liters : Number(draft.batch),
    efficiency: profile.efficiency,
    boil_minutes: boil,
    type: 'All Grain',
    ingredients: [
      ...grains.map((g) => ({
        name: g.name.trim(),
        kind: 'fermentable',
        amount: Number(g.amount),
        unit: 'kg',
        use: 'mash',
        minutes: 0,
      })),
      ...hops.map((h) => ({
        name: h.name.trim(),
        kind: 'hop',
        amount: Number(h.amount),
        unit: 'g',
        use: 'boil',
        minutes: Number(h.minutes),
      })),
    ],
    steps,
  }
}

export function guideProblem(draft: GuidedDraft, profile: Profile, stage: number): string {
  const recipe = guidedRecipe(draft, profile)
  if (!recipe.name) return 'Give the recipe a name.'
  if (!(recipe.batch_liters > 0 && recipe.batch_liters <= profile.batch_liters))
    return 'Choose a batch size within the brewer’s capacity.'
  if (stage === 0) return ''
  if (!draft.grains.length || draft.grains.some((g) => !g.name.trim() || !(Number(g.amount) > 0)))
    return 'Enter a name and weight for each grain.'
  if (
    profile.max_grain_kg > 0 &&
    draft.grains.reduce((sum, g) => sum + Number(g.amount), 0) > profile.max_grain_kg
  )
    return 'The grain bill exceeds this brewer’s configured capacity.'
  const boil = Number(draft.boil)
  if (!Number.isInteger(boil) || boil < 1 || boil > 599)
    return 'Use a boil time between 1 and 599 whole minutes.'
  for (const hop of draft.hops) {
    if (!hop.name.trim() || !(Number(hop.amount) > 0))
      return 'Enter a variety and weight for each hop.'
    const time = Number(hop.minutes)
    if (!Number.isInteger(time) || time < 1 || time > boil)
      return 'Set each hop addition between 1 minute and the full boil time. Use the advanced editor for flameout or dry hops.'
  }
  const slots = Math.min(profile.hop_slots || 4, 4)
  if (additionTimes(draft.hops).length > slots)
    return `Choose at most ${slots} different hop times. Varieties added together share a bin.`
  if (stage === 1) return ''
  if (
    draft.rests.some(
      (r) =>
        !Number.isFinite(r.celsius) ||
        r.celsius < (60 - 32) / 1.8 ||
        r.celsius > profile.max_target_c ||
        !Number.isInteger(Number(r.minutes)) ||
        Number(r.minutes) < 1 ||
        Number(r.minutes) > 599
    )
  )
    return 'Check the mash temperature and use a hold between 1 and 599 whole minutes.'
  if (recipe.steps.some((s) => s.target_c > profile.max_target_c))
    return 'This schedule exceeds the brewer’s temperature limit. Adjust the targets in the advanced editor.'
  if (recipe.steps.length > Math.min(15, profile.max_steps || 15))
    return 'The schedule exceeds this brewer’s step limit. Remove a mash rest or combine hop additions.'
  return ''
}
