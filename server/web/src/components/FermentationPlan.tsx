import { useState } from 'react'
import { goesInKeg, yeastInfo } from '../lib/beerCrafter'
import { formatVolume } from '../lib/recipeDisplay'
import { QuantityField } from './RecipeMeasures'
import type { Recipe } from '../types'
import { useHub } from '../context'
import { Button, Field, IconButton, SectionHeading } from './ui'
import { Plus, Trash2 } from 'lucide-react'

const minerals = [
  'calcium',
  'magnesium',
  'sodium',
  'chloride',
  'sulfate',
  'bicarbonate',
  'ph',
] as const
const celsius = (f: number) => (f - 32) / 1.8
export function FermentationPlan({
  recipe,
  change,
}: {
  recipe: Recipe
  change: (v: Partial<Recipe>) => void
}) {
  const { unit } = useHub(),
    stages = recipe.fermentation ?? [],
    carbonation = recipe.carbonation ?? {}
  const display = (c: number) => Number((unit === 'F' ? c * 1.8 + 32 : c).toFixed(2))
  const toC = (v: number) => (unit === 'F' ? celsius(v) : v)
  const add = (name: string, f: number, days: number) => ({ name, target_c: celsius(f), days })
  const [absorption, setAbsorption] = useState(''),
    [loss, setLoss] = useState('')
  const grainKg = recipe.ingredients
    .filter((i) => i.kind === 'fermentable' && !goesInKeg(i.type))
    .reduce((n, i) => n + i.amount, 0)
  const water = recipe.ingredients.filter((i) => i.kind === 'water')
  const pitch = recipe.ingredients
    .filter((i) => i.kind === 'yeast')
    .map((i) => yeastInfo(i.name)?.pitchF)
  const pitchF = pitch.length && pitch.every((f) => f === pitch[0]) ? pitch[0] : undefined
  const hasPitchStep = recipe.steps.some(
    (s) => s.kind === 'cool' && s.name === 'Chill to pitching temperature'
  )
  const estimate = (recipe.boil_liters || 0) + grainKg * Number(absorption) + Number(loss)
  const ready =
    (recipe.boil_liters || 0) > 0 &&
    absorption !== '' &&
    loss !== '' &&
    Number(absorption) >= 0 &&
    Number(loss) >= 0 &&
    Number.isFinite(estimate)
  function applyWater() {
    if (!ready || water.length > 1) return
    change({
      ingredients: water.length
        ? recipe.ingredients.map((i) => (i === water[0] ? { ...i, amount: estimate } : i))
        : [
            ...recipe.ingredients,
            {
              name: 'Brewing water',
              kind: 'water',
              unit: 'L',
              amount: estimate,
              use: 'mash',
              minutes: 0,
            },
          ],
    })
  }
  function preset(kind: string) {
    if (!kind || (stages.length && !window.confirm('Replace the current fermentation stages?')))
      return
    change({
      fermentation:
        kind === 'ale'
          ? [add('Primary fermentation', 68, 10), add('Crash chill', 44, 0)]
          : [add('Primary fermentation', 68, 21), add('Lagering', 35, 28)],
    })
  }
  return (
    <section className="panel recipe-plan">
      <SectionHeading title="Water and fermentation" />
      <h4>Starting water</h4>
      <p>Start with the pre-boil volume and add the water your grain and equipment retain.</p>
      <div className="form-grid thirds">
        <Field label="Grain absorption (L/kg)">
          <input
            type="number"
            min="0"
            step="any"
            value={absorption}
            onChange={(e) => setAbsorption(e.target.value)}
          />
        </Field>
        <QuantityField label="Equipment loss" unit="L" value={loss} change={setLoss} />
        <Button variant="secondary" disabled={!ready || water.length > 1} onClick={applyWater}>
          {ready ? 'Use ' + formatVolume(estimate) + ' of water' : 'Use estimated starting water'}
        </Button>
      </div>
      {!(recipe.boil_liters && recipe.boil_liters > 0) && (
        <small>Enter a boil volume in Boil planning to estimate starting water.</small>
      )}
      {water.length > 1 && (
        <small>Adjust the amounts of your separate water sources in Ingredients.</small>
      )}
      <h4>Water profile</h4>
      <div className="form-grid thirds">
        {minerals.map((m) => (
          <Field
            label={m === 'ph' ? 'Water pH' : m[0].toUpperCase() + m.slice(1) + ' (ppm)'}
            key={m}
          >
            <input
              type="number"
              max={m === 'ph' ? 14 : undefined}
              min="0"
              step="any"
              value={recipe.water?.[m] ?? ''}
              onChange={(e) => {
                const water = { ...recipe.water }
                if (!e.target.value) delete water[m]
                else water[m] = Number(e.target.value)
                change({ water })
              }}
            />
          </Field>
        ))}
      </div>
      {!water.length && recipe.water && Object.keys(recipe.water).length > 0 && (
        <small>
          Add the water volume to the ingredient bill to include this profile in BeerXML.
        </small>
      )}
      <h4>Fermentation stages</h4>
      {pitchF !== undefined && pitchF > 32 && (
        <Button
          variant="ghost"
          disabled={hasPitchStep}
          onClick={() =>
            change({
              steps: [
                ...recipe.steps,
                {
                  name: 'Chill to pitching temperature',
                  kind: 'cool',
                  target_c: celsius(pitchF),
                  location: 0,
                  minutes: 0,
                  drain_minutes: 0,
                },
              ],
            })
          }
        >
          <Plus size={16} /> Plan chill to {display(celsius(pitchF))} °{unit} for pitching
        </Button>
      )}
      <Field label="Apply fermentation preset">
        <select value="" onChange={(e) => preset(e.target.value)}>
          <option value="">Choose a preset</option>
          <option value="ale">Ale fermentation</option>
          <option value="lager">Lager fermentation</option>
        </select>
      </Field>
      {stages.map((s, i) => (
        <div className="form-grid quarters" key={i}>
          <Field label={'Stage ' + (i + 1) + ' name'}>
            <input
              value={s.name}
              onChange={(e) =>
                change({
                  fermentation: stages.map((stage, j) =>
                    i === j ? { ...stage, name: e.target.value } : stage
                  ),
                })
              }
            />
          </Field>
          <Field label={'Stage ' + (i + 1) + ' temperature (°' + unit + ')'}>
            <input
              type="number"
              step="any"
              value={display(s.target_c)}
              onChange={(e) =>
                change({
                  fermentation: stages.map((stage, j) =>
                    i === j ? { ...stage, target_c: toC(Number(e.target.value)) } : stage
                  ),
                })
              }
            />
          </Field>
          <Field
            label={'Stage ' + (i + 1) + ' days'}
            hint={
              i === stages.length - 1
                ? '0 ends the plan when the target temperature is reached.'
                : undefined
            }
          >
            <input
              type="number"
              min={i === stages.length - 1 ? 0 : 0.01}
              step="any"
              value={s.days}
              onChange={(e) =>
                change({
                  fermentation: stages.map((stage, j) =>
                    i === j ? { ...stage, days: Number(e.target.value) } : stage
                  ),
                })
              }
            />
          </Field>
          <IconButton
            label={'Remove fermentation stage ' + (i + 1)}
            onClick={() => change({ fermentation: stages.filter((_, j) => i !== j) })}
          >
            <Trash2 size={16} />
          </IconButton>
        </div>
      ))}
      <Button
        variant="ghost"
        onClick={() =>
          change({
            fermentation:
              stages.at(-1)?.days === 0
                ? [...stages.slice(0, -1), add('Fermentation', 68, 7), stages[stages.length - 1]]
                : [...stages, add('Fermentation', 68, 7)],
          })
        }
      >
        <Plus size={15} /> Add stage
      </Button>
      <h4>Carbonation</h4>
      <div className="form-grid thirds">
        <Field label="Carbonation method">
          <select
            value={carbonation.method ?? ''}
            onChange={(e) => change({ carbonation: { ...carbonation, method: e.target.value } })}
          >
            <option value="">Choose method</option>
            <option value="forced">Force carbonate</option>
            <option value="natural">Bottle or keg condition</option>
          </select>
        </Field>
        <Field label="Volumes of CO₂">
          <input
            type="number"
            min="0"
            step="any"
            max="10"
            value={carbonation.volumes ?? ''}
            onChange={(e) =>
              change({ carbonation: { ...carbonation, volumes: Number(e.target.value) } })
            }
          />
        </Field>
        <Field label={'Carbonation temperature (°' + unit + ')'}>
          <input
            type="number"
            step="any"
            value={
              carbonation.temperature_c === undefined ? '' : display(carbonation.temperature_c)
            }
            onChange={(e) =>
              change({
                carbonation: {
                  ...carbonation,
                  temperature_c: e.target.value === '' ? undefined : toC(Number(e.target.value)),
                },
              })
            }
          />
        </Field>
      </div>
    </section>
  )
}
