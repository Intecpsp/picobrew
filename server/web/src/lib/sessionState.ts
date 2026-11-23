import type { Brew } from '../types'

// Each adapter writes its own state enum to the same history column.
const pico: Record<number, string> = {
  2: 'Idle',
  3: 'Brewing',
  4: 'Sous vide',
  5: 'Racking',
  6: 'Rinsing',
  7: 'Cleaning',
  8: 'Distilling',
  9: 'Descaling',
}
const phases = [
  'Heating',
  'Holding temperature',
  'Cooling',
  'Draining',
  'Idle',
  'Circulating',
  'Cooling thermoblock',
  'Unheated pump hold',
  'Indefinite heating',
]
// The S/Pro and C control loops set these codes during temperature control.
const picoController: Record<number, string> = {
  0: 'Normal',
  1: 'Wort temperature high',
  2: 'Thermoblock temperature high',
  3: 'Temperature fault',
  5: 'Insufficient heating',
  6: 'Above hold temperature',
  8: 'Still communication fault',
  9: 'Still temperature warning',
  10: 'Still pressure limit',
}
const z = [
  'Off',
  'Booting',
  'Connecting to Wi-Fi',
  'Reconnecting to Wi-Fi',
  'Connecting to Ethernet',
  'Waiting for network',
  'Sync pending',
  'Syncing',
  'Updating',
  'Registering',
  'Downloading program',
  'Standby',
  'Idle',
  'Starting',
  'Running',
  'Stopping',
  'Error',
]
const ferm = [
  'Initializing',
  'Idle or sampling',
  'Fermenting',
  'Calibrating',
  'Calibration required',
  'Sensor fault',
  'Storage fault',
]
const still = [
  'No program selected',
  'Pump test',
  'Hardware test',
  'Pin test',
  'Full test',
  'Cleaning',
  'Essential oils',
  'Manual program',
  'Cleaning requested',
]
const hex = (value: number) => '0x' + value.toString(16).padStart(2, '0')

export function sessionState(
  brew: Pick<Brew, 'id' | 'model' | 'source' | 'step_names'>,
  field: string,
  raw: number | null | undefined,
  source: string,
  detail = false
) {
  if (raw == null) return 'Not recorded'
  const sim = source === 'simulation' || brew.source === 'simulation'
  const smallPico = ['pico-s', 'pico-pro', 'pico-c'].includes(brew.model)
  const isZ = ['z', 'z-series'].includes(brew.model)
  if (field === 'flags') {
    const bits: [number, string][] = [
      [1, 'Controller ready'],
      [2, 'Machine busy'],
      [4, 'Stopped'],
      [16, 'Sampling gap'],
    ]
    if (smallPico || isZ || sim) bits.splice(3, 0, [8, 'Step filter inserted'])
    const labels = bits.filter(([bit]) => raw & bit).map(([, label]) => label)
    const unknown = raw & ~bits.reduce((mask, [bit]) => mask | bit, 0)
    if (unknown) labels.push('Unknown bits ' + hex(unknown))
    const value = labels.join(' · ') || 'No flags set'
    return value
  }
  if (field === 'current_step') {
    if (raw < 0 || raw === 255) return 'No step'
    return [String(raw + 1), brew.step_names?.[raw]].filter(Boolean).join(' · ')
  }
  if (field === 'compartment') {
    if (!sim && brew.model === 'zymatic') {
      const name = [
        'Keg',
        'Mash',
        'Hop addition 1',
        'Hop addition 2',
        'Hop addition 3',
        'Hop addition 4',
        'Operator pause',
      ][raw]
      if (name) return detail ? name + ' (' + raw + ')' : name
    }
    // The S/Pro history sample records the controller compartment. These are
    // its front-panel labels, a different namespace from PicoPak recipe steps.
    if (!sim && ['pico-s', 'pico-pro'].includes(brew.model)) {
      const name = [
        'Reservoir',
        'Mash',
        'Heat water',
        'Adjunct 1',
        'Adjunct 2',
        'Adjunct 4',
        'Adjunct 3',
      ][raw]
      if (name) return detail ? name + ' (' + raw + ')' : name
    }
    return 'Position ' + raw
  }
  let name: string | undefined
  if (sim) {
    if (field === 'device_state') {
      // Original seeded example sessions omitted this field in every sample.
      name =
        raw === 0 && brew.id.startsWith('demo-history-') ? 'Not recorded' : ['Idle', 'Running'][raw]
    } else if (field === 'phase') return 'Stage ' + raw
    else if (field === 'controller_status' && raw === 1) name = 'Simulated run'
    else if (field === 'controller_status' && raw === 0 && brew.id.startsWith('demo-history-'))
      name = 'Not recorded'
  } else if (field === 'device_state') {
    name = smallPico
      ? pico[raw]
      : isZ
        ? z[raw]
        : brew.model === 'picoferm'
          ? ferm[raw]
          : brew.model === 'picostill'
            ? still[raw]
            : undefined
  } else if (field === 'phase' && smallPico) name = phases[raw]
  else if (field === 'controller_status' && smallPico) name = picoController[raw]
  else if (field === 'controller_status' && isZ)
    name = ['Off', 'Booting', 'Idle', 'Running', 'Standby'][raw]
  return name ? (detail ? name + ' (' + raw + ')' : name) : 'Code ' + raw
}
