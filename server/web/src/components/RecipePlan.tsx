import { QuantityField } from './RecipeMeasures'
import { routeLabel } from '../lib/programs'
import { goesInKeg, renameIngredient, ingredientDefaults, yeastInfo } from '../lib/beerCrafter'
import { useState } from 'react'
import { ArrowDown, ArrowUp, Plus, Trash2 } from 'lucide-react'
import type { Ingredient, RecipeStep, Unit } from '../types'
import { ingredientUnits } from '../lib/utils'
import { Button, Field, IconButton, SectionHeading } from './ui'
import './recipe-plan.scss'

const groups = [
  { title: 'Grain bill', kind: 'fermentable', add: 'Add grain' },
  { title: 'Hop additions', kind: 'hop', add: 'Add hops' },
  { title: 'Yeast, water & other additions', kind: 'misc', add: 'Add other ingredient' },
]
const grainUses = { mash: 'Mash', boil: 'Boil' }
const solubleUses = {
  ...grainUses,
  'before pitching': 'Before pitching yeast',
  fermentation: 'During fermentation',
  secondary: 'Secondary fermentation',
  conditioning: 'Conditioning',
  priming: 'Priming',
}
const hopUses = {
  boil: 'Boil',
  'first-wort': 'First wort',
  'dry-hop': 'Dry hop',
  aroma: 'Whirlpool or flameout',
  mash: 'Mash',
}
const kinds: Record<string, string> = {
  fermentable: 'Fermentable',
  hop: 'Hop',
  yeast: 'Yeast',
  water: 'Water',
  misc: 'Other',
}

