import { useState } from 'react'
import { useResource } from '../context'
import { duration } from '../lib/utils'
import type { Device } from '../types'
import { Button, ErrorBanner } from './ui'

interface DeviceLog {
  events: { sequence: number; uptime_ms: number; kind: string; detail: string }[]
}

function DeviceEventLog({ deviceID }: { deviceID: string }) {
  const log = useResource<DeviceLog>(`/devices/${encodeURIComponent(deviceID)}/events`)
  return (
    <>
      <h4>Recent device events</h4>
      <p>
        The machine keeps its last eight events until it restarts. Times are measured from startup.
      </p>
      <ErrorBanner message={log.error} retry={log.reload} />
      {log.loading && <p role="status">Reading events…</p>}
      {!log.loading && !log.error && log.data && (
        <>
          {log.data.events.length ? (
            <ul>
              {[...log.data.events].reverse().map((event) => (
                <li key={event.sequence}>
                  <strong>{event.kind.replace(/[-_]/g, ' ')}</strong> · {duration(event.uptime_ms)}
                  {event.detail && <div>{event.detail}</div>}
                </li>
              ))}
            </ul>
          ) : (
            <p>No events recorded since startup.</p>
          )}
          <Button variant="ghost" onClick={log.reload}>
            Refresh events
          </Button>
        </>
      )}
    </>
  )
}

export function MachineEvents({ device }: { device: Device }) {
  const [open, setOpen] = useState(false)
  if (device.adapter !== 'pico-standalone' || device.capabilities?.events_persistent !== false)
    return null
  const available = device.online && device.enabled
  const visible = open && available
  return (
    <section>
      <Button
        variant="secondary"
        aria-expanded={visible}
        disabled={!available}
        onClick={() => setOpen(!open)}
      >
        {visible ? 'Hide device events' : 'View device events'}
      </Button>
      {visible && <DeviceEventLog key={device.id} deviceID={device.id} />}
    </section>
  )
}
