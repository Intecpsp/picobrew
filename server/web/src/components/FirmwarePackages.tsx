import { useState, type FormEvent } from 'react'
import { Download, RefreshCw } from 'lucide-react'
import { useHub, useResource } from '../context'
import { api, download, errorMessage } from '../lib/api'
import type { FirmwareCandidate, FirmwarePackage, MachineFirmware, FirmwareUpdates } from '../types'
import { Button, ErrorBanner, Field, Loading, Modal } from './ui'

function firmwareModelName(model: string) {
  return model === 'pico-s'
    ? 'Pico S'
    : model === 'pico-pro'
      ? 'Pico Pro'
      : model === 'pico-c'
        ? 'Pico C'
        : {
            'pico-s-pro': 'Historical Pico S/Pro',
            'z-series': 'Pico Z',
            picoferm: 'PicoFerm',
            picostill: 'PicoStill',
          }[model] ||
          model ||
          'Not identified'
}

export function MachineFirmwareDialog({
  deviceID,
  onClose,
}: {
  deviceID: string
  onClose: () => void
}) {
  const hub = useHub()
  const firmware = useResource<MachineFirmware>(
    `/devices/${encodeURIComponent(deviceID)}/firmware`,
    4000
  )
  const [display, setDisplay] = useState('')
  const [checked, setChecked] = useState(false)
  const [saving, setSaving] = useState(false)
  const [verificationError, setVerificationError] = useState('')
  const updates = useResource<FirmwareUpdates>('/firmware/updates', 60000)
  const [chosen, setChosen] = useState('')
  const [activity, setActivity] = useState('')
  const machine = firmware.data
  const model = machine?.model || hub.devices?.find((d) => d.id === deviceID)?.model
  const items = new Map<string, FirmwareCandidate & { remote: boolean; kind?: string }>()
  for (const item of updates.data?.releases || []) {
    const same =
      item.model === model &&
      (model !== 'pico-c' ||
        (!machine?.hardware_verification_required &&
          item.display_variant === machine?.display_variant))
    if (same || (item.kind === 'historical' && (model === 'pico-s' || model === 'pico-pro'))) {
      items.set(item.archive_sha256, { ...item, remote: true })
    }
  }
  for (const item of machine?.packages || []) {
    if (!items.has(item.id)) items.set(item.id, { ...item, remote: false })
  }
  const versions = [...items].sort(
    ([, a], [, b]) =>
      Number(b.kind !== 'historical') - Number(a.kind !== 'historical') ||
      Number(a.firmware_version.startsWith('preview')) -
        Number(b.firmware_version.startsWith('preview')) ||
      b.firmware_version.localeCompare(a.firmware_version, 'en', { numeric: true })
  )
  const id = items.has(chosen)
    ? chosen
    : machine?.retry_package_id && items.has(machine.retry_package_id)
      ? machine.retry_package_id
      : versions[0]?.[0]
  const selected = id ? items.get(id) : undefined
  const canInstall =
    machine?.install_supported &&
    selected?.hub_ota_protocol === 1 &&
    (!machine.retry_package_id || machine.retry_package_id === id)
  async function checkUpdates() {
    setActivity('Checking…')
    setVerificationError('')
    try {
      updates.setData(await api<FirmwareUpdates>('/firmware/updates', { method: 'POST' }))
    } catch (error) {
      setVerificationError(errorMessage(error))
    } finally {
      setActivity('')
    }
  }
  async function useVersion(install: boolean) {
    if (!id || !selected || !machine || (install && !canInstall)) return
    setVerificationError('')
    setActivity(install ? 'Preparing update…' : 'Downloading…')
    try {
      if (selected.remote) {
        const saved = await api<FirmwarePackage>(`/firmware/releases/${id}/stage`, {
          method: 'POST',
        })
        if (saved.id !== id)
          throw new Error('The staged package does not match the selected release.')
      }
      if (install) {
        await api(`/devices/${encodeURIComponent(deviceID)}/firmware/install`, {
          method: 'POST',
          body: { package_id: id, device_uid: machine.device_uid, confirm: true },
        })
        hub.notify('Firmware update started.')
      } else {
        await download(
          `/firmware/packages/${id}/download`,
          `${selected.model}-${selected.display_variant ? selected.display_variant + '-' : ''}${selected.firmware_version}.zip`
        )
      }
      firmware.reload()
    } catch (error) {
      setVerificationError(errorMessage(error))
    } finally {
      setActivity('')
    }
  }
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
              <strong>
                {hub.devices?.find((d) => d.id === deviceID)?.name ||
                  firmwareModelName(model || '')}
              </strong>
              <span>Current version: {firmware.data.current_version || 'Not reported'}</span>
            </div>
            <p role="status">{firmware.data.reason}</p>
            {firmware.data.last_install && (
              <p role="status">
                Last update ({firmware.data.last_install.state}):{' '}
                {firmware.data.last_install.message}
              </p>
            )}
            <ErrorBanner message={verificationError} />
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
                </form>
              </details>
            )}
            <Field label="Firmware version">
              <select
                value={id || ''}
                disabled={!versions.length || !!activity}
                onChange={(e) => setChosen(e.target.value)}
              >
                {!versions.length && <option value="">No versions available</option>}
                {versions.map(([key, v]) => (
                  <option key={key} value={key}>
                    {v.firmware_version}
                    {v.display_variant ? ' · ' + v.display_variant.toUpperCase() : ''}
                    {v.kind === 'historical'
                      ? ' · Historical S/Pro build'
                      : v.kind === 'reference'
                        ? ' · Reference package'
                        : ''}
                  </option>
                ))}
              </select>
            </Field>
            <div className="inline-actions">
              {canInstall && (
                <Button disabled={!!activity || saving} onClick={() => void useVersion(true)}>
                  Install
                </Button>
              )}
              <Button
                variant="secondary"
                disabled={!selected || !!activity}
                onClick={() => void useVersion(false)}
              >
                <Download size={16} />
                Download
              </Button>
              <Button variant="secondary" disabled={!!activity} onClick={() => void checkUpdates()}>
                <RefreshCw size={16} />
                Check for Updates
              </Button>
            </div>
            {activity && <p role="status">{activity}</p>}
            {updates.data?.checked && (
              <p className="help-text">
                Last checked {new Date(updates.data.checked).toLocaleString()}
              </p>
            )}
            <ErrorBanner message={updates.error} />
          </div>
        )
      )}
    </Modal>
  )
}
