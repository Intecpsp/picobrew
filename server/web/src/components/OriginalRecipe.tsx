import { useState } from 'react'
import { useResource } from '../context'
import { formatNumber } from '../lib/recipeDisplay'
import { ErrorBanner, Loading, Modal } from './ui'

type Item = {
  Name: string
  Amount: number
  Units?: string | null
  Time?: number
  Location?: number
}
type Step = { Name: string; Temperature: number; Time: number; Drain: number }
interface SourceRecipe {
  Name: string
  Author: string
  BatchSize: number
  Efficiency: number
  IsMetric: boolean
  Notes: string
  TastingNotes?: string
  Fermentables: Item[]
  Hops: Item[]
  DryHops: Item[]
  WhirlpoolHops: Item[]
  Adjuncts: Item[]
  WhirlpoolAdjuncts: Item[]
  DryAdjuncts: Item[]
  Amendments: Item[]
  Yeast?: { Name: string; Laboratory?: string; ProductID?: string }
  MachineSteps: Step[]
  FermentationSteps: { Name: string; Temp: number; Days: number; Hours: number; Minutes: number }[]
  HumanBrewingSteps?: { FillKeg?: string }
}

export function OriginalRecipeLink({ id }: { id: string }) {
  const [open, setOpen] = useState(false)
  return (
    <>
      <button type="button" className="text-button" onClick={() => setOpen(true)}>
        View original recipe
      </button>
      {open && <OriginalRecipe id={id} close={() => setOpen(false)} />}
    </>
  )
}

function OriginalRecipe({ id, close }: { id: string; close: () => void }) {
  const source = useResource<{ VM: { Recipe: SourceRecipe } }>(
    '/catalog/' + encodeURIComponent(id) + '/source'
  )
  const recipe = source.data?.VM?.Recipe
  const groups: [string, Item[], string][] = recipe
    ? [
        ['Grain and extract', recipe.Fermentables, 'lb'],
        ['Boil hops', recipe.Hops, 'oz'],
        ['Whirlpool hops', recipe.WhirlpoolHops, 'oz'],
        ['Dry hops', recipe.DryHops, 'oz'],
        ['Boil additions', recipe.Adjuncts, ''],
        ['Whirlpool additions', recipe.WhirlpoolAdjuncts, ''],
        ['Fermentation additions', recipe.DryAdjuncts, ''],
        ['Water additions', recipe.Amendments, ''],
      ]
    : []
  return (
    <Modal title="Original recipe" onClose={close} wide>
      {source.loading ? (
        <Loading label="Opening archived recipe…" />
      ) : source.error || !recipe || recipe.IsMetric !== false ? (
        <ErrorBanner
          message={source.error || 'This record has an unrecognized source format.'}
          retry={source.reload}
        />
      ) : (
        <>
          <h3>{recipe.Name}</h3>
          <p>
            {[
              recipe.Author,
              formatNumber(recipe.BatchSize) + ' US gal',
              formatNumber(recipe.Efficiency) + '% efficiency',
            ]
              .filter(Boolean)
              .join(' · ')}
          </p>
          {recipe.HumanBrewingSteps?.FillKeg && <p>{recipe.HumanBrewingSteps.FillKeg}</p>}
          <h3>Ingredients</h3>
          <div className="table-scroll">
            <table className="data-table">
              <thead>
                <tr>
                  <th>Ingredient</th>
                  <th>Amount</th>
                  <th>Use</th>
                </tr>
              </thead>
              <tbody>
                {groups.flatMap(([group, items, unit]) =>
                  (items ?? []).map((i, n) => (
                    <tr key={group + n}>
                      <td>{i.Name}</td>
                      <td>
                        {formatNumber(i.Amount)} {i.Units || unit || '(unit unspecified)'}
                      </td>
                      <td>
                        {group}
                        {group === 'Boil hops' &&
                          i.Time !== undefined &&
                          ' · ' + formatNumber(i.Time) + ' min before boil ends'}
                      </td>
                    </tr>
                  ))
                )}
              </tbody>
            </table>
          </div>
          {recipe.Yeast?.Name && (
            <p>
              Yeast ·{' '}
              {[recipe.Yeast.Name, recipe.Yeast.Laboratory, recipe.Yeast.ProductID]
                .filter(Boolean)
                .join(' · ')}
            </p>
          )}
          {!!recipe.MachineSteps?.length && (
            <>
              <h3>Brew schedule</h3>
              <div className="table-scroll">
                <table className="data-table">
                  <thead>
                    <tr>
                      <th>Step</th>
                      <th>Target</th>
                      <th>Hold</th>
                      <th>Drain</th>
                    </tr>
                  </thead>
                  <tbody>
                    {recipe.MachineSteps.map((s, i) => (
                      <tr key={i}>
                        <td>{s.Name}</td>
                        <td>{s.Temperature ? s.Temperature + ' °F' : '—'}</td>
                        <td>{s.Time ? s.Time + ' min' : 'Reach target'}</td>
                        <td>{s.Drain} min</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            </>
          )}
          {!!recipe.FermentationSteps?.length && (
            <>
              <h3>Fermentation</h3>
              <ul>
                {recipe.FermentationSteps.map((s, i) => (
                  <li key={i}>
                    {[
                      s.Name,
                      s.Temp + ' °F',
                      s.Days && s.Days + ' days',
                      s.Hours && s.Hours + ' hr',
                      s.Minutes && s.Minutes + ' min',
                    ]
                      .filter(Boolean)
                      .join(' · ')}
                  </li>
                ))}
              </ul>
            </>
          )}
          {recipe.Notes && (
            <>
              <h3>Author’s notes</h3>
              <p className="preserve-lines">{recipe.Notes}</p>
            </>
          )}
          {recipe.TastingNotes && (
            <>
              <h3>Tasting notes</h3>
              <p className="preserve-lines">{recipe.TastingNotes}</p>
            </>
          )}
        </>
      )}
    </Modal>
  )
}
