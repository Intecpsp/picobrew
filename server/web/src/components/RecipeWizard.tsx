import { MeasureChoice, QuantityField, useRecipeQuantity } from './RecipeMeasures'
import { BeerCrafter, ReferenceLists } from './BeerCrafter'
import { useEffect, useRef, useState } from 'react'
import { ArrowLeft, ArrowRight, Plus, Save, SlidersHorizontal, Trash2 } from 'lucide-react'
import { useHub } from '../context'
import type { Recipe, Unit } from '../types'
import {
  grainOverLimit,
  basketWeight,
  isKegAddition,
  totalWeight,
  hopLimit,
  slotTimes,
  hopRoute,
  usesPicoPakRouting,
  type HopAddition,
  guidedRecipe,
  guideProblem,
  initialGuide,
  mashSchedule,
  type GuidedDraft,
  type Grain,
  type MashRest,
} from '../lib/guidedRecipe'
import { modelLabel, newRecipe } from '../lib/utils'
import { Button, ErrorBanner, Field, IconButton, PageTitle } from './ui'
import './recipe-wizard.scss'

const stages = ['Brewer', 'Ingredients', 'Mash', 'Review']
const knownModels = ['pico-s', 'pico-pro', 'pico-c', 'z', 'zymatic']
const profileID = (model: string) => (model === 'z-series' ? 'z' : model)

function MashTemperature({
  rest,
  unit,
  change,
}: {
  rest: MashRest
  unit: Unit
  change: (value: number) => void
}) {
  const [editing, setEditing] = useState<string | null>(null)
  const value = unit === 'F' ? rest.celsius * 1.8 + 32 : rest.celsius
  return (
    <Field label={`Mash temperature (°${unit})`}>
      <input
        type="number"
        step="any"
        value={editing ?? (Number.isFinite(value) ? Number(value.toFixed(2)) : '')}
        onBlur={() => setEditing(null)}
        onChange={(e) => {
          setEditing(e.target.value)
          const n = e.target.valueAsNumber
          change(unit === 'F' ? (n - 32) / 1.8 : n)
        }}
      />
    </Field>
  )
}

function IngredientFields({
  item,
  hop = false,
  change,
  maxWeight,
  invalid,
}: {
  item: Grain
  maxWeight?: number
  invalid?: boolean
  hop?: boolean
  change: (patch: Partial<Grain>) => void
}) {
  return (
    <>
      <Field label={hop ? 'Hop variety' : 'Grain'}>
        <input
          list={hop ? 'crafter-hop' : 'crafter-fermentable'}
          value={item.name}
          placeholder={hop ? 'e.g. Cascade' : 'e.g. Pale malt'}
          onChange={(e) => change({ name: e.target.value })}
        />
      </Field>
      <QuantityField
        label="Weight"
        unit={hop ? 'g' : 'kg'}
        value={item.amount}
        max={maxWeight}
        invalid={invalid}
        change={(v) => change({ amount: v })}
      />
    </>
  )
}

