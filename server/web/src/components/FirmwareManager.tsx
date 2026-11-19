import { useEffect, useRef, useState, type FormEvent } from 'react'
import { Download, RefreshCw, Upload } from 'lucide-react'
import { useHub, useResource } from '../context'
import { api, APIError, errorMessage } from '../lib/api'
import type { FirmwareCatalog, FirmwarePackage, FirmwareSettings } from '../types'
import { FirmwareDetails, FirmwarePackageCard, firmwareModelName } from './FirmwarePackages'
import { Button, Confirm, ErrorBanner, Field, Loading } from './ui'

const defaultSource: FirmwareSettings = {
  repository: 'jeffreydwalter/picobrew-pico-pro-firmware',
  ref: '',
}
const maxPackageBytes = 2 * 1024 * 1024

export function FirmwareManager() {
  const hub = useHub()
  const settings = useResource<FirmwareSettings>('/firmware/settings')
  const packages = useResource<FirmwarePackage[]>('/firmware/packages')
  const [source, setSource] = useState(defaultSource)
  const [sourceLoaded, setSourceLoaded] = useState(false)
  const [sourceDirty, setSourceDirty] = useState(false)
  const dirty = useRef(false)
  const [sourceSaving, setSourceSaving] = useState(false)
  const [sourceError, setSourceError] = useState('')
  const [catalog, setCatalog] = useState<FirmwareCatalog | null>(null)
  const [checking, setChecking] = useState(false)
  const [githubError, setGithubError] = useState('')
  const catalogRequest = useRef(0)
  const catalogController = useRef<AbortController | null>(null)
  const [importing, setImporting] = useState('')
  const [file, setFile] = useState<File | null>(null)
  const fileInput = useRef<HTMLInputElement>(null)
  const [uploading, setUploading] = useState(false)
  const [uploadError, setUploadError] = useState('')
  const [removing, setRemoving] = useState<FirmwarePackage | null>(null)
  const [deleting, setDeleting] = useState(false)
  const [removeError, setRemoveError] = useState('')

  useEffect(() => {
    if (!settings.data) return
    if (!dirty.current) setSource(settings.data)
    setSourceLoaded(true)
  }, [settings.data])
  useEffect(() => () => catalogController.current?.abort(), [])

  function invalidateCatalog() {
    catalogRequest.current++
    catalogController.current?.abort()
    setCatalog(null)
    setChecking(false)
    setGithubError('')
  }
  function editSource(next: FirmwareSettings) {
    dirty.current = true
    setSourceDirty(true)
    setSource(next)
    setSourceError('')
    invalidateCatalog()
  }
  async function saveSource(event: FormEvent) {
    event.preventDefault()
    if (!sourceLoaded || settings.loading || sourceSaving || importing) return
    setSourceSaving(true)
    setSourceError('')
    invalidateCatalog()
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
  async function checkGitHub() {
    if (!sourceLoaded || settings.loading || sourceDirty || sourceSaving || importing || checking)
      return
    const request = ++catalogRequest.current
    catalogController.current?.abort()
    const controller = new AbortController()
    catalogController.current = controller
    setChecking(true)
    setCatalog(null)
    setGithubError('')
    try {
      const result = await api<FirmwareCatalog>('/firmware/github', { signal: controller.signal })
      if (request !== catalogRequest.current) return
      if (result.repository !== source.repository || result.ref !== source.ref) {
        setGithubError('The saved firmware source changed. Check GitHub again after it reloads.')
        settings.reload()
        return
      }
      setCatalog(result)
    } catch (error) {
      if (request === catalogRequest.current && !controller.signal.aborted)
        setGithubError(errorMessage(error))
    } finally {
      if (request === catalogRequest.current) setChecking(false)
    }
  }
  function addPackage(item: FirmwarePackage) {
    packages.setData((current) => [
      item,
      ...(current || []).filter((existing) => existing.id !== item.id),
    ])
  }
  async function fetchPackage(model: string) {
    if (!catalog || importing || packages.loading) return
    setImporting(model)
    setGithubError('')
    try {
      const item = await api<FirmwarePackage>('/firmware/github/import', {
        method: 'POST',
        body: { repository: catalog.repository, commit: catalog.commit, model },
      })
      addPackage(item)
      hub.notify('Firmware package saved to the Hub.')
    } catch (error) {
      if (error instanceof APIError && error.status === 409) invalidateCatalog()
      if (error instanceof APIError && error.code === 'firmware_source_changed') settings.reload()
      setGithubError(errorMessage(error))
    } finally {
      setImporting('')
    }
  }
  async function uploadPackage(event: FormEvent) {
    event.preventDefault()
    if (!file || uploading || packages.loading) return
    setUploadError('')
    if (!file.name.toLowerCase().endsWith('.zip')) {
      setUploadError(
        'Choose a firmware package ZIP. A machine image alone is not a complete package.'
      )
      return
    }
    if (file.size > maxPackageBytes) {
      setUploadError('Firmware packages must be 2 MB or smaller.')
      return
    }
    setUploading(true)
    try {
      const body = new FormData()
      body.append('file', file)
      addPackage(await api<FirmwarePackage>('/firmware/packages', { method: 'POST', body }))
      setFile(null)
      if (fileInput.current) fileInput.current.value = ''
      hub.notify('Firmware package uploaded.')
    } catch (error) {
      setUploadError(errorMessage(error))
    } finally {
      setUploading(false)
    }
  }
  async function removePackage() {
    if (!removing || deleting) return
    setDeleting(true)
    setRemoveError('')
    try {
      await api(`/firmware/packages/${encodeURIComponent(removing.id)}`, { method: 'DELETE' })
      packages.setData((current) => current?.filter((item) => item.id !== removing.id) || [])
      setRemoving(null)
      hub.notify('Firmware package removed from the Hub.')
    } catch (error) {
      setRemoveError(errorMessage(error))
    } finally {
      setDeleting(false)
    }
  }

  return (
    <div className="firmware-manager">
      <form onSubmit={saveSource}>
        <Field label="GitHub repository" hint="owner/repository">
          <input
            value={source.repository}
            required
            disabled={sourceSaving || !!importing}
            onChange={(event) => editSource({ ...source, repository: event.target.value })}
          />
        </Field>
        <Field label="Branch or tag" hint="Leave blank to use the repository's default branch.">
          <input
            value={source.ref}
            placeholder="Default branch"
            disabled={sourceSaving || !!importing}
            onChange={(event) => editSource({ ...source, ref: event.target.value })}
          />
        </Field>
        <div className="inline-actions">
          <Button
            type="submit"
            variant="secondary"
            disabled={
              !sourceLoaded || settings.loading || !sourceDirty || sourceSaving || !!importing
            }
          >
            {sourceSaving ? 'Saving source…' : 'Save source'}
          </Button>
          <Button
            type="button"
            disabled={
              !sourceLoaded ||
              settings.loading ||
              sourceDirty ||
              sourceSaving ||
              checking ||
              !!importing
            }
            onClick={() => void checkGitHub()}
          >
            <RefreshCw size={16} className={checking ? 'spin' : ''} />{' '}
            {checking ? 'Checking GitHub…' : 'Check GitHub'}
          </Button>
        </div>
        {sourceDirty && (
          <p className="help-text" role="status">
            Save the source before checking GitHub.
          </p>
        )}
        <ErrorBanner
          message={sourceError || settings.error}
          retry={settings.error ? settings.reload : undefined}
        />
      </form>
      <ErrorBanner message={githubError} />
      {catalog && (
        <section className="firmware-catalog" aria-label="Packages on GitHub">
          <h3>Packages on GitHub</h3>
          <p className="help-text">
            {catalog.repository} · {catalog.ref || 'Default branch'} · {catalog.commit.slice(0, 12)}
          </p>
          {catalog.packages.length ? (
            catalog.packages.map((item) => (
              <article
                className="firmware-package"
                key={item.model}
                aria-label={`${firmwareModelName(item.model)} ${item.firmware_version}`}
              >
                <h4>
                  {firmwareModelName(item.model)} <span>{item.firmware_version}</span>
                </h4>
                <FirmwareDetails item={item} />
                <Button
                  disabled={!!importing || packages.loading}
                  onClick={() => void fetchPackage(item.model)}
                >
                  <Download size={16} /> {importing === item.model ? 'Fetching…' : 'Fetch to Hub'}
                </Button>
              </article>
            ))
          ) : (
            <p className="help-text">No supported firmware packages were found at this source.</p>
          )}
        </section>
      )}
      <form className="firmware-upload" onSubmit={uploadPackage}>
        <Field
          label="Upload firmware package (.zip)"
          hint="A complete firmware package, up to 2 MB. A .bin file alone is not enough."
        >
          <input
            ref={fileInput}
            type="file"
            accept=".zip,application/zip"
            disabled={uploading}
            onChange={(event) => {
              setFile(event.target.files?.[0] || null)
              setUploadError('')
            }}
          />
        </Field>
        <Button type="submit" variant="secondary" disabled={!file || uploading || packages.loading}>
          <Upload size={16} /> {uploading ? 'Uploading…' : 'Upload package'}
        </Button>
        <ErrorBanner message={uploadError} />
      </form>
      <section aria-label="Packages in this Hub">
        <h3>Packages in this Hub</h3>
        <ErrorBanner message={packages.error} retry={packages.reload} />
        {packages.loading ? (
          <Loading label="Loading firmware packages…" />
        ) : packages.data?.length ? (
          <div className="firmware-package-list">
            {packages.data.map((item) => (
              <FirmwarePackageCard
                key={item.id}
                item={item}
                disabled={deleting}
                onRemove={(item) => {
                  setRemoving(item)
                  setRemoveError('')
                }}
              />
            ))}
          </div>
        ) : (
          !packages.error && <p className="help-text">No firmware packages are stored yet.</p>
        )}
      </section>
      {removing && (
        <Confirm
          title={`Remove ${firmwareModelName(removing.model)} ${removing.firmware_version}?`}
          description="Remove this stored package from the Hub? Firmware already on a machine is unchanged."
          label="Remove package"
          danger
          busy={deleting}
          onClose={() => {
            if (!deleting) setRemoving(null)
          }}
          onConfirm={() => void removePackage()}
        >
          <ErrorBanner message={removeError} />
        </Confirm>
      )}
    </div>
  )
}
