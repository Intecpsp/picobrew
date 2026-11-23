import { useState } from 'react'
import { Button, Modal } from './ui'
import { USBInstructions, usbLabel } from './USBInstructions'

export function StockRestoreGuide({
  model,
  uid,
  display,
  onClose,
  returnLabel = 'Back to machine firmware',
}: {
  model: string
  uid?: string
  display?: string
  onClose: () => void
  returnLabel?: string
}) {
  const [step, setStep] = useState(1)
  const [confirmed, setConfirmed] = useState(false)
  const titles = [
    'Restore original PicoBrew firmware?',
    'Confirm your USB setup',
    'Restore using your computer',
  ]
  function advance() {
    setConfirmed(false)
    setStep((current) => current + 1)
  }
  return (
    <Modal
      title={titles[step - 1]}
      key={step}
      onClose={onClose}
      wide
      actions={
        <>
          <Button variant="secondary" onClick={onClose}>
            {step < 3 ? 'Cancel' : returnLabel}
          </Button>
          {step < 3 && (
            <Button disabled={!confirmed} onClick={advance}>
              {step === 1 ? 'Review USB requirements' : 'Show USB restore instructions'}
            </Button>
          )}
        </>
      }
    >
      {step < 3 ? (
        <>
          <p>
            {step === 1
              ? model === 'zymatic'
                ? 'This restores the original Zymatic 1.1.14 application over USB. The Zymatic can sync recipes with the Hub after restarting.'
                : 'Returning your ' +
                  usbLabel(model) +
                  ' to stock removes its standalone controls, custom API and any Hub wireless updates. A future conversion requires USB.'
              : 'Connect only the intended idle machine. Keep power and USB connected throughout the write and verification.'}
          </p>
          <label className="checkbox-row">
            <input
              type="checkbox"
              checked={confirmed}
              onChange={(e) => setConfirmed(e.target.checked)}
            />
            {step === 1
              ? 'I understand the effect of restoring stock firmware.'
              : 'I can connect this machine by USB and want to restore its original firmware.'}
          </label>
        </>
      ) : (
        <USBInstructions model={model} action="restore" uid={uid} display={display} />
      )}
    </Modal>
  )
}
