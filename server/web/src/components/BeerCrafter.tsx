import { useEffect, useId, useState } from 'react'
import { createPortal, flushSync } from 'react-dom'
import {
  LineChart,
  Line,
  ResponsiveContainer,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
} from 'recharts'
import { Printer } from 'lucide-react'
import { useHub } from '../context'
import type { Ingredient, Recipe } from '../types'
import {
  beerStats,
  brewingReference,
  ingredientBill,
  scaleBill,
  styleInfo,
  wortCurve,
} from '../lib/beerCrafter'
import { useRecipeQuantity } from './RecipeMeasures'
import { formatNumber } from '../lib/recipeDisplay'
import { routeLabel } from '../lib/programs'
import { Button, Field } from './ui'
import './beer-crafter.scss'

export function ReferenceLists() {
  return (
    <>
      {Object.entries({
        fermentable: brewingReference.fermentables,
        hop: brewingReference.hops,
        yeast: brewingReference.yeasts,
        misc: brewingReference.misc,
      }).map(([kind, rows]) => (
        <datalist key={kind} id={'crafter-' + kind}>
          {rows.map((r, i) => (
            <option key={i} value={r.name} />
          ))}
        </datalist>
      ))}
    </>
  )
}
const colors = {
  fermentable: ['#d29342', '#c2623e', '#eed099', '#8e6650', '#c39376', '#85673c'],
  hop: ['#6e9879', '#98b567', '#39867b', '#bacb81', '#8d9877', '#738f9e'],
}
function Bill({ items, kind }: { items: Ingredient[]; kind: keyof typeof colors }) {
  const formatQuantity = useRecipeQuantity()
  const rows = ingredientBill(items, kind),
    total = rows.reduce((n, r) => n + r.amount, 0)
  const [current, select] = useState('')
  const title = kind === 'hop' ? 'Hop bill' : 'Grain bill',
    unit = kind === 'hop' ? 'g' : 'kg',
    palette = colors[kind]
  if (!total)
    return (
      <section className="bill">
        <h4>{title}</h4>
        <p className="help-text">No {kind === 'hop' ? 'hops' : 'fermentables'} entered.</p>
      </section>
    )
  let offset = 0
  const slices = rows.map((r, i) => {
    const start = offset
    offset += r.amount / total
    return { ...r, start, end: offset, color: palette[i % palette.length] }
  })
  const gradient =
    'conic-gradient(' +
    slices.map((s) => s.color + ' ' + s.start * 100 + '% ' + s.end * 100 + '%').join(',') +
    ')'
  const active = slices.find((s) => s.name === current)
  return (
    <section className="bill">
      <header>
        <h4>{title}</h4>
        <span>{formatQuantity(total, unit)}</span>
      </header>
      <div
        className="bill-pie"
        role="img"
        aria-label={title + ' by weight'}
        onPointerMove={(e) => {
          const rect = e.currentTarget.getBoundingClientRect()
          const angle =
            (Math.atan2(
              (e.clientX - rect.left) / rect.width - 0.5,
              0.5 - (e.clientY - rect.top) / rect.height
            ) /
              (2 * Math.PI) +
              1) %
            1
          select(slices.find((s) => angle < s.end)?.name ?? '')
        }}
        onPointerLeave={() => select('')}
      >
        <div className="pie-edge" style={{ background: gradient }} />
        <div className="pie-top" style={{ background: gradient }} />
      </div>
      <div className="bill-readout">
        {active ? active.name + ' · ' + formatQuantity(active.amount, unit) : 'Share by weight'}
      </div>
      <ul>
        {slices.map((s) => (
          <li key={s.name}>
            <button
              type="button"
              aria-pressed={current === s.name}
              onFocus={() => select(s.name)}
              onClick={() => select(s.name)}
              onBlur={() => select('')}
            >
              <i style={{ background: s.color }} />
              <span>{s.name}</span>
              <strong>{formatNumber((s.amount / total) * 100, 1)}%</strong>
            </button>
          </li>
        ))}
      </ul>
    </section>
  )
}
function Curve({ recipe }: { recipe: Recipe }) {
  const { unit } = useHub(),
    curve = wortCurve(recipe)
  if (!curve.points.length) return null
  const data = curve.points.map((p) => ({
    ...p,
    temp: p.c === null ? null : Number((unit === 'F' ? p.c * 1.8 + 32 : p.c).toFixed(1)),
  }))
  return (
    <section className="wort-curve">
      <header>
        <h4>Planned wort temperature</h4>
        <span>
          {Math.round(curve.minutes)} min estimated
          {curve.chill > 0 ? ' · ' + Math.round(curve.chill) + ' min cooling' : ''}
        </span>
      </header>
      <div aria-label="Planned temperature by elapsed minute" className="curve-plot">
        <ResponsiveContainer width="100%" height="100%" minWidth={0}>
          <LineChart data={data} margin={{ left: 0, right: 12, top: 10, bottom: 2 }}>
            <CartesianGrid stroke="var(--hub-line)" vertical={false} />
            <XAxis
              dataKey="minute"
              tick={{ fill: 'var(--hub-muted)' }}
              type="number"
              domain={[0, 'dataMax']}
              tickFormatter={(v) => Math.round(v) + 'm'}
              minTickGap={28}
            />
            <YAxis
              tick={{ fill: 'var(--hub-muted)' }}
              width={52}
              domain={['auto', 'auto']}
              tickFormatter={(v) => v + '°' + unit}
            />
            <Tooltip
              labelFormatter={(v) => formatNumber(Number(v), 1) + ' min'}
              formatter={(v, _name, entry) => [v + ' °' + unit, entry.payload.name]}
              contentStyle={{ background: 'var(--hub-surface)', color: 'var(--hub-ink)' }}
            />
            <Line
              dataKey="temp"
              stroke="var(--hub-wort)"
              strokeWidth={2.5}
              dot={false}
              isAnimationActive={false}
            />
          </LineChart>
        </ResponsiveContainer>
      </div>
      <small>
        Crafter timing estimate from a 55 °F start. Actual heat-up, cooling and operator pauses vary
        by machine.
      </small>
    </section>
  )
}
const labels = {
  og: 'Original gravity',
  fg: 'Final gravity',
  abv: 'Alcohol',
  ibu: 'Bitterness',
  srm: 'Color',
}
function metricValue(k: keyof typeof labels, n: number) {
  return k === 'og' || k === 'fg'
    ? n.toFixed(3)
    : formatNumber(n, 1) + (k === 'abv' ? '%' : k === 'ibu' ? ' IBU' : ' SRM')
}
function Statistics({
  recipe,
  change,
}: {
  recipe: Recipe
  change?: (items: Ingredient[]) => void
}) {
  const stats = beerStats(recipe),
    style = styleInfo(recipe.style)
  const [metric, setMetric] = useState<'og' | 'ibu'>('og'),
    [target, setTarget] = useState('')
  const scaled = target ? scaleBill(recipe, metric, Number(target)) : null
  const { profiles } = useHub()
  const model = profiles.find(
    (p) => p.id === (recipe.source_model === 'z-series' ? 'z' : recipe.source_model)
  )
  let capacity = ''
  if (scaled && model) {
    const grain = scaled
      .filter(
        (i) => i.kind === 'fermentable' && !/^(sugar|extract|dry extract)$/i.test(i.type || '')
      )
      .reduce((n, i) => n + i.amount, 0)
    if (model.max_grain_kg > 0 && grain > model.max_grain_kg + 1e-9)
      capacity = 'Target exceeds the grain limit for this brewer.'
    const groups = new Map<string, number>()
    for (const i of scaled.filter(
      (i) => i.kind === 'hop' && ['boil', 'first-wort', 'first wort'].includes(i.use.toLowerCase())
    )) {
      const group = i.use.trim().toLowerCase().replace(' ', '-') + ':' + i.minutes
      groups.set(group, (groups.get(group) ?? 0) + i.amount)
    }
    if (model.max_hop_slot_g && [...groups.values()].some((w) => w > model.max_hop_slot_g! + 1e-9))
      capacity = 'Target exceeds the hop slot limit for this brewer.'
  }
  return (
    <section className="beer-statistics">
      <h4>Estimated beer statistics</h4>
      <dl>
        {(Object.keys(labels) as (keyof typeof labels)[]).map((k) => (
          <div key={k}>
            <dt>{labels[k]}</dt>
            <dd>{stats[k] === null ? '—' : metricValue(k, stats[k])}</dd>
            {style && style[k][1] > 0 && (
              <small>
                Style {metricValue(k, style[k][0])}–{metricValue(k, style[k][1])}
              </small>
            )}
          </div>
        ))}
      </dl>
      <small>
        Gravity uses fermentable yields and efficiency. Final gravity and alcohol use yeast
        attenuation. Bitterness estimates use Tinseth for timed kettle hops. Color uses Morey.
      </small>
      {change && (
        <div className="crafter-scale">
          <Field label="Scale ingredient weights to">
            <select
              value={metric}
              onChange={(e) => {
                setMetric(e.target.value as typeof metric)
                setTarget('')
              }}
            >
              <option value="og">Target original gravity</option>
              <option value="ibu">Target bitterness</option>
            </select>
          </Field>
          <Field label={metric === 'og' ? 'Target OG' : 'Target IBU'}>
            <input
              type="number"
              step={metric === 'og' ? 0.001 : 'any'}
              min={metric === 'og' ? 1.001 : 0.1}
              value={target}
              onChange={(e) => setTarget(e.target.value)}
            />
          </Field>
          <Button
            variant="secondary"
            disabled={!scaled || !!capacity}
            onClick={() => {
              if (scaled && !capacity) {
                change(scaled)
                setTarget('')
              }
            }}
          >
            Scale {metric === 'og' ? 'fermentables' : 'boil hops'}
          </Button>
          {target && (capacity || !scaled) && (
            <span role="status">
              {capacity ||
                'Enter a positive target and complete the ingredient properties needed for this estimate.'}
            </span>
          )}
        </div>
      )}
    </section>
  )
}
function PrintSheet({ recipe }: { recipe: Recipe }) {
  const formatQuantity = useRecipeQuantity()
  const { unit } = useHub(),
    title = useId(),
    t = (c: number) => formatNumber(unit === 'F' ? c * 1.8 + 32 : c, 1) + ' °' + unit
  const stats = recipe.native_program ? null : beerStats(recipe)
  const list =
    recipe.native_program?.steps.map((s) => ({
      name: s.name,
      route: routeLabel(s.location, recipe.source_model, true),
      temp: s.target_f ? t((s.target_f - 32) / 1.8) : 'Unheated',
      hold: s.minutes,
      drain: s.drain_minutes,
    })) ??
    recipe.steps.map((s) => ({
      name: s.name,
      route: routeLabel(s.location, recipe.source_model),
      temp: s.target_c ? t(s.target_c) : 'Unheated',
      hold: s.minutes,
      drain: s.drain_minutes,
    }))
  return (
    <article className="brew-print" aria-labelledby={title}>
      <h1 id={title}>{recipe.name || 'Recipe'}</h1>
      <p>
        {[
          recipe.style,
          recipe.batch_liters ? formatQuantity(recipe.batch_liters, 'L') : '',
          recipe.description,
        ]
          .filter(Boolean)
          .join(' · ')}
      </p>
      {(recipe.ingredients?.length ?? 0) > 0 && (
        <>
          <h2>Ingredients</h2>
          <ul>
            {recipe.ingredients.map((i, k) => (
              <li key={k}>
                {formatQuantity(i.amount, i.unit)} · {i.name} · {i.use}
                {i.minutes ? ' · ' + i.minutes + ' min' : ''}
              </li>
            ))}
          </ul>
        </>
      )}
      {stats && (
        <p>
          {(Object.keys(labels) as (keyof typeof labels)[])
            .filter((k) => stats[k] !== null)
            .map((k) => labels[k] + ' ' + metricValue(k, stats[k]!))
            .join(' · ')}
        </p>
      )}
      {recipe.water && Object.keys(recipe.water).length > 0 && (
        <p>
          Water profile ·{' '}
          {Object.entries(recipe.water)
            .map(([k, v]) => (k === 'ph' ? 'pH ' + v : k + ' ' + v + ' ppm'))
            .join(' · ')}
        </p>
      )}
      <h2>Brew schedule</h2>
      <table>
        <thead>
          <tr>
            {['Step', 'Flow path', 'Target', 'Hold (min)', 'Drain (min)'].map((s) => (
              <th key={s}>{s}</th>
            ))}
          </tr>
        </thead>
        <tbody>
          {list.map((s, i) => (
            <tr key={i}>
              {[s.name, s.route, s.temp, s.hold, s.drain].map((v, j) => (
                <td key={j}>{v}</td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>
      {recipe.fermentation?.map((s, i) => (
        <p key={i}>
          {s.name} · {t(s.target_c)} · {s.days ? s.days + ' days' : 'until target is reached'}
        </p>
      ))}
      {recipe.carbonation?.volumes ? (
        <p>
          Carbonation · {recipe.carbonation.volumes} volumes CO₂
          {recipe.carbonation.method && ' · ' + recipe.carbonation.method}
        </p>
      ) : null}
      {recipe.notes && <p>{recipe.notes}</p>}
    </article>
  )
}
export function BeerCrafter({
  recipe,
  change,
}: {
  recipe: Recipe
  change?: (items: Ingredient[]) => void
}) {
  const native = !!recipe.native_program
  const [printing, setPrinting] = useState(false)
  useEffect(() => {
    const show = () => flushSync(() => setPrinting(true))
    const hide = () => setPrinting(false)
    window.addEventListener('beforeprint', show)
    window.addEventListener('afterprint', hide)
    return () => {
      window.removeEventListener('beforeprint', show)
      window.removeEventListener('afterprint', hide)
    }
  }, [])
  return (
    <section className="panel beer-crafter" aria-label="Recipe analysis">
      <header className="crafter-heading">
        <h3>Recipe analysis</h3>
        <Button variant="ghost" onClick={() => window.print()}>
          <Printer size={16} />
          Print brew sheet
        </Button>
      </header>
      {!native && (
        <>
          <div className="bill-grid">
            <Bill items={recipe.ingredients} kind="fermentable" />
            <Bill items={recipe.ingredients} kind="hop" />
          </div>
          <Statistics recipe={recipe} change={change} />
        </>
      )}
      <Curve recipe={recipe} />
      {printing && createPortal(<PrintSheet recipe={recipe} />, document.body)}
    </section>
  )
}
