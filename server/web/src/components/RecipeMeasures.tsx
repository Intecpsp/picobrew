import { createContext, useContext, useState, type ReactNode } from 'react'
import { formatQuantity } from '../lib/recipeDisplay'
import { Field } from './ui'

const Measures = createContext({ us: false, change: (_value: boolean) => {} })
export function RecipeMeasures({ children }: { children: ReactNode }) {
  const [us, setUS] = useState(() => localStorage.getItem('hub-recipe-measures') === 'us')
  function change(value: boolean) {
    setUS(value)
    localStorage.setItem('hub-recipe-measures', value ? 'us' : 'metric')
  }
  return <Measures.Provider value={{ us, change }}>{children}</Measures.Provider>
}
export function MeasureChoice() {
  const { us, change } = useContext(Measures)
  return (
    <Field label="Recipe measurements">
      <select value={us ? 'us' : 'metric'} onChange={(e) => change(e.target.value === 'us')}>
        <option value="metric">Metric (L, kg, g)</option>
        <option value="us">US customary (gal, lb, oz)</option>
      </select>
    </Field>
  )
}
const canonicalPerUS: Record<string, [number, string]> = {
  L: [3.785411784, 'US gal'],
  kg: [0.45359237, 'lb'],
  g: [28.349523125, 'oz'],
  mL: [29.5735295625, 'US fl oz'],
}
export function useRecipeQuantity() {
  const { us } = useContext(Measures)
  return (n: number, unit: string) => {
    const conversion = us && canonicalPerUS[unit]
    return conversion ? formatQuantity(n / conversion[0], conversion[1]) : formatQuantity(n, unit)
  }
}
export function QuantityField({
  label,
  unit,
  value,
  max,
  invalid,
  placeholder,
  change,
}: {
  label: string
  unit: string
  value: string | number
  max?: number
  invalid?: boolean
  placeholder?: number
  change: (value: string) => void
}) {
  const { us } = useContext(Measures)
  const [factor, displayUnit] = us && canonicalPerUS[unit] ? canonicalPerUS[unit] : [1, unit]
  const [editing, setEditing] = useState<{ unit: string; raw: string; canonical: string } | null>(
    null
  )
  const converted = (n: number) => String(Number((n / factor).toPrecision(9)))
  const display = value === '' ? '' : factor === 1 ? String(value) : converted(Number(value))
  return (
    <Field label={label + ' (' + displayUnit + ')'}>
      <input
        type="number"
        step="any"
        min="0"
        max={max === undefined ? undefined : max / factor}
        aria-invalid={invalid || undefined}
        placeholder={placeholder === undefined ? undefined : converted(placeholder)}
        value={
          editing?.unit === displayUnit && editing.canonical === String(value)
            ? editing.raw
            : display
        }
        onChange={(e) => {
          const raw = e.target.value
          const canonical =
            raw === '' || factor === 1
              ? raw
              : String(Number((Number(raw) * factor).toPrecision(15)))
          setEditing({ unit: displayUnit, raw, canonical })
          change(canonical)
        }}
        onBlur={() => setEditing(null)}
      />
    </Field>
  )
}