export function RecipeIngredients({
  items,
  sourceYeast,
  change,
}: {
  sourceYeast?: string
  items: Ingredient[]
  change: (items: Ingredient[]) => void
}) {
  function patch(index: number, values: Partial<Ingredient>) {
    change(items.map((item, i) => (i === index ? { ...item, ...values } : item)))
  }
  return (
    <section className="panel recipe-plan">
      <SectionHeading title="Ingredients" />

      {groups.map((group) => {
        const members = items
          .map((item, index) => ({ item, index }))
          .filter(({ item }) =>
            group.kind === 'misc'
              ? !['fermentable', 'hop'].includes(item.kind)
              : item.kind === group.kind
          )
        return (
          <div className="plan-group" key={group.kind}>
            <div className="plan-group-head">
              <h4>{group.title}</h4>
              {group.kind === 'misc' && (
                <Button
                  variant="ghost"
                  onClick={() =>
                    change([
                      ...items,
                      {
                        name: '',
                        kind: 'misc',
                        type: 'Water Agent',
                        use: 'mash',
                        amount: 1,
                        unit: 'g',
                        minutes: 0,
                      },
                    ])
                  }
                >
                  <Plus size={15} /> Add water amendment
                </Button>
              )}
              <Button
                variant="ghost"
                onClick={() =>
                  change([
                    ...items,
                    {
                      name: '',
                      kind: group.kind,
                      amount: group.kind === 'hop' ? 10 : 1,
                      unit: ingredientUnits(group.kind)[0],
                      use:
                        group.kind === 'hop' ? 'boil' : group.kind === 'fermentable' ? 'mash' : '',
                      minutes: 0,
                    },
                  ])
                }
              >
                <Plus size={15} />
                {group.add}
              </Button>
            </div>
            {group.kind === 'misc' && sourceYeast && (
              <p>Yeast: {sourceYeast} · Pitch amount unspecified.</p>
            )}
            {members.map(({ item, index }) => {
              const hop = item.kind === 'hop',
                units = ingredientUnits(item.kind)
              const uses = hop
                ? hopUses
                : item.kind === 'fermentable'
                  ? goesInKeg(item.type)
                    ? solubleUses
                    : grainUses
                  : null
              return (
                <div className="plan-row" key={index}>
                  <div className="ingredient-fields">
                    <Field
                      label={
                        hop
                          ? 'Hop variety'
                          : group.kind === 'fermentable'
                            ? 'Grain or extract'
                            : 'Ingredient'
                      }
                    >
                      <input
                        aria-label={`Ingredient ${index + 1} name`}
                        list={'crafter-' + item.kind}
                        value={item.name}
                        placeholder={
                          hop
                            ? 'e.g. Cascade'
                            : group.kind === 'fermentable'
                              ? 'e.g. Pale malt'
                              : 'Name'
                        }
                        onChange={(e) => patch(index, renameIngredient(item, e.target.value))}
                      />
                    </Field>
                    <QuantityField
                      label="Amount"
                      unit={item.unit}
                      value={item.amount}
                      change={(v) => patch(index, { amount: Number(v) })}
                    />
                    {hop && (
                      <Field
                        label={
                          item.use.toLowerCase() === 'boil'
                            ? 'Minutes before boil ends'
                            : 'Addition time (min)'
                        }
                      >
                        <input
                          type="number"
                          min="0"
                          step="any"
                          value={item.minutes}
                          onChange={(e) => patch(index, { minutes: Number(e.target.value) })}
                        />
                      </Field>
                    )}
                    <Field label="Kind">
                      <select
                        value={item.kind}
                        onChange={(e) => {
                          const kind = e.target.value
                          patch(index, {
                            kind,
                            unit: ingredientUnits(kind)[0],
                            attenuation: undefined,
                            yield: undefined,
                            alpha: undefined,
                            color: undefined,
                            type: '',
                            form: '',
                            ...ingredientDefaults(kind, item.name),
                          })
                        }}
                      >
                        {Object.entries(kinds).map(([k, label]) => (
                          <option key={k} value={k}>
                            {label}
                          </option>
                        ))}
                      </select>
                    </Field>
                    {units.length > 1 && (
                      <Field label="Unit">
                        <select
                          value={item.unit}
                          onChange={(e) => patch(index, { unit: e.target.value })}
                        >
                          {units.map((u) => (
                            <option key={u}>{u}</option>
                          ))}
                        </select>
                      </Field>
                    )}
                    <Field label="Use">
                      {uses ? (
                        <select
                          value={item.use}
                          onChange={(e) => patch(index, { use: e.target.value })}
                        >
                          {!Object.hasOwn(uses, item.use) && (
                            <option value={item.use}>{item.use || 'Choose how to add'}</option>
                          )}
                          {Object.entries(uses).map(([value, label]) => (
                            <option key={value} value={value}>
                              {label}
                            </option>
                          ))}
                        </select>
                      ) : (
                        <input
                          value={item.use}
                          onChange={(e) => patch(index, { use: e.target.value })}
                        />
                      )}
                    </Field>
                    {hop ? (
                      <Field label="Alpha acid (%)">
                        <input
                          type="number"
                          min="0"
                          max="100"
                          step="any"
                          value={item.alpha ?? ''}
                          onChange={(e) =>
                            patch(index, {
                              alpha: e.target.value === '' ? undefined : Number(e.target.value),
                            })
                          }
                        />
                      </Field>
                    ) : (
                      <Field label="Addition time (min)">
                        <input
                          type="number"
                          min="0"
                          step="any"
                          value={item.minutes}
                          onChange={(e) => patch(index, { minutes: Number(e.target.value) })}
                        />
                      </Field>
                    )}
                    <Field label="Type">
                      <input
                        value={item.type || ''}
                        onChange={(event) => patch(index, { type: event.target.value })}
                        placeholder={
                          item.kind === 'fermentable'
                            ? 'Grain'
                            : item.kind === 'hop'
                              ? 'Bittering'
                              : 'Ale'
                        }
                      />
                    </Field>
                    <Field label="Form">
                      <input
                        value={item.form || ''}
                        onChange={(event) => patch(index, { form: event.target.value })}
                        placeholder={item.kind === 'hop' ? 'Pellet' : 'Dry'}
                      />
                    </Field>
                    {item.kind === 'yeast' && (
                      <Field
                        label="Expected attenuation (%)"
                        hint={
                          yeastInfo(item.name)
                            ? 'Reference fermentation range ' +
                              yeastInfo(item.name)!.minF +
                              '–' +
                              yeastInfo(item.name)!.maxF +
                              ' °F'
                            : ''
                        }
                      >
                        <input
                          type="number"
                          min="0"
                          max="100"
                          step="any"
                          value={item.attenuation ?? ''}
                          onChange={(e) =>
                            patch(index, {
                              attenuation:
                                e.target.value === '' ? undefined : Number(e.target.value),
                            })
                          }
                        />
                      </Field>
                    )}
                    {item.kind === 'fermentable' && (
                      <>
                        <Field label="Yield (%)">
                          <input
                            type="number"
                            min="0"
                            max="100"
                            value={item.yield ?? ''}
                            onChange={(event) =>
                              patch(index, {
                                yield:
                                  event.target.value === ''
                                    ? undefined
                                    : Number(event.target.value),
                              })
                            }
                          />
                        </Field>
                        <Field label="Color (Lovibond)">
                          <input
                            type="number"
                            min="0"
                            value={item.color ?? ''}
                            onChange={(event) =>
                              patch(index, {
                                color:
                                  event.target.value === ''
                                    ? undefined
                                    : Number(event.target.value),
                              })
                            }
                          />
                        </Field>
                      </>
                    )}
                    <Button
                      variant="ghost"
                      onClick={() => change(items.filter((_, i) => i !== index))}
                    >
                      <Trash2 size={15} /> Remove ingredient
                    </Button>
                  </div>
                </div>
              )
            })}
          </div>
        )
      })}
    </section>
  )
}

