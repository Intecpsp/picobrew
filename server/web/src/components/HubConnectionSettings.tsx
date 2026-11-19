import { useEffect, useState, type FormEvent } from 'react'
import { useHub, useResource } from '../context'
import { api, errorMessage } from '../lib/api'
import type { Device, Telemetry } from '../types'
import { HubConnectionStatus } from './DeviceDialogs'
import { Button, ErrorBanner, Field, Loading } from './ui'

export function HubConnectionSettings({ device }: { device: Device }) {
  const hub = useHub()
  const [expanded, setExpanded] = useState(false)
  const configurable =
    device.adapter === 'pico-standalone' && !!device.capabilities?.telemetry_forwarding
  const connection = useResource<Telemetry>(
    configurable && expanded ? `/devices/${device.id}/telemetry` : null
  )
  const [form, setForm] = useState({
    mode: device.telemetry?.mode || 'automatic',
    enabled: device.telemetry?.enabled ?? true,
    host: device.telemetry?.host || '',
    port: device.telemetry?.port || 80,
  })
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState('')
  useEffect(() => {
    if (connection.data) {
      setForm({
        mode: connection.data.mode || device.telemetry?.mode || 'automatic',
        enabled: connection.data.enabled,
        host: connection.data.host,
        port: connection.data.port,
      })
    }
  }, [connection.data, device.telemetry?.mode])
  const currentMode = connection.data?.mode || device.telemetry?.mode || 'automatic'
  async function save(event: FormEvent) {
    event.preventDefault()
    if (busy || connection.loading || !connection.data) return
    setBusy(true)
    setError('')
    try {
      await api(`/devices/${device.id}/telemetry`, {
        method: 'PUT',
        body: form.mode === 'manual' ? form : { mode: 'automatic' },
      })
      await hub.refresh()
      connection.reload()
      hub.notify(
        form.mode === 'manual' ? 'Manual connection saved.' : 'Automatic Hub connection restored.'
      )
    } catch (error) {
      setError(errorMessage(error))
    } finally {
      setBusy(false)
    }
  }
  return (
    <>
      <HubConnectionStatus device={device} />
      {configurable && (
        <details
          className="advanced-connection"
          onToggle={(event) => setExpanded(event.currentTarget.open)}
        >
          <summary>Advanced Hub connection</summary>
          <p className="help-text">
            The Hub normally configures live readings automatically. Use a manual receiver for a
            custom network setup. Manual settings stay in place when the machine reconnects.
          </p>
          <ErrorBanner
            message={error || connection.error}
            retry={connection.error ? connection.reload : undefined}
          />
          {connection.loading ? (
            <Loading />
          ) : connection.data ? (
            <form onSubmit={save}>
              <Field label="Connection management">
                <select
                  value={form.mode}
                  disabled={busy}
                  onChange={(event) =>
                    setForm({ ...form, mode: event.target.value as 'automatic' | 'manual' })
                  }
                >
                  <option value="automatic">Automatic · this Hub</option>
                  <option value="manual">Manual receiver</option>
                </select>
              </Field>
              {form.mode === 'manual' && (
                <>
                  <label className="check-field">
                    <input
                      type="checkbox"
                      checked={form.enabled}
                      disabled={busy}
                      onChange={(event) => setForm({ ...form, enabled: event.target.checked })}
                    />
                    Send live readings
                  </label>
                  <div className="form-grid">
                    <Field
                      label="Receiver host"
                      hint="A host name or IPv4 address reachable from the machine."
                    >
                      <input
                        value={form.host}
                        required={form.enabled}
                        maxLength={127}
                        disabled={busy}
                        onChange={(event) => setForm({ ...form, host: event.target.value })}
                      />
                    </Field>
                    <Field label="Receiver port">
                      <input
                        type="number"
                        min="1"
                        max="65535"
                        required
                        disabled={busy}
                        value={form.port}
                        onChange={(event) => setForm({ ...form, port: Number(event.target.value) })}
                      />
                    </Field>
                  </div>
                </>
              )}
              <Button
                type="submit"
                disabled={busy || (form.mode === 'automatic' && currentMode === 'automatic')}
              >
                {busy
                  ? 'Saving connection…'
                  : form.mode === 'manual'
                    ? 'Save manual connection'
                    : 'Return to automatic'}
              </Button>
            </form>
          ) : null}
        </details>
      )}
    </>
  )
}
