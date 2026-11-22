import type { Profile, Recipe, RecipeStep } from '../types'
import { goesInKeg, grainInfo, ingredientDefaults } from './beerCrafter'
import { newRecipe } from './utils'

export type Grain = { name: string; amount: string }
export type HopAddition = { slot: number; minutes: string; returns?: string[]; varieties: Grain[] }
export type MashRest = { name?: string; celsius: number; minutes: string }
export type GuidedDraft = {
  name: string
  batch: string
  grains: Grain[]
  hops: HopAddition[]
  style: string
  yeast: string
  yeastGrams: string
  firstWort: boolean
  mashPreset: string
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
    style: '',
    yeast: '',
    yeastGrams: '',
    firstWort: false,
    mashPreset: 'standard',
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
  if (draft.mashPreset === 'infusion') return [rest('Single infusion', 152, 90)]
  if (draft.mashPreset === 'mash-out')
    return [rest('Single infusion', 152, 90), rest('Mash out', 175, 15)]
  if (draft.mashPreset === 'high-efficiency')
    return [
      rest('Dough in', 104, 20),
      rest('Mash 1', 145, 40),
      rest('Mash 2', 161, 80),
      rest('Mash out', 175, 20),
    ]
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
  const scheduled = hopRoute(draft.hops.filter((h) => h.varieties.some((v) => v.name || v.amount)))
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
      location: location > 6 ? 0 : location,
      drain_minutes,
    })
  }
  if (rests.length) add('Heat water', 'heat', rests[0].celsius, 0, 1)
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
  const firstWort = draft.firstWort && beforeHops === 0 && !!scheduled.length
  if (firstWort)
    add(
      'Heat through hop slot ' + scheduled[0].slot,
      'heat',
      largeBrewer ? (207 - 32) / 1.8 : 95,
      0,
      scheduled[0].slot + 2
    )
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
    style: draft.style.trim(),
    source_model: profile.id,
    batch_liters: draft.batch === '' ? profile.batch_liters : Number(draft.batch),
    efficiency: profile.efficiency,
    boil_minutes: boil,
    type: 'All Grain',
    ingredients: [
      ...grains.map((g) => ({
        ...ingredientDefaults('fermentable', g.name),
        name: g.name.trim(),
        kind: 'fermentable',
        amount: Number(g.amount),
        unit: 'kg',
        use: isKegAddition(g) ? 'boil' : 'mash',
        minutes: 0,
      })),
      ...hops.map((h) => ({
        ...ingredientDefaults('hop', h.name),
        name: h.name.trim(),
        kind: 'hop',
        amount: Number(h.amount),
        unit: 'g',
        use: firstWort && Number(h.minutes) === boil ? 'first-wort' : 'boil',
        minutes: Number(h.minutes),
      })),
      ...(draft.yeast.trim() && Number(draft.yeastGrams) > 0
        ? [
            {
              kind: 'yeast',
              name: draft.yeast.trim(),
              amount: Number(draft.yeastGrams),
              unit: 'g',
              minutes: 0,
              use: 'fermentation',
              ...ingredientDefaults('yeast', draft.yeast),
            },
          ]
        : []),
    ],
    steps,
  }
}

export function hopLimit(profile: Profile) {
  return Math.min(profile.hop_slots || 4, 4)
}

export function usesPicoPakRouting(profileID: string) {
  return ['pico-s', 'pico-pro', 'pico-c'].includes(profileID)
}

function visitStarts(hop: HopAddition) {
  return [hop.minutes, ...(hop.returns ?? [])]
}

export function hopRoute(hops: HopAddition[]) {
  return hops
    .flatMap((h) => visitStarts(h).map((minutes) => ({ slot: h.slot, minutes: Number(minutes) })))
    .sort((a, b) => b.minutes - a.minutes)
}

// Times count down to boil end. Only successive visits to the same slot have
// a local ordering constraint. Every slot shares the single direct inlet.
export function slotTimes(hops: HopAddition[], slot: number, boil: number, visitIndex = 0) {
  if (!Number.isInteger(boil) || boil < 1 || boil > 599) return []
  const used = new Set<number>()
  let upper = boil,
    lower = 1
  for (const hop of hops) {
    visitStarts(hop).forEach((minutes, index) => {
      const time = Number(minutes)
      if (hop.slot !== slot) used.add(time)
      else if (index < visitIndex) upper = Math.min(upper, time - 1)
      else if (index > visitIndex) lower = Math.max(lower, time + 1)
    })
  }
  return Array.from({ length: Math.max(0, upper - lower + 1) }, (_, i) => upper - i).filter(
    (time) => !used.has(time)
  )
}

export function totalWeight(grains: Grain[]) {
  return grains.reduce((sum, item) => sum + Number(item.amount), 0)
}

export const isKegAddition = (g: Grain) => goesInKeg(grainInfo(g.name)?.type)
export const basketWeight = (grains: Grain[]) =>
  totalWeight(grains.filter((g) => !isKegAddition(g)))

export function grainOverLimit(grains: Grain[], profile: Profile) {
  return profile.max_grain_kg > 0 && basketWeight(grains) > profile.max_grain_kg + 1e-9
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
  if (draft.hops.length > slots) return `This brewer has room for ${slots} hop slots.`
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
    if (
      visitStarts(hop).some(
        (time, index) => !slotTimes(draft.hops, hop.slot, boil, index).includes(Number(time))
      )
    )
      return 'Choose a separate available minute within the boil for each visit.'
  }
  if (recipe.steps.length > (profile.max_steps || 15))
    return 'The schedule exceeds this brewer’s step limit. Remove a return visit or a mash rest.'
  if (
    (draft.yeast.trim() || draft.yeastGrams) &&
    !(draft.yeast.trim() && Number(draft.yeastGrams) > 0)
  )
    return 'Enter both the yeast name and pitch weight, or leave both blank.'
  if (
    recipe.steps.some(
      (s) => (s.kind === 'hop' || s.kind === 'boil') && profile.id === 'zymatic' && s.minutes > 255
    )
  )
    return 'Stock Zymatic allows at most 255 minutes in one boil or hop step.'
  if (stage === 1) return ''
  if (!mashSchedule(draft, profile).length) return 'Add at least one mash rest.'
  if (profile.id === 'zymatic' && mashSchedule(draft, profile).some((r) => Number(r.minutes) > 255))
    return 'Stock Zymatic allows at most 255 minutes in one mash rest.'
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
  if (profile.id === 'zymatic' && recipe.steps.some((s) => Math.round(s.target_c * 1.8 + 32) > 207))
    return 'Stock Zymatic accepts temperatures up to 207 °F (about 97.2 °C).'
  if (recipe.steps.some((s) => s.target_c > profile.max_target_c))
    return 'This schedule exceeds the brewer’s temperature limit. Adjust the targets in the advanced editor.'
  return ''
}
