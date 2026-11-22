import { useState } from 'react'
import { Button, Field, Modal } from './ui'

export function StockSetupGuide({ onClose }: { onClose: () => void }) {
  const [model, setModel] = useState('')
  return (
    <Modal
      title="Set up a stock brewer via USB"
      wide
      onClose={onClose}
      actions={
        <Button variant="secondary" onClick={onClose}>
          Back to Add machine
        </Button>
      }
    >
      <p>
        Use a USB-connected Mac for the first install. The USB installer supports one reviewed Pico
        S/Pro brewer. It verifies the original backup and the connected hardware before writing.
      </p>
      <ol className="stock-usb-steps">
        <li>
          Choose a private key file for this brewer. The installer reuses that key or creates one
          securely. Keep the key and a permanent private copy of your Hub data directory, which
          stores the original backup.
        </li>
        <li>
          Prepare the component’s Python environment and USB dependencies using the{' '}
          <a
            href="https://github.com/jeffreydwalter/picobrew-pico-pro-firmware/blob/master/firmware/pico-s-pro/modified/docs/macos-usb-installation.md"
            target="_blank"
            rel="noreferrer"
          >
            macOS USB setup guide
          </a>
          . Build the Hub in the checkout’s <code>server</code> directory.
        </li>
      </ol>
      <Field label="Brewer to set up">
        <select value={model} onChange={(e) => setModel(e.target.value)}>
          <option value="">Choose your brewer</option>
          <option value="pico-s">Pico S</option>
          <option value="pico-pro">Pico Pro</option>
        </select>
      </Field>
      {model && (
        <>
          <p>
            In the Mac’s terminal, run this command from <code>server</code>. Set the private key
            path and the Hub data directory you use.
          </p>
          <pre className="stock-usb-command">
            <code>{`./picobrew-hub usb-install --model ${model} \\\n  --key-file "/absolute/path/to/api.key" --data ./data --checkout ..`}</code>
          </pre>
          <p>
            If you already have this brewer’s original full-flash backup, add{' '}
            <code>--backup "/absolute/path/to/original.bin"</code> to verify and store it. Otherwise
            the installer uses the backup saved in the Hub or, on a stock brewer, reads the flash
            twice over USB and verifies the new backup before writing.
          </p>
          <p>
            The tool requests administrator access for USB writes. Follow its prompts to provision
            the key, install the application and website, and install recovery. Each programming
            session requires a fresh power-on or reset. After the brewer boots and joins Wi-Fi,
            return to Add machine and enter its URL and API key.
          </p>
        </>
      )}
    </Modal>
  )
}
