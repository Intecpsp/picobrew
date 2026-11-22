import { useState } from 'react'
import { Button, Modal } from './ui'

export function StockRestoreGuide({ model, onClose }: { model: string; onClose: () => void }) {
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
            {step < 3 ? 'Cancel' : 'Back to machine firmware'}
          </Button>
          {step < 3 && (
            <Button disabled={!confirmed} onClick={advance}>
              {step === 1 ? 'Review USB requirements' : 'Show USB restore instructions'}
            </Button>
          )}
        </>
      }
    >
      {step === 1 && (
        <>
          <p>
            Restoring your {model} to its original PicoBrew firmware removes its standalone website,
            REST API and wireless firmware updates from the Hub.
          </p>
          <label className="check-label">
            <input
              type="checkbox"
              checked={confirmed}
              onChange={(e) => setConfirmed(e.target.checked)}
            />
            I understand that returning to standalone firmware requires USB, the original private
            full-flash backup and the API key that matches this brewer.
          </label>
        </>
      )}
      {step === 2 && (
        <>
          <p>
            Restoration takes place on a USB-connected Mac. The tool checks that this is the
            reviewed Pico S/Pro brewer and verifies its boot selection and flash layout before
            writing.
          </p>
          <label className="check-label">
            <input
              type="checkbox"
              checked={confirmed}
              onChange={(e) => setConfirmed(e.target.checked)}
            />
            I can connect the idle brewer to my Mac by USB and want to restore its original
            application and recovery firmware.
          </label>
        </>
      )}
      {step === 3 && (
        <>
          <p>
            The tool uses the original firmware saved in your Hub data directory first. If the saved
            original is absent, it downloads the publicly archived PicoBrew application and recovery
            images from the project’s GitHub repository and verifies their pinned hashes. Your
            private backup and API key stay local. Physical restore testing is still pending.
          </p>
          <ol className="stock-usb-steps">
            <li>
              Prepare the USB tools from the project’s{' '}
              <a
                href="https://github.com/jeffreydwalter/picobrew-pico-pro-firmware/blob/master/firmware/pico-s-pro/modified/docs/macos-usb-installation.md#restore-all-oem-firmware-slots"
                target="_blank"
                rel="noreferrer"
              >
                macOS USB restoration guide
              </a>
              .
            </li>
            <li>
              Connect the idle brewer by USB. On the Mac, run this command from the checkout’s{' '}
              <code>server</code> directory. Set <code>--data</code> to the Hub data directory that
              holds this brewer’s saved original if it differs from <code>./data</code>.
            </li>
          </ol>
          <pre className="stock-usb-command">
            <code>{'./picobrew-hub usb-restore --data ./data --checkout ..'}</code>
          </pre>
          <ol start={3} className="stock-usb-steps">
            <li>
              At the initial “BREAK asserted” prompt, power on the brewer or press and release RESET
              once. Keep power and USB connected until the tool finishes.
            </li>
            <li>
              Restart the brewer only after the tool reports that it verified the original
              application and recovery.
            </li>
          </ol>
        </>
      )}
    </Modal>
  )
}
