import type { MachineRecipe, Recipe } from '../types'

export function isNative(recipe: Recipe) {
  return recipe.kind === 'native-program' || recipe.kind === 'maintenance'
}
export function kindLabel(kind?: string, sourceModel?: string) {
  if (kind === 'maintenance') return 'Cleaning program'
  if (kind !== 'native-program') return 'Ingredient recipe'
  if (['pico', 'pico-s', 'pico-pro', 'pico-c'].includes(sourceModel || '')) return 'PicoPak'
  if (sourceModel === 'z') return 'ZPak'
  if (sourceModel === 'zymatic') return 'Zymatic program'
  return 'Brew program'
}
// Typical uses in the bundled PicoPak starter schedules, not physical valve names.
export const picoPakStepUses = [
  'Heat water',
  'Mash',
  'Hop addition 1',
  'Hop addition 2',
  'Hop addition 3',
  'Hop addition 4',
]

// Archived Z/Zymatic schedules encode valves from zero and reserve 6 for a pause.
export function routeLabel(location: number, model?: string, archived = false) {
  if (archived && (model === 'z' || model === 'zymatic'))
    return location === 6 ? 'Pause' : `Valve position ${location + 1}`
  if (location === 0) return 'Unassigned'
  if (
    ['pico', 'pico-s', 'pico-pro', 'pico-c'].includes(model ?? '') &&
    picoPakStepUses[location - 1]
  )
    return picoPakStepUses[location - 1]
  return `Valve position ${location}`
}
export function programTiming(program: MachineRecipe) {
  const minutes = program.steps.reduce((sum, step) => sum + step.minutes + step.drain_minutes, 0)
  return `${minutes} min${program.steps.some((step) => step.target_f > 0) ? ' + heating' : ''}`
}
export function externalURL(value?: string) {
  if (!value) return undefined
  try {
    const url = new URL(value)
    return ['https:', 'http:'].includes(url.protocol) ? url.href : undefined
  } catch {
    return undefined
  }
}
export function catalogAssetURL(value?: string) {
  return value?.startsWith('/api/v1/catalog/assets/') && !value.includes('..') ? value : undefined
}
export function copyProgram(program: MachineRecipe): MachineRecipe {
  return { ...program, steps: program.steps.map((step) => ({ ...step })) }
}
