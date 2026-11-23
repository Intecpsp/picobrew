import { useState } from 'react'
import { Settings2, RefreshCw, Pencil, Trash2, SlidersHorizontal } from 'lucide-react'
import { isAccessory } from '../lib/capabilities'
import type { Device } from '../types'
import { Button, Modal } from './ui'

export function MachineManagement({
  device,
  testing,
  test,
  edit,
  preferences,
  preferencesDisabled,
  remove,
}: {
  device: Device
  testing: boolean
  test: () => void
  edit: () => void
  preferences: () => void
  preferencesDisabled?: boolean
  remove: () => void
}) {
  const [open, setOpen] = useState(false)
  function choose(action: () => void) {
    setOpen(false)
    action()
  }
  return (
    <>
      <Button variant="secondary" aria-haspopup="dialog" onClick={() => setOpen(true)}>
        <Settings2 size={16} />
        Manage machine
      </Button>
      {open && (
        <Modal title="Machine management" onClose={() => setOpen(false)}>
          <h3>{device.name}</h3>
          {device.base_url && <p>{device.base_url}</p>}
          <div className="management-actions">
            <Button
              variant="secondary"
              disabled={testing || !device.enabled}
              onClick={() => choose(test)}
            >
              <RefreshCw size={16} />
              Test connection
            </Button>
            <Button variant="secondary" onClick={() => choose(edit)}>
              <Pencil size={16} />
              Edit connection
            </Button>
            <Button
              variant="secondary"
              disabled={preferencesDisabled}
              onClick={() => choose(preferences)}
            >
              <SlidersHorizontal size={16} />
              {isAccessory(device) ? 'Device preferences' : 'Brewer preferences'}
            </Button>
            <Button variant="danger" onClick={() => choose(remove)}>
              <Trash2 size={16} />
              Remove machine
            </Button>
          </div>
        </Modal>
      )}
    </>
  )
}
