// @vitest-environment jsdom
import { createElement } from 'react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { cleanup, render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { HubContext, type HubState } from '../context'
import { setCSRF } from '../lib/api'
import type { Device, Telemetry } from '../types'
import { HubConnectionSettings } from './HubConnectionSettings'

const fetchMock = vi.fn<typeof fetch>()
const notify = vi.fn()
let connection: Telemetry
const device = {
  id: 'pico-s',
  adapter: 'pico-standalone',
  enabled: true,
  capabilities: { telemetry_forwarding: true },
  telemetry: { mode: 'automatic', state: 'configured' },
} as Device

function json(value: unknown, status = 200) {
  return new Response(JSON.stringify(value), {
    status,
    headers: { 'Content-Type': 'application/json' },
  })
}

function show(overrides: Partial<Device> = {}) {
  const brewer = { ...device, ...overrides }
  const hub = { devices: [brewer], refresh: vi.fn(async () => {}), notify } as unknown as HubState
  render(
    createElement(
      HubContext.Provider,
      { value: hub },
      createElement(HubConnectionSettings, { device: brewer })
    )
  )
  return hub
}

function writes() {
  return fetchMock.mock.calls.filter(([, options]) => options?.method === 'PUT')
}

beforeEach(() => {
  connection = { mode: 'automatic', enabled: true, host: 'hub.local', port: 8080 }
  notify.mockReset()
  fetchMock.mockReset()
  fetchMock.mockImplementation(async (_url, options) => {
    if (options?.method === 'PUT') {
      const next = JSON.parse(String(options.body))
      connection =
        next.mode === 'automatic'
          ? { mode: 'automatic', enabled: true, host: 'hub.local', port: 8080 }
          : next
    }
    return json(connection)
  })
  vi.stubGlobal('fetch', fetchMock)
  setCSRF('test')
})
afterEach(() => {
  cleanup()
  vi.unstubAllGlobals()
})

describe('advanced Hub connection overrides', () => {
  it('does not submit out-of-range ports even when manual sending is disabled', async () => {
    connection = { mode: 'manual', enabled: false, host: '', port: 80 }
    const user = userEvent.setup()
    show({ telemetry: { mode: 'manual', enabled: false, state: 'disabled' } })
    await user.click(screen.getByText('Advanced Hub connection'))
    const port = await screen.findByLabelText('Receiver port')
    for (const value of ['0', '65536']) {
      await user.clear(port)
      await user.type(port, value)
      await user.click(screen.getByRole('button', { name: 'Save manual connection' }))
      expect(writes()).toHaveLength(0)
    }
    await user.clear(port)
    await user.type(port, '65535')
    await user.click(screen.getByRole('button', { name: 'Save manual connection' }))
    await waitFor(() => expect(notify).toHaveBeenCalledWith('Manual connection saved.'))
    expect(JSON.parse(String(writes()[0][1]?.body))).toEqual({
      mode: 'manual',
      enabled: false,
      host: '',
      port: 65535,
    })
  })
  it('keeps normal setup automatic and saves a custom receiver only after an explicit advanced edit', async () => {
    const user = userEvent.setup()
    show()
    expect(screen.getByText('Live readings configured')).toBeTruthy()
    expect(fetchMock).not.toHaveBeenCalled()
    expect(screen.queryByLabelText('Receiver host')).toBeNull()
    await user.click(screen.getByText('Advanced Hub connection'))
    const management = await screen.findByLabelText('Connection management')
    expect((management as HTMLSelectElement).value).toBe('automatic')
    expect(writes()).toHaveLength(0)
    await user.selectOptions(management, 'manual')
    await user.clear(screen.getByLabelText('Receiver host'))
    await user.type(screen.getByLabelText('Receiver host'), 'brew-relay.local')
    await user.clear(screen.getByLabelText('Receiver port'))
    await user.type(screen.getByLabelText('Receiver port'), '49152')
    await user.click(screen.getByRole('button', { name: 'Save manual connection' }))
    await waitFor(() => expect(notify).toHaveBeenCalledWith('Manual connection saved.'))
    expect(writes()).toHaveLength(1)
    expect(JSON.parse(String(writes()[0][1]?.body))).toEqual({
      mode: 'manual',
      enabled: true,
      host: 'brew-relay.local',
      port: 49152,
    })
    await user.click(screen.getByText('Advanced Hub connection'))
    await user.click(screen.getByText('Advanced Hub connection'))
    await waitFor(() =>
      expect((screen.getByLabelText('Connection management') as HTMLSelectElement).value).toBe(
        'manual'
      )
    )
    expect((screen.getByLabelText('Receiver host') as HTMLInputElement).value).toBe(
      'brew-relay.local'
    )
    expect(writes()).toHaveLength(1)
  })

  it('preserves a disabled manual receiver and explicitly returns to automatic without resending its destination', async () => {
    connection = { mode: 'manual', enabled: false, host: 'custom.local', port: 9010 }
    const user = userEvent.setup()
    show({ telemetry: { mode: 'manual', enabled: false, state: 'disabled' } })
    await user.click(screen.getByText('Advanced Hub connection'))
    const management = await screen.findByLabelText('Connection management')
    expect((management as HTMLSelectElement).value).toBe('manual')
    expect((screen.getByLabelText('Send live readings') as HTMLInputElement).checked).toBe(false)
    expect(writes()).toHaveLength(0)
    await user.selectOptions(management, 'automatic')
    expect(screen.queryByLabelText('Receiver host')).toBeNull()
    await user.click(screen.getByRole('button', { name: 'Return to automatic' }))
    await waitFor(() => expect(notify).toHaveBeenCalledWith('Automatic Hub connection restored.'))
    expect(JSON.parse(String(writes()[0][1]?.body))).toEqual({ mode: 'automatic' })
  })

  it('keeps the entered override visible when the machine rejects the change', async () => {
    fetchMock.mockImplementation(async (_url, options) =>
      options?.method === 'PUT'
        ? json({ error: { code: 'busy', message: 'Wait until the machine is idle.' } }, 409)
        : json(connection)
    )
    const user = userEvent.setup()
    show()
    await user.click(screen.getByText('Advanced Hub connection'))
    await user.selectOptions(await screen.findByLabelText('Connection management'), 'manual')
    await user.clear(screen.getByLabelText('Receiver host'))
    await user.type(screen.getByLabelText('Receiver host'), 'custom.local')
    await user.click(screen.getByRole('button', { name: 'Save manual connection' }))
    await screen.findByText('Wait until the machine is idle.')
    expect((screen.getByLabelText('Receiver host') as HTMLInputElement).value).toBe('custom.local')
    expect(notify).not.toHaveBeenCalled()
  })
})
