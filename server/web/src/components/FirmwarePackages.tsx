import { useState } from 'react'
import { Download, Trash2 } from 'lucide-react'
import { useHub, useResource } from '../context'
import { download, errorMessage } from '../lib/api'
import type { FirmwareCandidate, FirmwarePackage, MachineFirmware } from '../types'
import { Button, ErrorBanner, Loading, Modal } from './ui'

export function firmwareModelName(model: string) {
  return model === 'pico-s'
    ? 'Pico S'
    : model === 'pico-pro'
      ? 'Pico Pro'
      : model || 'Not identified'
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
        `${item.model}-${item.firmware_version}.zip`
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
      aria-label={`${firmwareModelName(item.model)} ${item.firmware_version}`}
    >
      <div className="firmware-package-heading">
        <h4>
          {firmwareModelName(item.model)} <span>{item.firmware_version}</span>
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
