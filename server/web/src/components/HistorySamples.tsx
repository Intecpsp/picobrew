import type { BrewDetail, Sample, Unit } from '../types'
import { clockTime, duration, temperature } from '../lib/utils'
import { sampleReading } from '../lib/history'
import { sensorValue } from '../lib/capabilities'

export function HistorySamples({
  brew,
  page,
  unit,
}: {
  brew: BrewDetail
  page: number
  unit: Unit
}) {
  const accessory = ['picoferm', 'picostill'].includes(brew.model)
  const stable: Record<string, { label: string; unit: string }> = {
    temperature_f: { label: 'Temperature', unit: 'F' },
    pressure_kpa: { label: 'Pressure', unit: 'kPa' },
    battery_v: { label: 'Battery', unit: 'V' },
  }
  const extra = (
    brew.sample_fields ||
    Object.keys(stable).concat(...brew.samples.map((sample) => Object.keys(sample.readings || {})))
  ).filter(
    (field, index, fields) =>
      fields.indexOf(field) === index &&
      (stable[field] || field.startsWith('sensor_') || field.startsWith('actuator_'))
  )
  const metadata = (field: string) =>
    brew.field_metadata?.[field] ||
    stable[field] || { label: field.replaceAll('_', ' '), unit: 'raw' }
  return (
    <div className="table-scroll">
      <table className="data-table sample-table">
        <thead>
          <tr>
            <th>Time</th>
            {!accessory &&
              [
                'Wort',
                'Thermoblock',
                'Target',
                'Step time',
                'Phase',
                'Controller',
                'Compartment',
                'Step',
              ].map((label) => <th key={label}>{label}</th>)}
            {extra.map((field) => (
              <th key={field}>
                {metadata(field).label} (
                {metadata(field).unit === 'F' ? unit : metadata(field).unit})
              </th>
            ))}
            <th>Flags</th>
            <th>State</th>
            <th>Source</th>
          </tr>
        </thead>
        <tbody>
          {brew.samples.slice(page * 100, (page + 1) * 100).map((sample: Sample, index) => (
            <tr key={index}>
              <td>{clockTime(sample.elapsed_ms)}</td>
              {!accessory && (
                <>
                  <td>{temperature(sample.wort_f, unit)}</td>
                  <td>{temperature(sample.thermoblock_f, unit)}</td>
                  <td>{temperature(sample.target_f, unit)}</td>
                  <td>{duration(sample.step_seconds * 1000)}</td>
                  <td>{sample.phase}</td>
                  <td>{sample.controller_status}</td>
                  <td>{sample.compartment}</td>
                  <td>{sample.current_step === 255 ? '—' : sample.current_step + 1}</td>
                </>
              )}
              {extra.map((field) => (
                <td key={field}>
                  {sensorValue(
                    { value: sampleReading(sample, field), unit: metadata(field).unit },
                    unit
                  )}
                </td>
              ))}
              <td>0x{sample.flags.toString(16)}</td>
              <td>{sample.device_state}</td>
              <td>{sample.source}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  )
}
