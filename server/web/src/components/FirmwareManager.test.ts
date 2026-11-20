// @vitest-environment jsdom
import { createElement, type ReactNode } from 'react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { cleanup, render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { HubContext, type HubState } from '../context'
import { setCSRF } from '../lib/api'
import type { FirmwarePackage, FirmwareSettings } from '../types'
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

function json(value: unknown, status = 200) {
  return new Response(JSON.stringify(value), {
    status,
    headers: { 'Content-Type': 'application/json' },
  })
}
async function response(path: RequestInfo | URL, options?: RequestInit) {
  const url = String(path)
  if (url.endsWith('/firmware/settings')) {
    if (options?.method === 'PUT') source = JSON.parse(String(options.body))
    return json(source)
  }
  if (url.endsWith('/firmware/updates')) return json({ releases: [], notices: [] })
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
beforeEach(() => {
  source = { repository: defaultRepository, ref: '' }
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
  it('keeps only source controls in settings', async () => {
    show()
    await screen.findByDisplayValue(defaultRepository)
    expect(screen.queryByRole('button', { name: 'Check for Updates' })).toBeNull()
    expect(screen.queryByText('Packages in this Hub')).toBeNull()
    const user = userEvent.setup()
    await user.type(screen.getByLabelText('Branch or tag'), 'stable')
    await user.click(screen.getByRole('button', { name: 'Save source' }))
    await waitFor(() => expect(source.ref).toBe('stable'))
  })
  it('browses old versions and stages only the selected update before installation', async () => {
    const user = userEvent.setup()
    const release = {
      ...candidate,
      model: 'pico-s',
      kind: 'standalone',
      archive_sha256: uploaded.id,
      firmware_version: '1.3.0',
      hub_ota_protocol: 1,
    }
    const older = {
      ...release,
      firmware_version: '1.2.0',
      hub_ota_protocol: 0,
      archive_sha256: '2'.repeat(64),
    }
    const path = '/devices/pico-s/firmware'
    fetchMock.mockImplementation(async (url, opts) => {
      if (String(url).endsWith(path))
        return json({
          model: 'pico-s',
          device_uid: 'test-uid',
          current_version: '1.3.0',
          install_supported: true,
          packages: [],
        })
      if (String(url).endsWith('/firmware/updates'))
        return json({ releases: [older, release], notices: [] })
      if (String(url).endsWith('/stage')) return json(uploaded)
      if (String(url).endsWith('/install')) return json({ state: 'running' }, 202)
      return response(url, opts)
    })
    show(createElement(MachineFirmwareDialog, { deviceID: 'pico-s', onClose: close }))
    const versions = await screen.findByLabelText('Firmware version')
    await user.selectOptions(versions, older.archive_sha256)
    expect(screen.queryByRole('button', { name: 'Install' })).toBeNull()
    await user.click(screen.getByRole('button', { name: 'Check for Updates' }))
    expect(calls('/firmware/updates', 'POST')).toHaveLength(1)
    const stage = '/firmware/releases/' + uploaded.id + '/stage'
    expect(calls(stage, 'POST')).toHaveLength(0)
    await user.selectOptions(versions, release.archive_sha256)
    await user.click(screen.getByRole('button', { name: 'Install' }))
    await waitFor(() => expect(calls(path + '/install', 'POST')).toHaveLength(1))
    expect(calls(stage, 'POST')).toHaveLength(1)
    expect(JSON.parse(String(calls(path + '/install', 'POST')[0][1]?.body))).toEqual({
      package_id: uploaded.id,
      device_uid: 'test-uid',
      confirm: true,
    })
  })
  it('requires an independent controller selection once and never installs firmware', async () => {
    const user = userEvent.setup()
    let verified = false
    fetchMock.mockImplementation(async (url, options) => {
      if (String(url).endsWith('/devices/c/firmware/hardware')) {
        verified = true
        return json({
          device_uid: 'pinned-c',
          display_variant: 'ssd1309',
          verified_at: '2026-11-20T00:00:00Z',
        })
      }
      if (String(url).endsWith('/devices/c/firmware'))
        return json({
          device_id: 'c',
          device_uid: 'pinned-c',
          model: 'pico-c',
          current_version: '1.2.0',
          display_variant: 'ssd1309',
          verified_display_variant: verified ? 'ssd1309' : '',
          hardware_verification_required: !verified,
          install_supported: false,
          reason: 'Network installation is not available yet.',
          packages: [],
        })
      return response(url, options)
    })
    show(createElement(MachineFirmwareDialog, { deviceID: 'c', onClose: close }))
    const select = await screen.findByLabelText('Verified display controller')
    expect((select as HTMLSelectElement).value).toBe('')
    const save = screen.getByRole('button', { name: 'Save verified controller' })
    expect((save as HTMLButtonElement).disabled).toBe(true)
    await user.selectOptions(select, 'ssd1309')
    expect((save as HTMLButtonElement).disabled).toBe(true)
    await user.click(
      screen.getByRole('checkbox', {
        name: /I checked this machine's display controller independently/,
      })
    )
    await user.click(save)
    await waitFor(() => expect(notify).toHaveBeenCalledWith('Verified display controller saved.'))
    expect(JSON.parse(String(calls('/devices/c/firmware/hardware', 'PUT')[0][1]?.body))).toEqual({
      device_uid: 'pinned-c',
      display_variant: 'ssd1309',
    })
    expect(
      fetchMock.mock.calls.filter(([url]) => /install|commands/.test(String(url)))
    ).toHaveLength(0)
    expect(screen.queryByRole('button', { name: /install firmware/i })).toBeNull()
  })
})