const stepKinds: Record<string, string> = {
  mash: 'Mash rest',
  'mash-out': 'Mash out',
  boil: 'Boil',
  hop: 'Hop step',
  heat: 'Heat',
  hold: 'Hold',
  rest: 'Rest',
  unheated: 'Unheated',
  cool: 'Cool',
  fermentation: 'Fermentation',
}
function Target({
  step,
  unit,
  change,
}: {
  step: RecipeStep
  unit: Unit
  change: (v: number) => void
}) {
  const [draft, setDraft] = useState<string | null>(null)
  if (step.kind === 'unheated') return <div>Heat off</div>
  const display =
    step.target_c === 0
      ? ''
      : String(Number((unit === 'F' ? step.target_c * 1.8 + 32 : step.target_c).toFixed(2)))
  return (
    <Field label={'Target (°' + unit + ')'}>
      <input
        type="number"
        step="any"
        placeholder="Heat off"
        min={unit === 'F' ? 32 : 0}
        max={unit === 'F' ? 212 : 100}
        value={draft ?? display}
        onBlur={() => setDraft(null)}
        onChange={(e) => {
          const text = e.target.value
          setDraft(text)
          change(text === '' ? 0 : unit === 'F' ? (Number(text) - 32) / 1.8 : Number(text))
        }}
      />
    </Field>
  )
}
export function RecipeSchedule({
  steps,
  model,
  unit,
  change,
}: {
  model?: string
  steps: RecipeStep[]
  unit: Unit
  change: (steps: RecipeStep[]) => void
}) {
  const [kind, setKind] = useState('mash')
  function patch(index: number, values: Partial<RecipeStep>) {
    change(steps.map((s, i) => (i === index ? { ...s, ...values } : s)))
  }
  function move(index: number, offset: number) {
    const next = [...steps]
    next.splice(index + offset, 0, ...next.splice(index, 1))
    change(next)
  }
  function add() {
    if (kind === 'whirlpool') {
      change([
        ...steps,
        {
          name: 'Cool for whirlpool',
          kind: 'cool',
          target_c: 80,
          minutes: 0,
          drain_minutes: 0,
          location: 0,
        },
        {
          name: 'Whirlpool hop stand',
          kind: 'hold',
          target_c: 80,
          minutes: 15,
          drain_minutes: 0,
          location: 0,
        },
      ])
      return
    }
    const step = {
      kind,
      name: stepKinds[kind],
      target_c:
        kind === 'unheated'
          ? 0
          : kind === 'mash-out'
            ? 76
            : ['boil', 'hop'].includes(kind)
              ? 100
              : 67,
      minutes: 10,
      drain_minutes: 0,
      location: 0,
    }
    change([...steps, step])
  }
  return (
    <section className="panel recipe-plan">
      <SectionHeading
        title="Brew schedule"
        description="The brewer runs these steps in order. For a timed hop addition, include its step here."
      />
      {steps.map((s, i) => (
        <div className="plan-row" key={i}>
          <div className="plan-fields">
            <Field label={`${i + 1}. Step name`}>
              <input value={s.name} onChange={(e) => patch(i, { name: e.target.value })} />
            </Field>
            <Target step={s} unit={unit} change={(target_c) => patch(i, { target_c })} />
            <Field
              label="Duration (min)"
              hint={['heat', 'cool'].includes(s.kind) ? '0 to run until target' : undefined}
            >
              <input
                type="number"
                min={['heat', 'cool'].includes(s.kind) ? 0 : 1}
                max="43200"
                value={s.minutes}
                onChange={(e) => patch(i, { minutes: Number(e.target.value) })}
              />
            </Field>
          </div>
          <div className="step-routing">
            <Field label="Step type">
              <select
                value={s.kind}
                onChange={(e) =>
                  patch(i, {
                    kind: e.target.value,
                    ...(e.target.value === 'unheated' ? { target_c: 0 } : {}),
                  })
                }
              >
                {Object.entries(stepKinds).map(([k, n]) => (
                  <option key={k} value={k}>
                    {n}
                  </option>
                ))}
              </select>
            </Field>
            <Field label="Flow path">
              <select
                value={s.location}
                onChange={(e) => patch(i, { location: Number(e.target.value) })}
              >
                <option value="0">Choose before transfer</option>
                {[1, 2, 3, 4, 5, 6].map((position) => (
                  <option key={position} value={position}>
                    {routeLabel(position, model || 'pico')}
                  </option>
                ))}
              </select>
            </Field>
            <Field label="Drain after step (min)">
              <input
                type="number"
                min="0"
                value={s.drain_minutes}
                onChange={(e) => patch(i, { drain_minutes: Number(e.target.value) })}
              />
            </Field>
            <div className="inline-actions">
              <IconButton label={`Move step ${i + 1} up`} disabled={!i} onClick={() => move(i, -1)}>
                <ArrowUp size={16} />
              </IconButton>
              <IconButton
                label={`Move step ${i + 1} down`}
                disabled={i === steps.length - 1}
                onClick={() => move(i, 1)}
              >
                <ArrowDown size={16} />
              </IconButton>
              <IconButton
                label={`Remove step ${i + 1}`}
                onClick={() => change(steps.filter((_, idx) => i !== idx))}
              >
                <Trash2 size={16} />
              </IconButton>
            </div>
          </div>
        </div>
      ))}
      <div className="plan-add">
        <select aria-label="New step type" value={kind} onChange={(e) => setKind(e.target.value)}>
          <option value="whirlpool">Whirlpool hop stand with cooling</option>
          {Object.entries(stepKinds).map(([k, n]) => (
            <option key={k} value={k}>
              {n}
            </option>
          ))}
        </select>
        <Button variant="secondary" onClick={add}>
          <Plus size={15} />
          Add step
        </Button>
      </div>
    </section>
  )
}
