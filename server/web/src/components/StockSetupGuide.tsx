import { useState } from 'react'
import { Button, Field, Modal } from './ui'
import { StockRestoreGuide } from './StockRestoreGuide'
import { USBInstructions, usbLabel, usbModels, type USBAction } from './USBInstructions'

export function StockSetupGuide({ onClose }: { onClose: () => void }) {
  const [model, setModel] = useState('')
  const [action, setAction] = useState<USBAction>('install')
  const [restoring, setRestoring] = useState(false)
  if (restoring)
    return (
      <StockRestoreGuide
        model={model}
        onClose={() => setRestoring(false)}
        returnLabel="Back to USB setup"
      />
    )
  return (
    <Modal
      title="USB setup and recovery"
      wide
      onClose={onClose}
      actions={
        <Button variant="secondary" onClick={onClose}>
          Back to Add machine
        </Button>
      }
    >
      <Field label="Machine model">
        <select
          value={model}
          onChange={(e) => {
            setModel(e.target.value)
            setAction(e.target.value === 'zymatic' ? 'restore' : 'install')
          }}
        >
          <option value="">Choose your machine</option>
          {usbModels.map((m) => (
            <option key={m} value={m}>
              {usbLabel(m)}
            </option>
          ))}
        </select>
      </Field>
      {model && (
        <>
          {model === 'zymatic' && (
            <p>
              Zymatic can connect to the Hub with its original firmware. A standalone image is
              unavailable.{' '}
              <a
                href="https://github.com/jeffreydwalter/picobrew-pico-pro-firmware/blob/master/server/docs/stock-zymatic.md"
                target="_blank"
                rel="noreferrer"
              >
                Set up stock recipe sync
              </a>
            </p>
          )}
          <Field label="USB operation">
            <select value={action} onChange={(e) => setAction(e.target.value as USBAction)}>
              {model !== 'zymatic' && <option value="install">Install standalone firmware</option>}
              <option value="backup">
                {model === 'zymatic' ? 'Back up readable program flash' : 'Back up stock firmware'}
              </option>
              <option value="restore">Restore original firmware</option>
            </select>
          </Field>
          {action === 'restore' ? (
            <Button variant="secondary" onClick={() => setRestoring(true)}>
              Review stock restoration
            </Button>
          ) : (
            <USBInstructions key={model} model={model} action={action} />
          )}
        </>
      )}
    </Modal>
  )
}
