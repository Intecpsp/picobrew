import { useEffect, useState } from 'react'
import { Sparkles } from 'lucide-react'
import { api, errorMessage } from '../lib/api'
import { AIWorkbench } from '../pages/Assistant'
import { Badge, Button, ErrorBanner, SectionHeading } from './ui'

export interface AIMonitorState {
  enabled: boolean
  state: 'off' | 'pending' | 'active' | 'finished'
  brew_id?: string
  current_brew_id?: string
  expires_at?: string
  configured: boolean
}

export function BrewAIMonitor({
  deviceID,
  deviceName,
  monitor,
  error,
  reload,
  loading = false,
}: {
  deviceID: string
  deviceName: string
  monitor: AIMonitorState | null
  error: string
  reload: () => void
  loading?: boolean
}) {
  const [busy, setBusy] = useState(false)
  const [failure, setFailure] = useState('')
  const [asking, setAsking] = useState(false)
  const currentBrewID = monitor?.current_brew_id
  const verified = !error && !loading
  const active = monitor?.enabled && monitor.state === 'active' && monitor.brew_id === currentBrewID
  useEffect(() => {
    setAsking(false)
    setFailure('')
  }, [currentBrewID])
  useEffect(() => {
    if (active) setAsking(true)
  }, [active, currentBrewID])

  async function change(enabled: boolean) {
    if (enabled && !verified) return
    setBusy(true)
    setFailure('')
    try {
      await api(`/devices/${deviceID}/ai-monitor`, {
        method: 'PUT',
        body: { enabled, ...(enabled ? { brew_id: currentBrewID } : {}) },
      })
      reload()
    } catch (cause) {
      setFailure(errorMessage(cause))
      reload()
    } finally {
      setBusy(false)
    }
  }

  if (!currentBrewID && monitor?.state !== 'pending' && monitor?.state !== 'finished' && !error)
    return null
  return (
    <>
      <section className="panel live-ai-monitor" aria-label="AI monitoring for this brew">
        <SectionHeading
          title="AI support for this brew"
          description={
            monitor?.state === 'pending'
              ? 'Monitoring is waiting for this new brew session to begin.'
              : monitor?.state === 'finished' && !currentBrewID
                ? 'This session has ended. Its observations stay with the brew journal.'
                : 'Get observations from the current session. You remain in control of the brewer.'
          }
          actions={
            <Badge tone={verified && active && monitor?.configured ? 'green' : 'neutral'}>
              {!verified
                ? 'Verifying session'
                : active
                  ? monitor?.configured
                    ? 'Monitoring on'
                    : 'Monitoring paused'
                  : monitor?.state === 'pending'
                    ? 'Waiting for brew'
                    : 'Monitoring off'}
            </Badge>
          }
        />
        <ErrorBanner message={failure || error} retry={reload} />
        <div className="inline-actions">
          {(currentBrewID || monitor?.state === 'pending') && (
            <Button
              variant="secondary"
              disabled={
                busy ||
                (!active && monitor?.state !== 'pending' && (!monitor?.configured || !verified))
              }
              onClick={() => void change(!(active || monitor?.state === 'pending'))}
            >
              <Sparkles size={16} />
              {busy
                ? 'Saving…'
                : active || monitor?.state === 'pending'
                  ? 'Turn off AI monitoring'
                  : 'Monitor this brew with AI'}
            </Button>
          )}
          {currentBrewID && (
            <Button variant="ghost" onClick={() => setAsking(!asking)}>
              {asking
                ? 'Hide AI observations'
                : !verified
                  ? 'View saved observations'
                  : active
                    ? 'View AI observations'
                    : 'Ask about this brew'}
            </Button>
          )}
          {monitor?.brew_id && (
            <a
              className="button ghost"
              href={`#/history/${encodeURIComponent(monitor.brew_id)}?analysis=1`}
            >
              Open this brew in the journal
            </a>
          )}
        </div>
        {currentBrewID && !monitor?.configured && (
          <p className="help-text">
            <a href="#/settings">Configure an AI provider in Settings</a> to enable monitoring.
          </p>
        )}
      </section>
      {asking && currentBrewID && (
        <AIWorkbench
          key={currentBrewID}
          mode="live"
          deviceID={deviceID}
          brewID={currentBrewID}
          contextName={deviceName}
          liveVerified={verified}
          embedded
        />
      )}
    </>
  )
}
