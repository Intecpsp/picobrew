import { useState } from 'react'
import {
  CartesianGrid,
  Line,
  LineChart,
  ReferenceLine,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from 'recharts'
import type { BrewDetail, Unit } from '../types'
import { annotations, clockTime } from '../lib/utils'
import { historyChannels, sampleReading } from '../lib/history'
import { sensorValue } from '../lib/capabilities'
import { Field } from './ui'

const colors = [
  '#b46b40',
  '#597b6b',
  '#77769a',
  '#227b9a',
  '#a37b25',
  '#ba4e74',
  '#557e46',
  '#6e6a9d',
]
export function SensorHistory({
  brew,
  unit,
  height = 280,
  notes = '',
  onSelect,
}: {
  brew: BrewDetail
  unit: Unit
  height?: number
  notes?: string
  onSelect?: (elapsed: number) => void
}) {
  const channels = historyChannels(brew)
  const units = [...new Set(channels.map(({ metadata }) => metadata.unit))]
  const [selected, setSelected] = useState('F')
  const axisUnit = units.includes(selected) ? selected : units[0] || 'raw'
  const visible = channels.filter(({ metadata }) => metadata.unit === axisUnit)
  const displayUnit = axisUnit === 'F' ? unit : axisUnit
  const rows = brew.samples.map((sample) => ({
    elapsed_ms: sample.elapsed_ms,
    ...Object.fromEntries(
      visible.map(({ field }) => {
        const value = sampleReading(sample, field)
        return [
          field,
          value !== null && axisUnit === 'F' && unit === 'C' ? ((value - 32) * 5) / 9 : value,
        ]
      })
    ),
  }))
  return (
    <div className="sensor-history">
      <Field label="Recorded measurements">
        <select value={axisUnit} onChange={(event) => setSelected(event.target.value)}>
          {units.map((value) => (
            <option key={value} value={value}>
              {value === 'F'
                ? 'Temperature'
                : value === 'kPa'
                  ? 'Pressure'
                  : value === 'V'
                    ? 'Voltage'
                    : value === '%'
                      ? 'Output level'
                      : value === 'rpm'
                        ? 'Speed'
                        : value === 'raw'
                          ? 'Raw sensor readings'
                          : value}
            </option>
          ))}
        </select>
      </Field>
      <div
        className="chart-frame"
        role="img"
        aria-label={`Recorded ${visible.map(({ metadata }) => metadata.label).join(', ')} in ${displayUnit}`}
      >
        <ResponsiveContainer width="100%" height={height}>
          <LineChart
            data={rows}
            margin={{ top: 12, right: 20, bottom: 0, left: 0 }}
            onClick={(event: any) => {
              if (event?.activeLabel != null && Number.isFinite(Number(event.activeLabel)))
                onSelect?.(Number(event.activeLabel))
            }}
          >
            <CartesianGrid vertical={false} stroke="#e7e1d8" strokeDasharray="3 4" />
            <XAxis
              dataKey="elapsed_ms"
              tickFormatter={clockTime}
              minTickGap={55}
              tickLine={false}
              axisLine={false}
              fontSize={14}
            />
            <YAxis
              domain={['auto', 'auto']}
              tickLine={false}
              axisLine={false}
              width={65}
              fontSize={13}
              tickFormatter={(value) =>
                `${Number(value).toLocaleString(undefined, { maximumFractionDigits: 1 })}${axisUnit === 'F' ? '°' : ''}`
              }
            />
            <Tooltip
              labelFormatter={(value) => clockTime(Number(value))}
              formatter={(value: any, name: any) => [
                axisUnit === 'F'
                  ? `${Number(value).toFixed(1)}°${unit}`
                  : sensorValue({ value: Number(value), unit: displayUnit }, unit),
                name,
              ]}
              contentStyle={{ border: '1px solid #e5ddd2', borderRadius: 10, fontSize: 16 }}
            />
            {visible.map(({ field, metadata }, index) => (
              <Line
                key={field}
                name={metadata.label}
                dataKey={field}
                type={field.startsWith('actuator_') ? 'stepAfter' : 'linear'}
                stroke={colors[index % colors.length]}
                strokeWidth={2}
                dot={false}
                connectNulls={false}
                isAnimationActive={false}
              />
            ))}
            {annotations(notes).map((note, index) => (
              <ReferenceLine
                key={index}
                x={note.elapsed_ms}
                stroke="#565775"
                strokeDasharray="3 3"
                label={{ value: note.label, position: 'insideTopLeft', fontSize: 14 }}
              />
            ))}
          </LineChart>
        </ResponsiveContainer>
        <div className="chart-legend">
          {visible.map(({ field, metadata }, index) => (
            <span key={field}>
              <i style={{ background: colors[index % colors.length] }} />
              {metadata.label}
            </span>
          ))}
          <small>
            {brew.samples.length.toLocaleString()} samples ·{' '}
            {displayUnit === 'raw' ? 'Native raw values' : displayUnit}
          </small>
        </div>
      </div>
    </div>
  )
}