export function RecipeWizard({
  busy,
  error,
  save,
  advanced,
  back,
}: {
  busy: boolean
  error: string
  save: (recipe: Recipe) => void
  advanced: (recipe: Recipe) => void
  back: () => void
}) {
  const hub = useHub()
  const formatQuantity = useRecipeQuantity()
  const profiles = hub.profiles.filter((p) => knownModels.includes(p.id))
  const [selected, setSelected] = useState('')
  const available = hub.devices.filter((d) => profiles.some((p) => p.id === profileID(d.model)))
  const preferred = available.find((d) => d.online) ?? available[0]
  const profile =
    profiles.find((p) => p.id === (selected || (preferred && profileID(preferred.model)))) ??
    profiles[0]
  const [draft, setDraft] = useState(initialGuide)
  const [selectedSlot, setSelectedSlot] = useState<number | null>(null)
  const [stage, setStage] = useState(0)
  const [problem, setProblem] = useState('')
  const heading = useRef<HTMLHeadingElement>(null)
  const entered = useRef(false)
  function patch(values: Partial<GuidedDraft>) {
    entered.current = true
    if (profile && !selected) setSelected(profile.id)
    setProblem('')
    setDraft((d) => ({ ...d, ...values }))
  }
  useEffect(() => {
    if (entered.current) heading.current?.focus()
  }, [stage])
  const recipe = profile && guidedRecipe(draft, profile)
  function next() {
    if (!profile || !recipe) return
    const message = guideProblem(draft, profile, stage)
    if (message) {
      setProblem(message)
      return
    }
    if (!selected) setSelected(profile.id)
    if (stage === 3) save(recipe)
    else setStage(stage + 1)
  }
  const overGrain = profile && grainOverLimit(draft.grains, profile)
  const slots = Array.from({ length: profile ? hopLimit(profile) : 0 }, (_, i) => i + 1)
  const times = selectedSlot === null ? [] : slotTimes(draft.hops, selectedSlot, Number(draft.boil))
  const savedSlot = draft.hops.find((h) => h.slot === selectedSlot)
  const preferredMinutes = [Number(draft.boil), 30, 15, 5]
  const current: HopAddition | undefined =
    savedSlot ??
    (selectedSlot !== null && times.length
      ? {
          slot: selectedSlot,
          minutes: String(times.find((t) => t <= preferredMinutes[selectedSlot - 1]) ?? times[0]),
          varieties: [{ name: '', amount: '' }],
        }
      : undefined)
  const returns = current?.returns ?? []
  const nextReturn = savedSlot
    ? slotTimes(draft.hops, savedSlot.slot, Number(draft.boil), returns.length + 1)[0]
    : undefined
  const stepLimit = Math.min(15, profile?.max_steps || 15)
  const route = hopRoute(draft.hops)
  const slotLimit = profile?.max_hop_slot_g || 0
  const overweightSlot = draft.hops.find(
    (h) => slotLimit > 0 && totalWeight(h.varieties) > slotLimit + 1e-9
  )
  function updateAddition(values: Partial<HopAddition>) {
    if (!current) return
    const changed = { ...current, ...values }
    patch({
      hops: savedSlot
        ? draft.hops.map((h) => (h.slot === current.slot ? changed : h))
        : [...draft.hops, changed],
    })
  }
  return (
    <>
      <ReferenceLists />
      <PageTitle
        title="Create a recipe"
        back={() => {
          if (!entered.current || window.confirm('Leave this recipe without saving your changes?'))
            back()
        }}
        actions={
          <Button
            variant="secondary"
            disabled={busy}
            onClick={() => advanced(entered.current && recipe ? recipe : newRecipe())}
          >
            <SlidersHorizontal size={16} /> Advanced editor
          </Button>
        }
      />
      <section className="panel guide-panel" aria-label="Recipe wizard">
        <ol className="guide-progress">
          {stages.map((label, i) => (
            <li key={label} aria-current={stage === i ? 'step' : undefined}>
              {i < stage ? (
                <button
                  type="button"
                  disabled={busy}
                  onClick={() => {
                    setProblem('')
                    setStage(i)
                  }}
                >
                  {label}
                </button>
              ) : (
                label
              )}
            </li>
          ))}
        </ol>
        <h2 ref={heading} tabIndex={-1}>
          {
            [
              'Choose your brewer',
              'Build the grain bill and hop additions',
              'Set the mash rests',
              'Review your recipe',
            ][stage]
          }
        </h2>
        <ErrorBanner message={problem || error} />
        {profile && recipe ? (
          <>
            {stage === 0 && (
              <>
                <Field label="Recipe name">
                  <input
                    maxLength={100}
                    value={draft.name}
                    placeholder="e.g. Weekend pale ale"
                    onChange={(e) => patch({ name: e.target.value })}
                  />
                </Field>
                <div className="guide-fields">
                  <Field label="Brewer">
                    <select
                      value={profile.id}
                      onChange={(e) => {
                        patch({ batch: '', ...(draft.customMash ? {} : { rests: [] }) })
                        setSelected(e.target.value)
                      }}
                    >
                      {profiles.map((p) => (
                        <option key={p.id} value={p.id}>
                          {modelLabel(p.id)}
                          {p.id === 'z'
                            ? ' · one brewing module'
                            : p.id === 'zymatic'
                              ? ' · planning only'
                              : ''}
                        </option>
                      ))}
                    </select>
                  </Field>
                  <QuantityField
                    label="Batch size"
                    unit="L"
                    value={draft.batch}
                    placeholder={profile.batch_liters}
                    max={profile.batch_liters}
                    change={(v) => patch({ batch: v })}
                  />
                  <MeasureChoice />
                </div>
                <Field label="Beer style">
                  <input
                    list="crafter-style"
                    value={draft.style}
                    onChange={(e) => patch({ style: e.target.value })}
                  />
                </Field>
                {usesPicoPakRouting(profile.id) && (
                  <Button
                    variant="secondary"
                    onClick={() => {
                      if (
                        !entered.current ||
                        window.confirm('Leave this draft and browse original PicoPaks?')
                      )
                        hub.navigate(
                          '/catalog?source=picopak-library&kind=native-program&model=' +
                            encodeURIComponent(profile.id)
                        )
                    }}
                  >
                    Start from an original PicoPak
                  </Button>
                )}
              </>
            )}
            {stage === 1 && (
              <>
                <div className="guide-group">
                  <h3>Grain bill</h3>
                  {profile.max_grain_kg > 0 && (
                    <p className="help-text">
                      {modelLabel(profile.id)} grain limit ·{' '}
                      {formatQuantity(profile.max_grain_kg, 'kg')}
                    </p>
                  )}
                  {overGrain && (
                    <p className="notice" role="alert">
                      Your grain bill totals {formatQuantity(basketWeight(draft.grains), 'kg')}.
                      Reduce it to {formatQuantity(profile.max_grain_kg, 'kg')} or less for{' '}
                      {modelLabel(profile.id)}.
                    </p>
                  )}
                  {draft.grains.map((grain, i) => (
                    <div className="guide-row" key={i}>
                      <IngredientFields
                        item={grain}
                        invalid={overGrain}
                        maxWeight={
                          !isKegAddition(grain) && profile.max_grain_kg
                            ? Math.max(
                                0,
                                profile.max_grain_kg -
                                  basketWeight(draft.grains.filter((_, j) => i !== j))
                              )
                            : undefined
                        }
                        change={(values) =>
                          patch({
                            grains: draft.grains.map((g, j) => (j === i ? { ...g, ...values } : g)),
                          })
                        }
                      />
                      <IconButton
                        label={`Remove grain ${i + 1}`}
                        onClick={() => patch({ grains: draft.grains.filter((_, j) => i !== j) })}
                      >
                        <Trash2 size={16} />
                      </IconButton>
                    </div>
                  ))}
                  <Button
                    variant="ghost"
                    onClick={() => patch({ grains: [...draft.grains, { name: '', amount: '' }] })}
                  >
                    <Plus size={16} />
                    Add grain
                  </Button>
                </div>
                <div className="guide-group">
                  <h3>Boil and hops</h3>
                  <p className="help-text">
                    Load each slot once. Set when wort flows directly into it, including any return
                    visits.
                  </p>
                  <Field label="Boil time (min)">
                    <input
                      type="number"
                      min="1"
                      max="599"
                      value={draft.boil}
                      onChange={(e) => patch({ boil: e.target.value })}
                    />
                  </Field>
                  <div className="hop-slots" role="group" aria-label="Hop slots">
                    {slots.map((slot) => {
                      const addition = draft.hops.find((h) => h.slot === slot)
                      return (
                        <button
                          type="button"
                          key={slot}
                          aria-label={`Hop slot ${slot}`}
                          aria-pressed={slot === selectedSlot}
                          disabled={
                            !addition && !slotTimes(draft.hops, slot, Number(draft.boil)).length
                          }
                          onClick={() => setSelectedSlot(slot)}
                        >
                          <strong>{slot}</strong>
                          <span>{addition ? `${addition.minutes} min` : 'Empty'}</span>
                        </button>
                      )
                    })}
                  </div>
                  {current && (
                    <fieldset className="guide-addition">
                      <legend>Hop slot {current.slot}</legend>
                      <div className="addition-header">
                        <Field label="Minutes before boil ends">
                          <select
                            value={current.minutes}
                            onChange={(e) => updateAddition({ minutes: e.target.value })}
                          >
                            {!times.includes(Number(current.minutes)) && (
                              <option value={current.minutes} disabled>
                                Choose a valid time
                              </option>
                            )}
                            {times.map((t) => (
                              <option key={t} value={t}>
                                {t}
                              </option>
                            ))}
                          </select>
                        </Field>
                        <Button
                          variant="ghost"
                          onClick={() => {
                            patch({ hops: draft.hops.filter((h) => h.slot !== current.slot) })
                            setSelectedSlot(null)
                          }}
                        >
                          Clear slot
                        </Button>
                      </div>
                      {returns.map((minutes, index) => {
                        const choices = slotTimes(
                          draft.hops,
                          current.slot,
                          Number(draft.boil),
                          index + 1
                        )
                        return (
                          <div key={index} className="addition-header">
                            <Field label={`Return ${index + 1} (minutes before boil ends)`}>
                              <select
                                value={minutes}
                                onChange={(e) =>
                                  updateAddition({
                                    returns: returns.map((value, i) =>
                                      i === index ? e.target.value : value
                                    ),
                                  })
                                }
                              >
                                {!choices.includes(Number(minutes)) && (
                                  <option value={minutes} disabled>
                                    Choose a valid time
                                  </option>
                                )}
                                {choices.map((t) => (
                                  <option key={t} value={t}>
                                    {t}
                                  </option>
                                ))}
                              </select>
                            </Field>
                            <IconButton
                              label={`Remove return ${index + 1}`}
                              onClick={() =>
                                updateAddition({ returns: returns.filter((_, i) => i !== index) })
                              }
                            >
                              <Trash2 size={16} />
                            </IconButton>
                          </div>
                        )
                      })}
                      {savedSlot && (
                        <Button
                          variant="ghost"
                          disabled={nextReturn === undefined || recipe.steps.length >= stepLimit}
                          onClick={() =>
                            updateAddition({ returns: [...returns, String(nextReturn)] })
                          }
                        >
                          <Plus size={16} />
                          Add return visit
                        </Button>
                      )}
                      {savedSlot && recipe.steps.length >= stepLimit && (
                        <p className="help-text">All {stepLimit} program steps are in use.</p>
                      )}
                      {current.varieties.map((v, j) => (
                        <div key={j} className="guide-row">
                          <IngredientFields
                            item={v}
                            hop
                            invalid={
                              !!slotLimit && totalWeight(current.varieties) > slotLimit + 1e-9
                            }
                            maxWeight={
                              slotLimit
                                ? Math.max(
                                    0,
                                    slotLimit -
                                      totalWeight(current.varieties.filter((_, k) => k !== j))
                                  )
                                : undefined
                            }
                            change={(values) =>
                              updateAddition({
                                varieties: current.varieties.map((old, k) =>
                                  j === k ? { ...old, ...values } : old
                                ),
                              })
                            }
                          />
                          {current.varieties.length > 1 && (
                            <IconButton
                              label={`Remove variety ${j + 1}`}
                              onClick={() =>
                                updateAddition({
                                  varieties: current.varieties.filter((_, k) => j !== k),
                                })
                              }
                            >
                              <Trash2 size={16} />
                            </IconButton>
                          )}
                        </div>
                      ))}
                      <Button
                        variant="ghost"
                        onClick={() =>
                          updateAddition({
                            varieties: [...current.varieties, { name: '', amount: '' }],
                          })
                        }
                      >
                        <Plus size={16} />
                        Add variety
                      </Button>
                      <span className="help-text">
                        {' '}
                        {formatQuantity(totalWeight(current.varieties), 'g')} of {slotLimit} g
                      </span>
                    </fieldset>
                  )}
                  {route.length > 1 && (
                    <p>
                      <strong>Boil route</strong> ·{' '}
                      {route
                        .map((visit) => `Slot ${visit.slot} (${visit.minutes} min)`)
                        .join(' → ')}
                    </p>
                  )}
                  {route.length > 0 && route[0].minutes === Number(draft.boil) && (
                    <label className="check">
                      <input
                        type="checkbox"
                        checked={draft.firstWort}
                        onChange={(e) => patch({ firstWort: e.target.checked })}
                      />{' '}
                      First wort hopping · heat through slot {route[0].slot} on the way to the boil
                    </label>
                  )}
                  {overweightSlot && (
                    <p className="notice" role="alert">
                      Reduce the combined hop weight in slot {overweightSlot.slot} to {slotLimit} g
                      or less.
                    </p>
                  )}
                </div>
              </>
            )}
            {stage === 1 && (
              <div className="guide-fields">
                <Field label="Yeast (optional)">
                  <input
                    list="crafter-yeast"
                    value={draft.yeast}
                    onChange={(e) => patch({ yeast: e.target.value })}
                  />
                </Field>
                <QuantityField
                  label="Yeast amount"
                  unit="g"
                  value={draft.yeastGrams}
                  change={(v) => patch({ yeastGrams: v })}
                />
              </div>
            )}
            {stage === 2 && (
              <div className="guide-group">
                <Field label="Mash schedule">
                  <select
                    value={draft.customMash ? 'custom' : draft.mashPreset}
                    onChange={(e) =>
                      patch({
                        customMash: e.target.value === 'custom',
                        mashPreset: e.target.value === 'custom' ? draft.mashPreset : e.target.value,
                        rests:
                          e.target.value !== 'custom' || draft.rests.length
                            ? draft.rests
                            : mashSchedule(draft, profile).map(({ celsius, minutes }) => ({
                                celsius,
                                minutes,
                              })),
                      })
                    }
                  >
                    <option value="standard">
                      {profile.id === 'z' || profile.id === 'zymatic'
                        ? 'Single infusion'
                        : 'PicoPak step mash'}
                    </option>
                    {profile.id.startsWith('pico-') && (
                      <option value="infusion">Single infusion</option>
                    )}
                    <option value="mash-out">Single infusion with mash out</option>
                    <option value="high-efficiency">High efficiency multi-step</option>
                    <option value="custom">Custom mash</option>
                  </select>
                </Field>
                {!draft.customMash && (
                  <ol>
                    {mashSchedule(draft, profile).map((rest) => (
                      <li key={rest.name}>
                        <strong>{rest.name}</strong> ·{' '}
                        {Number(
                          (hub.unit === 'F' ? rest.celsius * 1.8 + 32 : rest.celsius).toFixed(1)
                        )}{' '}
                        °{hub.unit} · {rest.minutes} min
                      </li>
                    ))}
                  </ol>
                )}
                {draft.customMash &&
                  draft.rests.map((rest, i) => (
                    <div className="guide-row" key={i}>
                      <MashTemperature
                        rest={rest}
                        unit={hub.unit}
                        change={(celsius) =>
                          patch({
                            rests: draft.rests.map((r, j) => (i === j ? { ...r, celsius } : r)),
                          })
                        }
                      />
                      <Field label={`Hold time (min)`}>
                        <input
                          type="number"
                          min="1"
                          max="599"
                          value={rest.minutes}
                          onChange={(e) =>
                            patch({
                              rests: draft.rests.map((r, j) =>
                                i === j ? { ...r, minutes: e.target.value } : r
                              ),
                            })
                          }
                        />
                      </Field>
                      <IconButton
                        label={`Remove mash rest ${i + 1}`}
                        disabled={draft.rests.length === 1}
                        onClick={() => patch({ rests: draft.rests.filter((_, j) => i !== j) })}
                      >
                        <Trash2 size={16} />
                      </IconButton>
                    </div>
                  ))}
                {draft.customMash && (
                  <Button
                    variant="ghost"
                    onClick={() =>
                      patch({ rests: [...draft.rests, { celsius: 76, minutes: '10' }] })
                    }
                  >
                    <Plus size={16} />
                    Add mash rest
                  </Button>
                )}
              </div>
            )}
            {stage === 3 && (
              <>
                <h3>{recipe.name}</h3>
                <p>
                  {modelLabel(profile.id)} · {formatQuantity(recipe.batch_liters, 'L')}
                  {!profile.control_supported ? ' · Planning only' : ''}
                </p>
                <BeerCrafter recipe={recipe} />
                <div className="guide-review">
                  <div>
                    <h3>Ingredients by compartment</h3>
                    <dl>
                      <dt>Grain compartment</dt>
                      <dd>
                        {draft.grains
                          .filter((g) => !isKegAddition(g))
                          .map((g) => g.name + ' · ' + formatQuantity(Number(g.amount), 'kg'))
                          .join(', ')}
                      </dd>
                      {draft.grains.some(isKegAddition) && (
                        <>
                          <dt>Keg additions</dt>
                          <dd>
                            {draft.grains
                              .filter(isKegAddition)
                              .map((g) => g.name + ' · ' + formatQuantity(Number(g.amount), 'kg'))
                              .join(', ')}
                          </dd>
                        </>
                      )}
                      {[...draft.hops]
                        .sort((a, b) => Number(b.minutes) - Number(a.minutes))
                        .map((addition) => (
                          <div key={addition.minutes}>
                            <dt>
                              Hop slot {addition.slot} · {addition.minutes} min before boil ends
                            </dt>
                            <dd>
                              {addition.varieties
                                .map((h) => h.name + ' · ' + formatQuantity(Number(h.amount), 'g'))
                                .join(', ')}
                            </dd>
                          </div>
                        ))}
                    </dl>
                  </div>
                  <div>
                    <h3>Brew schedule</h3>
                    <ol>
                      {recipe.steps.map((s, i) => (
                        <li key={i}>
                          <strong>{s.name}</strong> ·{' '}
                          {Math.round(hub.unit === 'F' ? s.target_c * 1.8 + 32 : s.target_c)}°
                          {hub.unit}
                          {s.minutes > 0 ? ` · ${s.minutes} min` : ' · until heated'}
                          {s.drain_minutes > 0 ? ` · drain ${s.drain_minutes} min` : ''}
                        </li>
                      ))}
                    </ol>
                  </div>
                </div>
              </>
            )}
            <footer>
              {stage > 0 && (
                <Button
                  variant="secondary"
                  disabled={busy}
                  onClick={() => {
                    setProblem('')
                    setStage(stage - 1)
                  }}
                >
                  <ArrowLeft size={16} />
                  Previous
                </Button>
              )}
              <Button
                disabled={busy || (stage === 1 && (overGrain || !!overweightSlot))}
                onClick={next}
              >
                {stage === 3 ? (
                  <>
                    <Save size={16} />
                    {busy ? 'Saving…' : 'Save recipe'}
                  </>
                ) : (
                  <>
                    Continue
                    <ArrowRight size={16} />
                  </>
                )}
              </Button>
            </footer>
          </>
        ) : (
          <p>Brewer profiles are unavailable. You can create a recipe in the advanced editor.</p>
        )}
      </section>
    </>
  )
}
