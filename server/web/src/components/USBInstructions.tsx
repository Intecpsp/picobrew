import { useState } from 'react'
import { modelLabel } from '../lib/utils'
import { Field } from './ui'

const source = 'https://github.com/jeffreydwalter/picobrew-pico-pro-firmware/blob/master/'
export const usbModels = [
  'pico-s',
  'pico-pro',
  'pico-c',
  'z-series',
  'picostill',
  'picoferm',
  'zymatic',
]
const serialModel = (model: string) => ['z-series', 'picostill', 'picoferm'].includes(model)
export const usbLabel = (model: string) =>
  model === 'z-series' ? 'Pico Z module (Z1–Z4)' : modelLabel(model)

export type USBAction = 'install' | 'backup' | 'restore'

export function USBInstructions({
  model,
  action,
  uid,
  display = '',
}: {
  model: string
  action: USBAction
  uid?: string
  display?: string
}) {
  const [controller, setController] = useState(
    display === 'ssd1309' || display === 'ssd1325' ? display : ''
  )
  const serial = serialModel(model)
  const zymatic = model === 'zymatic'
  const cc = !serial && !zymatic
  const needsDisplay = model === 'pico-c'
  const args = ['./picobrew-hub', 'usb-' + action, '--model', model]
  if (needsDisplay) args.push('--display', controller)
  if (cc && action === 'install') args.push('--key-file', '"/absolute/path/to/api.key"')
  if (cc && action === 'restore' && uid && /^[a-f0-9]{32}$/i.test(uid))
    args.push('--device', uid.toLowerCase())
  if (serial || zymatic) args.push('--port', 'SERIAL_PORT')
  args.push('--data', './data', '--checkout', '..')
  const doc = zymatic
    ? 'firmware/zymatic/modified/README.md'
    : serial
      ? 'firmware/common/docs/esp-usb.md'
      : model === 'pico-c'
        ? 'firmware/pico-c/modified/docs/standalone-provisioning.md'
        : 'firmware/pico-s-pro/modified/docs/macos-usb-installation.md'
  return (
    <>
      {serial && model === 'z-series' && (
        <p>
          Connect one brewing module at a time, including on a Z1–Z4 stack.{' '}
          {action === 'install' &&
            'Setup requires a matching stock 0.0.119 module and flash layout.'}
          {action !== 'install' &&
            'The backup covers the module’s ESP32 flash. It does not capture or restore its separate MSP controller.'}
        </p>
      )}
      {needsDisplay && (
        <Field
          label="Physical Pico C display controller"
          hint="Check the fitted controller or its hardware documentation before selecting."
        >
          <select value={controller} onChange={(e) => setController(e.target.value)}>
            <option value="">Choose the controller you verified</option>
            <option value="ssd1325">SSD1325</option>
            <option value="ssd1309">SSD1309</option>
          </select>
        </Field>
      )}
      {(!needsDisplay || controller) && (
        <>
          <p>
            Run this from the checkout’s <code>server</code> directory{' '}
            {cc
              ? 'on the USB-connected Mac'
              : serial
                ? 'on the computer connected to the service port'
                : 'on the computer connected to the Zymatic’s built-in USB port'}
            . Set <code>--data</code> to your Hub data directory.
            {(serial || zymatic) && (
              <>
                {' '}
                Replace <code>SERIAL_PORT</code> with the attached device’s serial port.
              </>
            )}
          </p>
          <pre className="stock-usb-command">
            <code>{args.join(' ')}</code>
          </pre>
          {cc && action === 'install' && (
            <p>
              Set an absolute path for this machine’s private API key. The installer verifies a
              saved original or reads the stock flash twice. Follow the prompts for power-on or
              reset at each USB session.{' '}
              {model === 'pico-c'
                ? 'The Pico C keeps its original recovery.'
                : 'Install the separate recovery image to enable Hub wireless updates.'}
            </p>
          )}
          {cc && action === 'restore' && (
            <p>
              {model === 'pico-c'
                ? 'The tool restores the original application and verifies the existing recovery slot.'
                : 'The tool restores the original application and recovery slots. A pinned GitHub fallback is available only for the individually documented S/Pro unit.'}{' '}
              Follow the USB power-on and reset prompts and wait for readback verification.
            </p>
          )}
          {cc && action === 'backup' && (
            <p>
              Connect the idle machine’s CH340 adapter. Follow the power-on or reset prompt to read
              its stock flash twice and save its verified original in the Hub data directory.
            </p>
          )}
          {cc && action !== 'backup' && (
            <p>
              To supply this brewer’s private full-flash backup, add{' '}
              <code>--backup "/absolute/path/to/original.bin"</code>.
            </p>
          )}
          {serial && action === 'install' && (
            <p>
              Setup saves a verified stock backup for this device before writing a compatible
              standalone package. Keep the backup for restoration.
            </p>
          )}
          {serial && action === 'backup' && (
            <p>
              The tool reads the entire flash twice and stores the matching original in the Hub data
              directory under the connected device’s identity.
            </p>
          )}
          {serial && action === 'restore' && (
            <p>
              Connect the same device and use the Hub data directory that holds its verified
              full-flash backup. The tool writes and verifies that device’s saved flash contents.
            </p>
          )}
          {zymatic && action === 'backup' && (
            <p>
              The tool uses AVRDUDE to read the Zymatic’s program flash twice and saves matching
              bytes privately. The bootloader determines what can be read. Keep the capture with
              your machine’s records.
            </p>
          )}
          {zymatic && action === 'restore' && (
            <p>
              The tool uses your saved 1.1.14 application or retrieves the verified original from
              the pinned archive. To import your own vendor HEX or ZIP, add{' '}
              <code>--oem-file "/absolute/path/to/original.zip"</code>. Omit{' '}
              <code>--port SERIAL_PORT</code> to prepare the HEX for Windows PicoFlash. After
              restarting, confirm 1.1.14 in About Zymatic.
            </p>
          )}
          <a href={source + doc} target="_blank" rel="noreferrer">
            {zymatic
              ? 'Zymatic USB instructions'
              : cc
                ? 'macOS USB instructions'
                : 'ESP USB instructions'}
          </a>
        </>
      )}
    </>
  )
}
