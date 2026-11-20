import { useState, type FormEvent } from 'react'
import { Download, Trash2 } from 'lucide-react'
import { useHub, useResource } from '../context'
import { api, download, errorMessage } from '../lib/api'
import type { FirmwareCandidate, FirmwarePackage, MachineFirmware } from '../types'
import { Button, ErrorBanner, Field, Loading, Modal } from './ui'

export function firmwareModelName(model: string) {
  return model === 'pico-s'
    ? 'Pico S'
    : model === 'pico-pro'
      ? 'Pico Pro'
      : model === 'pico-c'
        ? 'Pico C'
        : model || 'Not identified'
}

export function firmwareCandidateName(item: FirmwareCandidate) {
  return (
    firmwareModelName(item.model) +
    (item.display_variant ? ` · ${item.display_variant.toUpperCase()}` : '')
  )
}

export function firmwareSize(bytes: number) {
  return bytes >= 1024 * 1024
    ? `${(bytes / (1024 * 1024)).toFixed(1)} MB`
    : `${Math.ceil(bytes / 1024)} KB`
}

export function FirmwareDetails({ item }: { item: FirmwareCandidate }) {
  return (
    <details className="firmware-details">
      <summary>Package details</summary>
      <dl>
        <dt>Machine family</dt>
        <dd>{item.model_family}</dd>
        {item.display_variant && (
          <>
            <dt>Display controller</dt>
            <dd>{item.display_variant.toUpperCase()}</dd>
          </>
        )}
        <dt>Machine image</dt>
        <dd>{firmwareSize(item.binary_bytes)}</dd>
        <dt>Image SHA-256</dt>
        <dd>
          <code>{item.binary_sha256}</code>
        </dd>
      </dl>
    </details>
  )
}

export function FirmwarePackageCard({
  item,
  onRemove,
  disabled = false,
}: {
  item: FirmwarePackage
  onRemove?: (item: FirmwarePackage) => void
  disabled?: boolean
}) {
  const [downloading, setDownloading] = useState(false)
  const [error, setError] = useState('')
  async function saveFile() {
    setDownloading(true)
    setError('')
    try {
      await download(
        `/firmware/packages/${encodeURIComponent(item.id)}/download`,
        `${item.model}${item.display_variant ? '-' + item.display_variant : ''}-${item.firmware_version}.zip`
      )
    } catch (error) {
      setError(errorMessage(error))
    } finally {
      setDownloading(false)
    }
  }
  return (
    <article
      className="firmware-package"
      aria-label={`${firmwareCandidateName(item)} ${item.firmware_version}`}
    >
      <div className="firmware-package-heading">
        <h4>
          {firmwareCandidateName(item)} <span>{item.firmware_version}</span>
        </h4>
        <span className="help-text">{firmwareSize(item.package_bytes)}</span>
      </div>
      <p className="firmware-source">
        {item.provenance.source === 'github' ? (
          <>
            GitHub · {item.provenance.repository}{' '}
            {item.provenance.commit && item.provenance.repository && (
              <a
                href={`https://github.com/${item.provenance.repository}/commit/${item.provenance.commit}`}
                target="_blank"
                rel="noreferrer"
              >
                {item.provenance.commit.slice(0, 12)}
              </a>
            )}
          </>
        ) : (
          'Uploaded package'
        )}
      </p>
      <FirmwareDetails item={item} />
      <div className="inline-actions">
        <Button
          variant="secondary"
          disabled={downloading || disabled}
          onClick={() => void saveFile()}
        >
          <Download size={16} /> {downloading ? 'Downloading…' : 'Download package'}
        </Button>
        {onRemove && (
          <Button variant="ghost" disabled={disabled} onClick={() => onRemove(item)}>
            <Trash2 size={16} /> Remove
          </Button>
        )}
      </div>
      <ErrorBanner message={error} />
    </article>
  )
}

export function MachineFirmwareDialog({
  deviceID,
  onClose,
}: {
  deviceID: string
  onClose: () => void
}) {
  const hub = useHub()
  const firmware = useResource<MachineFirmware>(`/devices/${encodeURIComponent(deviceID)}/firmware`)
  const [display, setDisplay] = useState('')
  const [checked, setChecked] = useState(false)
  const [saving, setSaving] = useState(false)
  const [verificationError, setVerificationError] = useState('')
  async function verifyController(event: FormEvent) {
    event.preventDefault()
    if (!checked || !display || saving || !firmware.data?.device_uid) return
    setSaving(true)
    setVerificationError('')
    try {
      await api(`/devices/${encodeURIComponent(deviceID)}/firmware/hardware`, {
        method: 'PUT',
        body: { device_uid: firmware.data.device_uid, display_variant: display },
      })
      setChecked(false)
      setDisplay('')
      firmware.reload()
      hub.notify('Verified display controller saved.')
    } catch (error) {
      setVerificationError(errorMessage(error))
    } finally {
      setSaving(false)
    }
  }
  return (
    <Modal title="Machine firmware" onClose={onClose} wide>
      <ErrorBanner message={firmware.error} retry={firmware.reload} />
      {firmware.loading ? (
        <Loading label="Loading machine firmware…" />
      ) : (
        firmware.data && (
          <div className="machine-firmware">
            <div className="firmware-current">
              <strong>{firmwareModelName(firmware.data.model)}</strong>
              <span>Current version: {firmware.data.current_version || 'Not reported'}</span>
            </div>
            <p role="status">{firmware.data.reason}</p>
            {firmware.data.model === 'pico-c' && (
              <details open={firmware.data.hardware_verification_required}>
                <summary>Display controller verification</summary>
                <p className="help-text">
                  Firmware reports{' '}
                  {firmware.data.display_variant?.toUpperCase() || 'an unknown controller'}. This is
                  a build setting, not hardware detection. Check the physical controller or a
                  documented hardware identification before marking its firmware packages as
                  matching. This check is saved for this machine; it is not required to connect or
                  download packages.
                </p>
                {firmware.data.verified_display_variant && (
                  <p>Verified hardware: {firmware.data.verified_display_variant.toUpperCase()}</p>
                )}
                <form onSubmit={verifyController}>
                  <Field label="Verified display controller">
                    <select
                      value={display}
                      disabled={saving}
                      onChange={(event) => {
                        setDisplay(event.target.value)
                        setChecked(false)
                      }}
                    >
                      <option value="">Choose the controller you checked</option>
                      <option value="ssd1325">SSD1325</option>
                      <option value="ssd1309">SSD1309</option>
                    </select>
                  </Field>
                  <label className="check-label">
                    <input
                      type="checkbox"
                      checked={checked}
                      disabled={saving || !display}
                      onChange={(event) => setChecked(event.target.checked)}
                    />{' '}
                    I checked this machine's display controller independently of its firmware
                    report.
                  </label>
                  <Button
                    type="submit"
                    disabled={saving || !checked || !display || !firmware.data.device_uid}
                  >
                    {saving ? 'Saving…' : 'Save verified controller'}
                  </Button>
                  <ErrorBanner message={verificationError} />
                </form>
              </details>
            )}
            <h3>Matching packages in this Hub</h3>
            {firmware.data.packages.length ? (
              <div className="firmware-package-list">
                {firmware.data.packages.map((item) => (
                  <FirmwarePackageCard key={item.id} item={item} />
                ))}
              </div>
            ) : (
              <p className="help-text">No matching packages are stored in this Hub.</p>
            )}
            <Button
              variant="secondary"
              onClick={() => {
                onClose()
                hub.navigate('/settings/firmware')
              }}
            >
              Manage firmware packages
            </Button>
          </div>
        )
      )}
    </Modal>
  )
}
