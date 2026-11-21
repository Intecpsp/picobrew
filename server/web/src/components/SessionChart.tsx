import { useEffect, useMemo, useRef, useState } from 'react'
import type { BrewDetail, Sample, Unit } from '../types'
import { historyChannels, sampleReading } from '../lib/history'
import { annotations, clockTime, convertTemp } from '../lib/utils'
import { Field } from './ui'
import './session-chart.scss'

const states = [
  ['phase', 'Phase'],
  ['current_step', 'Step'],
  ['controller_status', 'Controller'],
  ['flags', 'Flags'],
  ['compartment', 'Compartment'],
  ['device_state', 'Device state'],
] as const
type StateKey = (typeof states)[number][0]
const colors = ['wort', 'block', 'target', 'phase', 'steam', 'gold', 'pink', 'leaf']
const temperatureColors: Record<string, string> = {
  wort_f: 'wort',
  thermoblock_f: 'block',
  target_f: 'target',
}
const unitLabels: Record<string, string> = {
  F: 'Temperature',
  kPa: 'Pressure',
  V: 'Voltage',
  '%': 'Output level',
  rpm: 'Speed',
  raw: 'Raw sensor readings',
}

function stateValue(field: StateKey, raw: number | null) {
  if (raw === null) return '—'
  if (field === 'current_step') return raw < 0 || raw === 255 ? '—' : String(raw + 1)
  if (field === 'flags') return '0x' + raw.toString(16).padStart(2, '0')
  return String(raw)
}
function nearest(samples: Sample[], time: number) {
  let lo = 0,
    hi = samples.length - 1
  while (lo < hi) {
    const mid = (lo + hi) >>> 1
    if (samples[mid].elapsed_ms < time) lo = mid + 1
    else hi = mid
  }
  return lo > 0 && time - samples[lo - 1].elapsed_ms < samples[lo].elapsed_ms - time ? lo - 1 : lo
}
function tickSize(span: number, count: number) {
  const target = span / count
  const power = 10 ** Math.floor(Math.log10(target))
  return ([1, 2, 5, 10].find((n) => n * power >= target) || 10) * power
}

