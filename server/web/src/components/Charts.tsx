import {
  ResponsiveContainer,
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ReferenceLine,
  AreaChart,
  Area,
} from 'recharts'
import type { Sample, Unit } from '../types'
import { annotations, clockTime, convertTemp } from '../lib/utils'
export function TemperatureChart({
  samples,
  unit,
  notes = '',
  height = 280,
  onSelect,
}: {
  samples: Sample[]
  unit: Unit
  notes?: string
  height?: number
  onSelect?: (elapsed: number) => void
}) {
  const data = samples.map((sample) => ({
    ...sample,
    wort: convertTemp(sample.wort_f, unit),
    block: convertTemp(sample.thermoblock_f, unit),
    target: convertTemp(sample.target_f, unit),
  }))
  return (
    <div
      className="chart-frame"
      role="img"
      aria-label={`Wort, thermoblock and target temperatures in degrees ${unit === 'F' ? 'Fahrenheit' : 'Celsius'} over the brew`}
    >
      <ResponsiveContainer width="100%" height={height}>
        <LineChart
          data={data}
          margin={{ top: 12, right: 20, bottom: 0, left: 0 }}
          onClick={(event: any) => {
            const value = event?.activeLabel
            if (value !== undefined && value !== null && Number.isFinite(Number(value)))
              onSelect?.(Number(value))
          }}
        >
          <CartesianGrid vertical={false} stroke="var(--hub-line)" strokeDasharray="3 4" />
          <XAxis
            dataKey="elapsed_ms"
            tickFormatter={clockTime}
            minTickGap={55}
            stroke="var(--hub-muted)"
            tickLine={false}
            axisLine={false}
            fontSize={14}
          />
          <YAxis
            domain={['auto', 'auto']}
            stroke="var(--hub-muted)"
            tickLine={false}
            axisLine={false}
            fontSize={14}
            width={52}
            tickFormatter={(value) => `${value}°`}
          />
          <Tooltip
            labelFormatter={(value) => clockTime(Number(value))}
            formatter={(value: any, name: any) => [
              value == null ? '—' : `${Number(value).toFixed(1)}°${unit}`,
              name,
            ]}
            contentStyle={{
              backgroundColor: 'var(--hub-surface)',
              color: 'var(--hub-ink)',
              border: '1px solid var(--hub-line)',
              borderRadius: 10,
              fontSize: 16,
            }}
          />
          <Line
            name="Wort"
            type="monotone"
            dataKey="wort"
            stroke="var(--hub-wort)"
            strokeWidth={2.5}
            dot={false}
            isAnimationActive={false}
          />
          <Line
            name="Thermoblock"
            type="monotone"
            dataKey="block"
            stroke="var(--hub-block)"
            strokeWidth={1.7}
            dot={false}
            isAnimationActive={false}
          />
          <Line
            name="Target"
            type="stepAfter"
            dataKey="target"
            stroke="var(--hub-target)"
            strokeDasharray="5 5"
            strokeWidth={1.5}
            dot={false}
            isAnimationActive={false}
          />
          {annotations(notes).map((note, index) => (
            <ReferenceLine
              key={index}
              x={note.elapsed_ms}
              stroke="var(--hub-phase)"
              strokeDasharray="3 3"
              label={{
                value: note.label,
                position: 'insideTopLeft',
                fill: 'var(--hub-muted)',
                fontSize: 14,
              }}
            />
          ))}
        </LineChart>
      </ResponsiveContainer>
      <div className="chart-legend">
        <span>
          <i className="wort" />
          Wort
        </span>
        <span>
          <i className="block" />
          Thermoblock
        </span>
        <span>
          <i className="target" />
          Target
        </span>
        <small>{samples.length.toLocaleString()} samples</small>
      </div>
    </div>
  )
}
export function StateChart({ samples, brewing = true }: { samples: Sample[]; brewing?: boolean }) {
  return (
    <ResponsiveContainer width="100%" height={190}>
      <LineChart data={samples} margin={{ top: 10, right: 20, bottom: 0, left: 0 }}>
        <CartesianGrid vertical={false} stroke="var(--hub-line)" />
        <XAxis
          stroke="var(--hub-muted)"
          dataKey="elapsed_ms"
          tickFormatter={clockTime}
          minTickGap={55}
          fontSize={14}
        />
        <YAxis stroke="var(--hub-muted)" allowDecimals={false} fontSize={14} width={52} />
        <Tooltip
          labelFormatter={(value) => clockTime(Number(value))}
          contentStyle={{
            backgroundColor: 'var(--hub-surface)',
            color: 'var(--hub-ink)',
            border: '1px solid var(--hub-line)',
            borderRadius: 10,
            fontSize: 16,
          }}
        />
        {brewing && (
          <Line
            name="Phase"
            type="stepAfter"
            dataKey="phase"
            stroke="var(--hub-wort)"
            dot={false}
            isAnimationActive={false}
          />
        )}
        {brewing && (
          <Line
            name="Step"
            type="stepAfter"
            dataKey="current_step"
            stroke="var(--hub-block)"
            dot={false}
            isAnimationActive={false}
          />
        )}
        {brewing && (
          <Line
            name="Controller"
            type="stepAfter"
            dataKey="controller_status"
            stroke="var(--hub-phase)"
            dot={false}
            isAnimationActive={false}
          />
        )}
        <Line
          name="Flags"
          type="stepAfter"
          dataKey="flags"
          stroke="var(--hub-gold)"
          dot={false}
          isAnimationActive={false}
        />
        {brewing && (
          <Line
            name="Compartment"
            type="stepAfter"
            dataKey="compartment"
            stroke="var(--hub-pump)"
            dot={false}
            isAnimationActive={false}
          />
        )}
        <Line
          name="Device state"
          type="stepAfter"
          dataKey="device_state"
          stroke="var(--hub-state)"
          dot={false}
          isAnimationActive={false}
        />
      </LineChart>
    </ResponsiveContainer>
  )
}
export function Sparkline({ values }: { values: { value: number | null }[] }) {
  return (
    <ResponsiveContainer width="100%" height={40}>
      <AreaChart data={values}>
        <Area
          dataKey="value"
          type="monotone"
          stroke="var(--hub-wort)"
          fill="var(--hub-chart-fill)"
          strokeWidth={2}
          isAnimationActive={false}
        />
      </AreaChart>
    </ResponsiveContainer>
  )
}
