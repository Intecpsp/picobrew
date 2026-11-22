import { unmeasuredYeast } from '../lib/beerCrafter'
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
        <div className="table-scroll">
          <table className="data-table" aria-label="Scaled ingredient bill">
            <thead>
              <tr>
                <th>Ingredient</th>
                <th>Amount</th>
                <th>When to add</th>
              </tr>
            </thead>
            <tbody>
              {unmeasuredYeast(recipe) && (
                <tr>
                  <td>{unmeasuredYeast(recipe)}</td>
                  <td>Pitch amount unspecified</td>
                  <td>Fermentation</td>
                </tr>
              )}
              {recipe.ingredients.map((i, n) => (
                <tr key={n}>
                  <td>{i.name}</td>
                  <td>{formatQuantity(i.amount, i.unit)}</td>
                  <td>
                    {i.slot_addition ||
                    (i.kind === 'hop' && (i.use === 'boil' || i.use === 'first-wort'))
                      ? [
                          plan.hop_slots_by_minute?.[i.minutes]
                            ? 'Hop slot ' + plan.hop_slots_by_minute[i.minutes]
                            : '',
                          i.use === 'first-wort'
                            ? 'First wort'
                            : i.minutes + ' min before boil ends',
                        ]
                          .filter(Boolean)
                          .join(' · ')
                      : i.use}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
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
