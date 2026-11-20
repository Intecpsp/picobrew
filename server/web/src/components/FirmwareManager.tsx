import { useEffect, useRef, useState, type FormEvent } from 'react'
import { useHub, useResource } from '../context'
import { api, errorMessage } from '../lib/api'
import type { FirmwareSettings } from '../types'
import { Button, ErrorBanner, Field } from './ui'

const defaultSource: FirmwareSettings = {
  repository: 'jeffreydwalter/picobrew-pico-pro-firmware',
  ref: '',
}

export function FirmwareManager() {
  const hub = useHub()
  const settings = useResource<FirmwareSettings>('/firmware/settings')
  const [source, setSource] = useState(defaultSource)
  const [sourceLoaded, setSourceLoaded] = useState(false)
  const [sourceDirty, setSourceDirty] = useState(false)
  const dirty = useRef(false)
  const [sourceSaving, setSourceSaving] = useState(false)
  const [sourceError, setSourceError] = useState('')
  useEffect(() => {
    if (!settings.data) return
    if (!dirty.current) setSource(settings.data)
    setSourceLoaded(true)
  }, [settings.data])
  function editSource(next: FirmwareSettings) {
    dirty.current = true
    setSourceDirty(true)
    setSource(next)
    setSourceError('')
  }
  async function saveSource(event: FormEvent) {
    event.preventDefault()
    if (!sourceLoaded || settings.loading || sourceSaving) return
    setSourceSaving(true)
    setSourceError('')
    try {
      const result = await api<FirmwareSettings>('/firmware/settings', {
        method: 'PUT',
        body: source,
      })
      setSource(result)
      dirty.current = false
      setSourceDirty(false)
      hub.notify('Firmware source saved.')
    } catch (error) {
      setSourceError(errorMessage(error))
    } finally {
      setSourceSaving(false)
    }
  }
  return (
    <div className="firmware-manager">
      <form onSubmit={saveSource}>
        <Field label="GitHub repository" hint="owner/repository">
          <input
            value={source.repository}
            required
            disabled={sourceSaving}
            onChange={(event) => editSource({ ...source, repository: event.target.value })}
          />
        </Field>
        <Field label="Branch or tag" hint="Leave blank to use the repository's default branch.">
          <input
            value={source.ref}
            placeholder="Default branch"
            disabled={sourceSaving}
            onChange={(event) => editSource({ ...source, ref: event.target.value })}
          />
        </Field>
        <div className="inline-actions">
          <Button
            type="submit"
            variant="secondary"
            disabled={!sourceLoaded || settings.loading || !sourceDirty || sourceSaving}
          >
            {sourceSaving ? 'Saving source…' : 'Save source'}
          </Button>
        </div>
        <ErrorBanner
          message={sourceError || settings.error}
          retry={settings.error ? settings.reload : undefined}
        />
      </form>
    </div>
  )
}
