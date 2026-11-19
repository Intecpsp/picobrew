// @vitest-environment jsdom
import { createElement, type ReactNode } from 'react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { act, cleanup, render, screen, waitFor, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { HubContext, type HubState } from '../context'
import { setCSRF } from '../lib/api'
import type { FirmwareCatalog, FirmwarePackage, FirmwareSettings } from '../types'
import { FirmwareManager } from './FirmwareManager'
import { MachineFirmwareDialog } from './FirmwarePackages'

const fetchMock = vi.fn<typeof fetch>()
const notify = vi.fn()
const navigate = vi.fn()
const close = vi.fn()
const originalShowModal = Object.getOwnPropertyDescriptor(HTMLDialogElement.prototype, 'showModal')
const defaultRepository = 'jeffreydwalter/picobrew-pico-pro-firmware'
const commit = 'a'.repeat(40)
const candidate = {
  model: 'pico-s',
  model_family: 'pico-s-pro',
  firmware_version: '1.1.0',
  binary_sha256: 'b'.repeat(64),
  binary_bytes: 180000,
}
const uploaded: FirmwarePackage = {
  ...candidate,
  id: 'c'.repeat(64),
  package_bytes: 210000,
  created_at: '2026-11-19T12:00:00Z',
  validation: 'passed',
  provenance: { source: 'upload' },
}
let source: FirmwareSettings
let stored: FirmwarePackage[]

function json(value: unknown, status = 200) {
  return new Response(JSON.stringify(value), {
    status,
    headers: { 'Content-Type': 'application/json' },
  })
}
function catalog(): FirmwareCatalog {
  return { ...source, commit, packages: [candidate] }
}
async function response(path: RequestInfo | URL, options?: RequestInit) {
  const url = String(path)
  if (url.endsWith('/firmware/settings')) {
    if (options?.method === 'PUT') source = JSON.parse(String(options.body))
    return json(source)
  }
  if (url.endsWith('/firmware/github')) return json(catalog())
  if (url.endsWith('/firmware/github/import')) {
    const item = {
      ...uploaded,
      provenance: { source: 'github' as const, repository: source.repository, commit },
    }
    stored = [item]
    return json(item)
  }
  if (url.endsWith('/firmware/packages')) {
    if (options?.method === 'POST') {
      stored = [uploaded]
      return json(uploaded, 201)
    }
    return json(stored)
  }
  if (options?.method === 'DELETE') {
    stored = stored.filter((item) => !url.endsWith(item.id))
    return json({ deleted: true })
  }
  throw new Error(`Unexpected request: ${url}`)
}
function show(child: ReactNode = createElement(FirmwareManager)) {
  const hub = { notify, navigate } as unknown as HubState
  return render(createElement(HubContext.Provider, { value: hub }, child))
}
function calls(path: string, method = 'GET') {
  return fetchMock.mock.calls.filter(
    ([url, options]) => String(url).endsWith(path) && (options?.method || 'GET') === method
  )
}
async function ready() {
  await waitFor(() =>
    expect(
      (screen.getByRole('button', { name: 'Check GitHub' }) as HTMLButtonElement).disabled
    ).toBe(false)
  )
  await waitFor(() => expect(screen.queryByText('Loading firmware packages…')).toBeNull())
}

beforeEach(() => {
  source = { repository: defaultRepository, ref: '' }
  stored = []
  fetchMock.mockReset()
  fetchMock.mockImplementation(response)
  notify.mockReset()
  navigate.mockReset()
  close.mockReset()
  vi.stubGlobal('fetch', fetchMock)
  setCSRF('test-session')
  Object.defineProperty(HTMLDialogElement.prototype, 'showModal', {
    configurable: true,
    value(this: HTMLDialogElement) {
      this.open = true
    },
  })
})
afterEach(() => {
  cleanup()
  vi.restoreAllMocks()
  vi.unstubAllGlobals()
  if (originalShowModal)
    Object.defineProperty(HTMLDialogElement.prototype, 'showModal', originalShowModal)
  else Reflect.deleteProperty(HTMLDialogElement.prototype, 'showModal')
})