// The caller supplies at least one recorded sample.
export function SessionChart({
  brew,
  unit,
  notes,
  onSelect,
}: {
  brew: BrewDetail
  unit: Unit
  notes: string
  onSelect: (elapsed: number | null) => void
}) {
  const plot = useRef<HTMLDivElement>(null)
  const [width, setWidth] = useState(640)
  const [group, setGroup] = useState('F')
  const [hidden, setHidden] = useState<string[]>([])
  const [hover, setHover] = useState<number | null>(null)
  const [pinned, setPinned] = useState<{ index: number; time: number } | null>(null)
  useEffect(() => {
    const el = plot.current
    if (!el) return
    const resize = () => {
      if (el.clientWidth) setWidth(el.clientWidth)
    }
    resize()
    const observer = new ResizeObserver(resize)
    observer.observe(el)
    return () => observer.disconnect()
  }, [])
  const samples = useMemo(
    () => [...brew.samples].sort((a, b) => a.elapsed_ms - b.elapsed_ms),
    [brew.samples]
  )
  const channels = useMemo(
    () => historyChannels(brew),
    [brew.samples, brew.sample_fields, brew.field_metadata, brew.model]
  )
  const units = [...new Set(channels.map((c) => c.metadata.unit))]
  const axisUnit = units.includes(group) ? group : units[0] || 'F'
  const displayUnit = axisUnit === 'F' ? '°' + unit : axisUnit
  const series = useMemo(
    () =>
      channels
        .filter((c) => c.metadata.unit === axisUnit)
        .map((c, i) => ({
          ...c,
          color: 'var(--hub-' + (temperatureColors[c.field] || colors[i % colors.length]) + ')',
          values: samples.map((s) =>
            axisUnit === 'F'
              ? convertTemp(sampleReading(s, c.field), unit)
              : sampleReading(s, c.field)
          ),
        })),
    [samples, channels, axisUnit, unit]
  )
  const accessory = ['picostill', 'picoferm'].includes(brew.model)
  const gapLimit =
    brew.gap_count && brew.sample_interval_ms ? brew.sample_interval_ms * 1.5 : Infinity
  const lanes = useMemo(
    () =>
      states
        .filter(([key]) => !accessory || key === 'device_state' || key === 'flags')
        .map(([key, label]) => {
          const runs: { start: number; end: number; value: number | null }[] = []
          for (const [i, s] of samples.entries()) {
            const value = sampleReading(s, key)
            const gap = i > 0 && s.elapsed_ms - samples[i - 1].elapsed_ms > gapLimit
            const last = runs[runs.length - 1]
            if (last && !gap) last.end = s.elapsed_ms
            if (!last || gap || last.value !== value)
              runs.push({ start: s.elapsed_ms, end: s.elapsed_ms, value })
          }
          return { key, label, runs }
        }),
    [samples, accessory, gapLimit]
  )

  const left = width < 480 ? 92 : 106,
    right = Math.max(left + 1, width - 12),
    top = 18,
    bottom = 210,
    laneY = bottom + 16
  const axisY = laneY + lanes.length * 25 + 4,
    height = axisY + 24
  const first = samples[0].elapsed_ms,
    end = samples[samples.length - 1].elapsed_ms,
    span = Math.max(1, end - first)
  const x = (time: number) => left + ((time - first) / span) * (right - left)
  const { curves, ticks, y } = useMemo(() => {
    let low = Infinity,
      high = -Infinity
    for (const c of series)
      if (!hidden.includes(c.field))
        for (const v of c.values)
          if (v !== null) {
            low = Math.min(low, v)
            high = Math.max(high, v)
          }
    if (!Number.isFinite(low)) {
      low = 0
      high = 1
    }
    if (high === low) {
      const pad = Math.max(1, Math.abs(low) * 0.02)
      low -= pad
      high += pad
    }
    const increment = tickSize(high - low, 4)
    low = Math.floor(low / increment) * increment
    high = Math.ceil(high / increment) * increment
    const y = (v: number) => bottom - ((v - low) / (high - low)) * (bottom - top)
    const ticks = Array.from(
      { length: Math.round((high - low) / increment) + 1 },
      (_, i) => low + i * increment
    )
    const curves = series
      .filter((c) => !hidden.includes(c.field))
      .map((c) => {
        let path = '',
          previous = false
        const dots: { x: number; y: number }[] = []
        const step = c.field === 'target_f' || c.field.startsWith('actuator_')
        for (const [i, v] of c.values.entries()) {
          if (v === null) {
            previous = false
            continue
          }
          const separated = i > 0 && samples[i].elapsed_ms - samples[i - 1].elapsed_ms > gapLimit
          const px = x(samples[i].elapsed_ms),
            py = y(v)
          if (!previous || separated) {
            path += 'M' + px.toFixed(2) + ',' + py.toFixed(2)
            if (
              c.values[i + 1] == null ||
              samples[i + 1].elapsed_ms - samples[i].elapsed_ms > gapLimit
            )
              dots.push({ x: px, y: py })
          } else
            path += step
              ? 'H' + px.toFixed(2) + 'V' + py.toFixed(2)
              : 'L' + px.toFixed(2) + ',' + py.toFixed(2)
          previous = true
        }
        return { ...c, path, dots }
      })
    return { curves, ticks, y }
  }, [series, hidden, samples, gapLimit, left, right, first, span])
  const active =
    pinned && samples[pinned.index]?.elapsed_ms === pinned.time
      ? pinned.index
      : nearest(samples, pinned?.time ?? hover ?? end)
  const sample = samples[active]
  const inspect = pinned !== null || hover !== null
  const cursorX = x(sample.elapsed_ms)
  const pin = (index: number) => {
    const time = samples[index].elapsed_ms
    setPinned({ index, time })
    onSelect(time)
  }
  const clear = () => {
    setPinned(null)
    setHover(null)
    onSelect(null)
  }
  function point(clientX: number) {
    const px = clientX - (plot.current?.getBoundingClientRect().left || 0)
    return nearest(samples, first + ((px - left) / (right - left)) * span)
  }
  const tickCount = Math.max(1, Math.floor((right - left) / 83))
  const timeStep =
    ([1, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 14400, 86400].find(
      (v) => v * 1000 >= span / tickCount
    ) ?? Math.ceil(span / tickCount / 86400000) * 86400) * 1000
  const times = [first]
  for (let t = (Math.floor(first / timeStep) + 1) * timeStep; t < end; t += timeStep)
    if (x(t) - left > 75 && right - x(t) > 75) times.push(t)
  if (times.length === 1 && end - first >= 2000 && right - left >= 180)
    times.push(first + Math.round(span / 2000) * 1000)
  if (end > first) times.push(end)
  const notesOnChart = annotations(notes).filter(
    (n) => n.elapsed_ms >= first && n.elapsed_ms <= end
  )
  const suffix =
    displayUnit.startsWith('°') || displayUnit === '%' ? displayUnit : ' ' + displayUnit
  const fmt = (v: number | null) =>
    v == null
      ? '—'
      : v.toLocaleString(undefined, { maximumFractionDigits: axisUnit === 'F' ? 1 : 3 })
  const readout = [
    clockTime(sample.elapsed_ms),
    ...series.map((c) => c.metadata.label + ' ' + fmt(c.values[active]) + ' ' + displayUnit),
    ...lanes.map((l) => l.label + ' ' + stateValue(l.key, sampleReading(sample, l.key))),
  ].join(', ')
  return (
    <div className="session-chart">
      {units.length > 1 && (
        <Field label="Recorded measurements">
          <select value={axisUnit} onChange={(e) => setGroup(e.target.value)}>
            {units.map((u) => (
              <option key={u} value={u}>
                {unitLabels[u] || u}
              </option>
            ))}
          </select>
        </Field>
      )}
      <div className="session-chart-toolbar">
        <div className="session-series" role="group" aria-label="Visible measurements">
          {series.map((c) => (
            <button
              type="button"
              key={c.field}
              aria-pressed={!hidden.includes(c.field)}
              aria-label={c.metadata.label}
              onClick={() =>
                setHidden((old) =>
                  old.includes(c.field) ? old.filter((f) => f !== c.field) : [...old, c.field]
                )
              }
            >
              <i
                style={{
                  borderColor: c.color,
                  borderStyle: c.field === 'target_f' ? 'dashed' : 'solid',
                }}
              />
              {c.metadata.label}{' '}
              <strong>
                {fmt(c.values[active])}
                {c.values[active] != null && suffix}
              </strong>
            </button>
          ))}
        </div>
        <span>
          {pinned !== null ? 'Pinned' : hover !== null ? 'At' : 'Last sample'}{' '}
          <strong>{clockTime(sample.elapsed_ms)}</strong>{' '}
          {pinned !== null && (
            <button type="button" onClick={clear}>
              Unpin
            </button>
          )}
        </span>
      </div>
      <div
        className="session-plot"
        ref={plot}
        role="slider"
        tabIndex={0}
        aria-label="Inspect recorded samples"
        aria-valuemin={1}
        aria-valuemax={samples.length}
        aria-valuenow={active + 1}
        aria-valuetext={readout}
        onPointerMove={(e) => {
          if (pinned === null && e.pointerType !== 'touch')
            setHover(samples[point(e.clientX)].elapsed_ms)
        }}
        onPointerLeave={() => setHover(null)}
        onClick={(e) => pin(point(e.clientX))}
        onKeyDown={(e) => {
          let index = active
          switch (e.key) {
            case 'ArrowLeft':
            case 'ArrowDown':
              index--
              break
            case 'ArrowRight':
            case 'ArrowUp':
              index++
              break
            case 'Home':
              index = 0
              break
            case 'End':
              index = samples.length - 1
              break
            case ' ':
            case 'Enter':
              break
            case 'Escape':
              clear()
              return
            default:
              return
          }
          e.preventDefault()
          pin(Math.max(0, Math.min(samples.length - 1, index)))
        }}
      >
        <svg viewBox={`0 0 ${width} ${height}`} width="100%" height={height} aria-hidden="true">
          <text x="0" y={top} className="plot-unit">
            {displayUnit}
          </text>
          {ticks.map((v) => (
            <g key={v}>
              <line x1={left} x2={right} y1={y(v)} y2={y(v)} className="plot-grid" />
              <text x={left - 9} y={y(v) + 4} textAnchor="end">
                {fmt(v)}
              </text>
            </g>
          ))}
          {curves.map((c) => (
            <g key={c.field} data-channel={c.field}>
              <path
                d={c.path}
                fill="none"
                stroke={c.color}
                strokeWidth={2}
                strokeDasharray={c.field === 'target_f' ? '5 4' : undefined}
              />
              {c.dots.map((d, i) => (
                <circle key={i} cx={d.x} cy={d.y} r={2.3} fill={c.color} />
              ))}
            </g>
          ))}
          {notesOnChart.map((note, i) => (
            <g key={i}>
              <title>
                {clockTime(note.elapsed_ms)} · {note.label}
              </title>
              <line
                x1={x(note.elapsed_ms)}
                x2={x(note.elapsed_ms)}
                y1={top}
                y2={bottom}
                stroke="var(--hub-phase)"
                strokeDasharray="2 4"
              />
              <circle cx={x(note.elapsed_ms)} cy={top} r={3} fill="var(--hub-phase)" />
            </g>
          ))}
          {lanes.map((lane, li) => (
            <g key={lane.key} data-state={lane.key} transform={`translate(0,${laneY + li * 25})`}>
              <text x={0} y={14}>
                {lane.label}
              </text>
              {lane.runs.map((r, i) => {
                const w = Math.max(1, x(r.end) - x(r.start))
                const label = stateValue(lane.key, r.value)
                return (
                  <g key={i}>
                    <rect
                      x={x(r.start)}
                      y={0}
                      width={w}
                      height={20}
                      fill={i % 2 ? 'var(--hub-accent-bg)' : 'var(--hub-inset)'}
                    />
                    <line x1={x(r.start)} x2={x(r.start)} y1={1} y2={19} stroke="var(--hub-edge)" />
                    {w > label.length * 8 + 14 && (
                      <text x={x(r.start) + 6} y={14}>
                        {label}
                      </text>
                    )}
                  </g>
                )
              })}
            </g>
          ))}
          {inspect && (
            <g>
              <line
                x1={cursorX}
                x2={cursorX}
                y1={top}
                y2={axisY}
                stroke="var(--hub-ink)"
                strokeDasharray="3 3"
              />
              {curves.map((c) => {
                const v = c.values[active]
                return v === null ? null : (
                  <circle
                    key={c.field}
                    cx={cursorX}
                    cy={y(v)}
                    r={4}
                    fill={c.color}
                    stroke="var(--hub-surface)"
                    strokeWidth={1.5}
                  />
                )
              })}
            </g>
          )}
          <text x={0} y={axisY + 16}>
            Elapsed
          </text>
          {times.map((t) => (
            <text
              key={t}
              x={x(t)}
              y={axisY + 16}
              textAnchor={t === first ? 'start' : t === end ? 'end' : 'middle'}
            >
              {clockTime(t)}
            </text>
          ))}
        </svg>
      </div>
      <div className="session-chart-detail">
        {inspect ? (
          <>
            {lanes.map((l) => (
              <span key={l.key}>
                {l.label} <b>{stateValue(l.key, sampleReading(sample, l.key))}</b>
              </span>
            ))}
            {notesOnChart
              .filter((n) => nearest(samples, n.elapsed_ms) === active)
              .map((n, i) => (
                <span key={i}>{n.label}</span>
              ))}
          </>
        ) : (
          <span>{samples.length.toLocaleString()} recorded samples</span>
        )}
      </div>
    </div>
  )
}
