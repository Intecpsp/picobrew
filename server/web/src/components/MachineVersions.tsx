import { RecipeLoadingSheet } from './RecipeLoadingSheet'
import { OriginalRecipeLink } from './OriginalRecipe'
import { useState } from 'react'
import { useHub } from '../context'
import { modelLabel } from '../lib/utils'
import { useRecipeQuantity } from './RecipeMeasures'
import { formatTemperature, formatNumber } from '../lib/recipeDisplay'
import type { Adapted } from '../types'
import { ProgramTable } from './Program'
import { Field, SectionHeading } from './ui'

export function MachineVersions({
  versions,
  initialModel,
  sourceID,
}: {
  versions?: Adapted[]
  sourceID?: string
  initialModel?: string
}) {
  const hub = useHub()
  const [selection, setSelection] = useState(
    initialModel || hub.devices.find((d) => d.online)?.model || 'pico-s'
  )
  const plan =
    versions?.find((v) => v.profile.id === (selection === 'z-series' ? 'z' : selection)) ||
    versions?.[0]
  if (!plan) return null
  return (
    <section className="panel" aria-label="Machine versions">
      <SectionHeading
        title="Machine versions"
        actions={sourceID && <OriginalRecipeLink id={sourceID} />}
      />
      <Field label="View brewer version">
        <select value={plan.profile.id} onChange={(e) => setSelection(e.target.value)}>
          {versions?.map((v) => (
            <option key={v.profile.id} value={v.profile.id}>
              {modelLabel(v.profile.id)}
            </option>
          ))}
        </select>
      </Field>
      <BrewerPlan plan={plan} />
    </section>
  )
}

export function BrewerPlan({ plan }: { plan: Adapted }) {
  const formatQuantity = useRecipeQuantity()
  const { recipe, profile, machine_recipe: program } = plan
  return (
    <>
      {recipe.batch_liters > 0 && (
        <p>
          Estimated yield <strong>{formatQuantity(recipe.batch_liters, 'L')}</strong> ·{' '}
          {formatNumber(recipe.efficiency)}% efficiency
          {!program && ' · Needs recipe review'}
        </p>
      )}
      {plan.deployment_blockers?.map((reason) => (
        <p key={reason} className="warning-note">
          {reason}
        </p>
      ))}
      {!!recipe.ingredients?.length && (
        <RecipeLoadingSheet recipe={recipe} slots={plan.hop_slots_by_minute} />
      )}
      {program && (
        <ProgramTable program={program} model={profile.id} archived={profile.id === 'zymatic'} />
      )}
      {!!recipe.fermentation?.length && (
        <p>
          {recipe.fermentation
            .map(
              (s) =>
                s.name +
                ' · ' +
                formatTemperature(s.target_c) +
                ' · ' +
                formatNumber(s.days) +
                ' days'
            )
            .join(' / ')}
        </p>
      )}
      {plan.warnings
        ?.filter((w) => !w.startsWith('Native recipe ID') && !w.includes('temperature rounds'))
        .map((w) => (
          <p className="help-text" key={w}>
            {w}
          </p>
        ))}
    </>
  )
}
