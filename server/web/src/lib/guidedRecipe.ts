import type { Profile, Recipe, RecipeStep } from '../types'
import { newRecipe } from './utils'

export type Grain = { name: string; amount: string }
export type HopAddition = { slot: number; minutes: string; varieties: Grain[] }
export type MashRest = { name?: string; celsius: number; minutes: string }
export type GuidedDraft = {
  name: string
  batch: string
  grains: Grain[]
  hops: HopAddition[]
  customMash: boolean
  rests: MashRest[]
  boil: string
}

export function initialGuide(): GuidedDraft {
  return {
    name: '',
    batch: '',
    grains: [{ name: '', amount: '' }],
    hops: [],
    customMash: false,
    rests: [],
    boil: '60',
  }
}

// Most frequent four-rest mash in the archived PicoPak programs. The Z and
// Zymatic base recipes use a single infusion for 11 of their 12 beer styles.
export function mashSchedule(draft: GuidedDraft, profile: Profile): MashRest[] {
  if (draft.customMash) return draft.rests
  const rest = (name: string, fahrenheit: number, minutes: number) => ({
    name,
    celsius: (fahrenheit - 32) / 1.8,
    minutes: String(minutes),
  })
  if (profile.id === 'z' || profile.id === 'zymatic') return [rest('Single infusion', 152, 90)]
  return [
    rest('Dough in', 110, 7),
    rest('Mash 1', 148, 25),
    rest('Mash 2', 156, 20),
    rest('Mash out', 176, 7),
  ]
}

// Canonical standalone schedules. Z local HTTP lists 1 = keg, 2 = mash,
// 3–6 = adjuncts; PicoPak starter schedules use the same recipe numbering.
// Archived ZPak instructions and controller telemetry use separate encodings.
export function guidedRecipe(draft: GuidedDraft, profile: Profile): Recipe {
  const grains = draft.grains.filter((g) => g.name || g.amount)
  const hops = draft.hops
    .flatMap((addition) => addition.varieties.map((v) => ({ ...v, minutes: addition.minutes })))
    .filter((h) => h.name || h.amount)
  const scheduled = draft.hops
    .filter((a) => a.varieties.some((v) => v.name || v.amount))
    .sort((a, b) => Number(b.minutes) - Number(a.minutes))
  const z = profile.id === 'z'
  const largeBrewer = z || profile.id === 'zymatic'
  const rests = mashSchedule(draft, profile)
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
  add('Heat water', 'heat', rests[0].celsius, 0, 1)
  rests.forEach((rest, i) =>
    add(
      rest.name ?? (rests.length > 1 ? 'Mash rest ' + (i + 1) : 'Mash'),
      rest.name === 'Mash out' ? 'mash-out' : 'mash',
      rest.celsius,
      Number(rest.minutes),
      2,
      i === rests.length - 1 ? (largeBrewer ? 8 : 2) : 0
    )
  )
  const boil = Number(draft.boil)
  const beforeHops = boil - Number(scheduled[0]?.minutes || 0)
  // Z limits keg passthrough to 94.45 °C. 202 °F stays below that ceiling.
  if (beforeHops > 0) add('Boil', 'boil', z ? (202 - 32) / 1.8 : 95, beforeHops, 1)
  scheduled.forEach((addition, i) =>
    add(
      'Hop slot ' + addition.slot,
      'hop',
      largeBrewer ? (207 - 32) / 1.8 : 95,
      Number(addition.minutes) - Number(scheduled[i + 1]?.minutes || 0),
      addition.slot + 2,
      i === scheduled.length - 1 ? (largeBrewer ? 10 : 5) : 0
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

export function hopLimit(profile: Profile) {
  return Math.min(profile.hop_slots || 4, 4)
}

// Only offer times strictly between the neighboring occupied physical slots.
export function slotTimes(hops: HopAddition[], slot: number, boil: number) {
  const earlier = hops.filter((h) => h.slot < slot).map((h) => Number(h.minutes))
  const later = hops.filter((h) => h.slot > slot).map((h) => Number(h.minutes))
  const last = Math.max(0, ...later) + 1
  const first = Math.min(
    Number.isInteger(boil) && boil <= 599 ? boil : 0,
    ...earlier.map((t) => t - 1)
  )
  return Array.from({ length: Math.max(0, first - last + 1) }, (_, i) => first - i)
}

export function totalWeight(grains: Grain[]) {
  return grains.reduce((sum, item) => sum + Number(item.amount), 0)
}

export function grainOverLimit(grains: Grain[], profile: Profile) {
  return profile.max_grain_kg > 0 && totalWeight(grains) > profile.max_grain_kg + 1e-9
}

export function guideProblem(draft: GuidedDraft, profile: Profile, stage: number): string {
  const recipe = guidedRecipe(draft, profile)
  if (!recipe.name) return 'Give the recipe a name.'
  if (!(recipe.batch_liters > 0 && recipe.batch_liters <= profile.batch_liters))
    return 'Choose a batch size within the brewer’s capacity.'
  if (stage === 0) return ''
  if (!draft.grains.length || draft.grains.some((g) => !g.name.trim() || !(Number(g.amount) > 0)))
    return 'Enter a name and weight for each grain.'
  if (grainOverLimit(draft.grains, profile))
    return 'The grain bill exceeds this brewer’s configured capacity.'
  const boil = Number(draft.boil)
  if (!Number.isInteger(boil) || boil < 1 || boil > 599)
    return 'Use a boil time between 1 and 599 whole minutes.'
  const slots = hopLimit(profile)
  if (draft.hops.length > slots) return `This brewer has room for ${slots} timed hop additions.`
  if (
    draft.hops.some((h) => !Number.isInteger(h.slot) || h.slot < 1 || h.slot > slots) ||
    new Set(draft.hops.map((h) => h.slot)).size !== draft.hops.length
  )
    return 'Choose a different available slot for each addition.'
  for (const hop of draft.hops) {
    if (profile.max_hop_slot_g && totalWeight(hop.varieties) > profile.max_hop_slot_g + 1e-9)
      return `Slot ${hop.slot} exceeds its ${profile.max_hop_slot_g} g hop limit.`
    if (
      !hop.varieties.length ||
      hop.varieties.some((v) => !v.name.trim() || !(Number(v.amount) > 0))
    )
      return 'Enter a variety and weight for each hop.'
    const time = Number(hop.minutes)
    if (!slotTimes(draft.hops, hop.slot, boil).includes(time))
      return 'Choose a whole minute within the boil for each hop addition. Earlier slots must start first.'
  }
  if (stage === 1) return ''
  if (
    mashSchedule(draft, profile).some(
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
