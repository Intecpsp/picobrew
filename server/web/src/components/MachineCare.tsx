import type { DeviceStatus } from '../types'
import { SectionHeading } from './ui'

const careTasks = new Map([
  ['rinse', 'Rinse the brewer'],
  ['deep_clean', 'Run Deep Clean'],
  ['descale', 'Descale if using water other than distilled or reverse osmosis'],
  ['clean', 'Run Clean'],
  ['clean_filter', 'Clean the inline filter'],
  ['clean_condenser', 'Clean the condenser and removable parts'],
  ['sanitize_seal', 'Sanitize and air-dry the fermentation seal before fitting it'],
])

export function careTasksDue(maintenance?: DeviceStatus['maintenance']) {
  if (maintenance?.available === false) return []
  return (maintenance?.due || []).map(
    (task) => careTasks.get(task) || 'Check the machine for care instructions'
  )
}

export function CareNotice({ maintenance }: { maintenance?: DeviceStatus['maintenance'] }) {
  const tasks = careTasksDue(maintenance)
  if (!tasks.length) return null
  return (
    <div className="connection-advisory" role="status">
      <strong>Maintenance due</strong>
      <ul>
        {tasks.map((task, i) => (
          <li key={i}>{task}</li>
        ))}
      </ul>
    </div>
  )
}

const manualRoot = 'https://picobrewcontent.blob.core.windows.net/content/'
function picoCare(tabletLocation: string) {
  return [
    'Rinse after each brew.',
    `Run Deep Clean every three brews. Put the cleaning tablet in ${tabletLocation}.`,
    'If you use reservoir water other than distilled or reverse osmosis water, descale every 20 brews. Run Deep Clean after descaling.',
  ]
}
const kegCare = picoCare('the brewing keg')
const care: Record<string, { manual: string; reminders: string[] }> = {
  'pico-s': { manual: manualRoot + 'pico/Pico_Manual.pdf#page=127', reminders: kegCare },
  'pico-pro': { manual: manualRoot + 'picopro/PicoPro_Manual.pdf#page=129', reminders: kegCare },
  'pico-c': {
    manual: manualRoot + 'picoc/PicoC_Manual.pdf#page=109',
    reminders: picoCare('the center of the step filter'),
  },
  'z-series': {
    manual: manualRoot + 'z/Z_Manual.pdf#page=36',
    reminders: [
      'Rinse after each brew.',
      'Run Clean every three brews, or before leaving the brewer unused for more than a couple of weeks.',
    ],
  },
  zymatic: {
    manual: 'https://picodevblob.blob.core.windows.net/content/zymatic/Zymatic_Manual.pdf#page=23',
    reminders: [
      'Rinse and clean the inline filter after each brew.',
      'Run Deep Clean every three to four brews, then rinse.',
    ],
  },
  picostill: {
    manual: manualRoot + 'picostill/PicoStill_Manual.pdf#page=58',
    reminders: [
      'After each use, clean the condenser column and removable parts. Keep the upper electronics dry.',
      'Soak the copper parts for about 10 minutes in the cleaning solution specified in the manual, then rinse thoroughly.',
    ],
  },
  picoferm: {
    manual: manualRoot + 'picoferm/PicoFerm_Manual.pdf#page=5',
    reminders: [
      'Before fitting PicoFerm to a Pico C keg, sanitize the underside of the gray fermentation seal and let it air-dry. Keep liquid out of the monitor.',
      'Depressurize the keg before removing or replacing PicoFerm.',
    ],
  },
}

export function MachineCare({ model }: { model: string }) {
  const guide = Object.hasOwn(care, model) ? care[model] : undefined
  if (!guide) return null
  return (
    <section className="panel" aria-label="Cleaning and maintenance">
      <SectionHeading title="Cleaning and maintenance" />
      <ul>
        {guide.reminders.map((item) => (
          <li key={item}>{item}</li>
        ))}
      </ul>
      <a href={guide.manual} target="_blank" rel="noreferrer">
        PicoBrew care instructions
      </a>
    </section>
  )
}