describe('Hub firmware packages', () => {
  it('checks GitHub only on request and imports the reviewed model at its pinned commit', async () => {
    const user = userEvent.setup()
    show()
    await ready()
    expect((screen.getByLabelText('GitHub repository') as HTMLInputElement).value).toBe(
      defaultRepository
    )
    expect(calls('/firmware/github')).toHaveLength(0)
    await user.click(screen.getByRole('button', { name: 'Check GitHub' }))
    const found = await screen.findByRole('region', { name: 'Packages on GitHub' })
    expect(within(found).getByRole('article', { name: 'Pico S 1.1.0' })).toBeTruthy()
    await user.click(within(found).getByRole('button', { name: 'Fetch to Hub' }))
    await waitFor(() => expect(notify).toHaveBeenCalledWith('Firmware package saved to the Hub.'))
    expect(JSON.parse(String(calls('/firmware/github/import', 'POST')[0][1]?.body))).toEqual({
      repository: defaultRepository,
      commit,
      model: 'pico-s',
    })
    const saved = screen.getByRole('region', { name: 'Packages in this Hub' })
    expect(within(saved).getByText(/GitHub · jeffreydwalter/)).toBeTruthy()
    expect(within(saved).getByRole('link').getAttribute('href')).toBe(
      `https://github.com/${defaultRepository}/commit/${commit}`
    )
    expect(within(saved).getByRole('button', { name: 'Download package' })).toBeTruthy()
    expect(screen.queryByRole('button', { name: /^Install/ })).toBeNull()
  })

  it('saves an edited repository and branch, clearing candidates until they are checked again', async () => {
    const user = userEvent.setup()
    show()
    await ready()
    await user.click(screen.getByRole('button', { name: 'Check GitHub' }))
    await screen.findByRole('region', { name: 'Packages on GitHub' })
    await user.clear(screen.getByLabelText('GitHub repository'))
    await user.type(screen.getByLabelText('GitHub repository'), 'brewer/firmware')
    await user.type(screen.getByLabelText('Branch or tag'), 'stable')
    expect(screen.queryByRole('region', { name: 'Packages on GitHub' })).toBeNull()
    expect(
      (screen.getByRole('button', { name: 'Check GitHub' }) as HTMLButtonElement).disabled
    ).toBe(true)
    await user.click(screen.getByRole('button', { name: 'Save source' }))
    await waitFor(() => expect(notify).toHaveBeenCalledWith('Firmware source saved.'))
    expect(source).toEqual({ repository: 'brewer/firmware', ref: 'stable' })
    expect(calls('/firmware/github')).toHaveLength(1)
    await user.click(screen.getByRole('button', { name: 'Check GitHub' }))
    await screen.findByText(/brewer\/firmware · stable/)
  })

  it('preserves source edits while the first saved-source response is still loading', async () => {
    let finish!: (value: Response) => void
    fetchMock.mockImplementation((path, options) =>
      String(path).endsWith('/firmware/settings')
        ? new Promise<Response>((resolve) => {
            finish = resolve
          })
        : response(path, options)
    )
    const user = userEvent.setup()
    show()
    await user.clear(screen.getByLabelText('GitHub repository'))
    await user.type(screen.getByLabelText('GitHub repository'), 'my/unfinished-source')
    await user.type(screen.getByLabelText('Branch or tag'), 'branch-in-progress')
    await act(async () => finish(json(source)))
    expect((screen.getByLabelText('GitHub repository') as HTMLInputElement).value).toBe(
      'my/unfinished-source'
    )
    expect((screen.getByLabelText('Branch or tag') as HTMLInputElement).value).toBe(
      'branch-in-progress'
    )
    expect(screen.getByText('Save the source before checking GitHub.')).toBeTruthy()
  })

  it('ignores a late GitHub response after the user changes the source', async () => {
    let finish!: (value: Response) => void
    fetchMock.mockImplementation((path, options) =>
      String(path).endsWith('/firmware/github')
        ? new Promise<Response>((resolve) => {
            finish = resolve
          })
        : response(path, options)
    )
    const user = userEvent.setup()
    show()
    await ready()
    await user.click(screen.getByRole('button', { name: 'Check GitHub' }))
    await user.type(screen.getByLabelText('Branch or tag'), 'new-branch')
    await act(async () => finish(json(catalog())))
    expect(screen.queryByRole('region', { name: 'Packages on GitHub' })).toBeNull()
    expect(calls('/firmware/github/import', 'POST')).toHaveLength(0)
  })

  it('clears a stale GitHub catalog when the server rejects a changed source', async () => {
    fetchMock.mockImplementation((path, options) =>
      String(path).endsWith('/firmware/github/import')
        ? Promise.resolve(
            json(
              {
                error: {
                  code: 'firmware_source_changed',
                  message: 'The firmware source changed. Check GitHub again.',
                },
              },
              409
            )
          )
        : response(path, options)
    )
    const user = userEvent.setup()
    show()
    await ready()
    await user.click(screen.getByRole('button', { name: 'Check GitHub' }))
    await user.click(await screen.findByRole('button', { name: 'Fetch to Hub' }))
    await screen.findByText('The firmware source changed. Check GitHub again.')
    expect(screen.queryByRole('region', { name: 'Packages on GitHub' })).toBeNull()
    expect(screen.getByText('No firmware packages are stored yet.')).toBeTruthy()
  })

  it('uploads a ZIP through the shared authenticated form transport and displays uploaded provenance', async () => {
    const user = userEvent.setup()
    show()
    await ready()
    const file = new File(['complete package fixture'], 'pico-s.zip', { type: 'application/zip' })
    await user.upload(screen.getByLabelText('Upload firmware package (.zip)'), file)
    await user.click(screen.getByRole('button', { name: 'Upload package' }))
    await waitFor(() => expect(notify).toHaveBeenCalledWith('Firmware package uploaded.'))
    const request = calls('/firmware/packages', 'POST')[0][1]
    expect((request?.body as FormData).get('file')).toBe(file)
    expect(request?.headers).toEqual({ Accept: 'application/json', 'X-CSRF-Token': 'test-session' })
    expect(screen.getByText('Uploaded package')).toBeTruthy()
    expect(
      (screen.getByLabelText('Upload firmware package (.zip)') as HTMLInputElement).value
    ).toBe('')
  })

  it('rejects oversized files before upload and preserves the server validation error for an invalid package', async () => {
    const user = userEvent.setup()
    show()
    await ready()
    await user.upload(
      screen.getByLabelText('Upload firmware package (.zip)'),
      new File([new Uint8Array(2 * 1024 * 1024 + 1)], 'too-large.zip', { type: 'application/zip' })
    )
    await user.click(screen.getByRole('button', { name: 'Upload package' }))
    await screen.findByText('Firmware packages must be 2 MB or smaller.')
    expect(calls('/firmware/packages', 'POST')).toHaveLength(0)
    fetchMock.mockImplementation((path, options) =>
      options?.method === 'POST'
        ? Promise.resolve(
            json(
              {
                error: {
                  code: 'invalid_firmware_package',
                  message: 'The image hash does not match the package report.',
                },
              },
              400
            )
          )
        : response(path, options)
    )
    await user.upload(
      screen.getByLabelText('Upload firmware package (.zip)'),
      new File(['invalid'], 'bad.zip', { type: 'application/zip' })
    )
    await user.click(screen.getByRole('button', { name: 'Upload package' }))
    await screen.findByText('The image hash does not match the package report.')
    expect(notify).not.toHaveBeenCalled()
    expect(screen.getByText('No firmware packages are stored yet.')).toBeTruthy()
  })

  it('removes only the selected stored package after explicit confirmation', async () => {
    stored = [uploaded]
    const user = userEvent.setup()
    show()
    await ready()
    await user.click(screen.getByRole('button', { name: 'Remove' }))
    const dialog = screen.getByRole('dialog', { name: 'Remove Pico S 1.1.0?' })
    expect(calls(`/firmware/packages/${uploaded.id}`, 'DELETE')).toHaveLength(0)
    await user.click(within(dialog).getByRole('button', { name: 'Remove package' }))
    await screen.findByText('No firmware packages are stored yet.')
    expect(calls(`/firmware/packages/${uploaded.id}`, 'DELETE')).toHaveLength(1)
    expect(notify).toHaveBeenCalledWith('Firmware package removed from the Hub.')
  })

  it('shows machine-specific readiness and matching downloads without inventing installation support', async () => {
    fetchMock.mockResolvedValue(
      json({
        device_id: 'pico-s',
        model: 'pico-s',
        current_version: '1.0.0',
        install_supported: false,
        reason: 'This machine cannot install firmware from the Hub.',
        packages: [uploaded],
      })
    )
    const user = userEvent.setup()
    show(createElement(MachineFirmwareDialog, { deviceID: 'pico-s', onClose: close }))
    await screen.findByText('This machine cannot install firmware from the Hub.')
    expect(screen.getByText('Current version: 1.0.0')).toBeTruthy()
    expect(screen.getByRole('button', { name: 'Download package' })).toBeTruthy()
    expect(screen.queryByRole('button', { name: /^Install/ })).toBeNull()
    expect(calls('/devices/pico-s/firmware')).toHaveLength(1)
    expect(calls('/firmware/packages')).toHaveLength(0)
    await user.click(screen.getByRole('button', { name: 'Manage firmware packages' }))
    expect(navigate).toHaveBeenCalledWith('/settings/firmware')
    expect(close).toHaveBeenCalled()
  })

  it('does not assume a package model when the machine identity is unknown', async () => {
    fetchMock.mockResolvedValue(
      json({
        device_id: 'unknown',
        model: '',
        current_version: '',
        install_supported: false,
        reason: 'Identify the machine before choosing firmware.',
        packages: [],
      })
    )
    show(createElement(MachineFirmwareDialog, { deviceID: 'unknown', onClose: close }))
    await screen.findByText('Identify the machine before choosing firmware.')
    expect(screen.getByText('Not identified')).toBeTruthy()
    expect(screen.getByText('No matching packages are stored in this Hub.')).toBeTruthy()
    expect(screen.queryByRole('button', { name: 'Download package' })).toBeNull()
  })
})
