// @vitest-environment jsdom
import { createElement } from 'react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { cleanup, render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { HubContext, type HubState } from '../context'
import { setCSRF } from '../lib/api'
import type { Device } from '../types'
import { DeviceDialog, HubConnectionStatus } from './DeviceDialogs'

const fetchMock = vi.fn<typeof fetch>()
const onClose = vi.fn()
const originalShowModal = Object.getOwnPropertyDescriptor(HTMLDialogElement.prototype, 'showModal')

function machine(overrides: Partial<Device> = {}): Device {
  return {
    id: 'physical-s',
    name: 'Kitchen brewer',
    model: 'pico-s',
    model_source: 'firmware',
    adapter: 'pico-standalone',
    base_url: 'http://192.168.1.42',
    location: 'Kitchen',
    enabled: true,
    key_configured: true,
    created_at: '',
    online: true,
    last_seen: null,
    status: null,
    capabilities: null,
    telemetry: { state: 'configured' },
    ...overrides,
  }
}

function hub(device?: Device): HubState {
  return {
    session: {
      authenticated: true,
      setup_required: false,
      csrf_token: 'test',
      version: 'test',
      demo: false,
    },
    devices: device ? [device] : [],
    profiles: [],
    notify: vi.fn(),
    refresh: vi.fn(async () => {}),
  } as unknown as HubState
}

function show(device?: Device) {
  const state = hub(device)
  render(
    createElement(
      HubContext.Provider,
      { value: state },
      createElement(DeviceDialog, { device, onClose })
    )
  )
  return state
}

function json(value: unknown, status = 200) {
  return new Response(JSON.stringify(value), {
    status,
    headers: { 'Content-Type': 'application/json' },
  })
}

beforeEach(() => {
  fetchMock.mockReset()
  onClose.mockReset()
  vi.stubGlobal('fetch', fetchMock)
  setCSRF('test')
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

describe('automatic physical-machine connections', () => {
  it('offers model-specific offline USB setup and restoration before registration', async () => {
    const user = userEvent.setup()
    show()
    await user.type(screen.getByLabelText('Machine name'), 'My brewer')
    await user.click(screen.getByRole('button', { name: 'USB setup and recovery' }))
    const select = screen.getByLabelText('Machine model', { selector: 'select' })
    expect((select as HTMLSelectElement).value).toBe('')
    expect(screen.queryByText(/picobrew-hub usb-install/)).toBeNull()
    for (const model of ['pico-s', 'pico-pro']) {
      await user.selectOptions(select, model)
      const command = screen.getByText(new RegExp('picobrew-hub usb-install --model ' + model))
      expect(command.textContent).toContain('--data ./data')
      expect(command.textContent).not.toContain('--backup')
    }
    await user.selectOptions(select, 'pico-c')
    expect(screen.queryByText(/picobrew-hub usb-install/)).toBeNull()
    await user.selectOptions(screen.getByLabelText('Physical Pico C display controller'), 'ssd1309')
    expect(screen.getByText(/picobrew-hub usb-install/).textContent).toContain('--display ssd1309')
    for (const model of ['z-series', 'picostill', 'picoferm']) {
      await user.selectOptions(select, model)
      for (const action of ['install', 'backup']) {
        await user.selectOptions(screen.getByLabelText('USB operation'), action)
        const command = screen.getByText(new RegExp('picobrew-hub usb-' + action))
        expect(command.textContent).toContain('--model ' + model + ' --port SERIAL_PORT')
        expect(command.textContent).not.toContain('--key-file')
      }
    }
    await user.selectOptions(select, 'zymatic')
    expect(screen.getByText(/picobrew-hub usb-install/).textContent).toContain('--key-file')
    const actions = screen.getByLabelText('USB operation')
    await user.selectOptions(actions, 'backup')
    expect(screen.getByText(/picobrew-hub usb-backup/).textContent).toContain(
      '--model zymatic --port SERIAL_PORT'
    )
    await user.selectOptions(actions, 'restore')
    await user.click(screen.getByRole('button', { name: 'Review stock restoration' }))
    await user.click(screen.getByRole('checkbox'))
    await user.click(screen.getByRole('button', { name: 'Review USB requirements' }))
    await user.click(screen.getByRole('checkbox'))
    await user.click(screen.getByRole('button', { name: 'Show USB restore instructions' }))
    expect(screen.getByText(/picobrew-hub usb-restore/).textContent).toContain('--model zymatic')
    expect(screen.getByText(/--oem-file/)).toBeTruthy()
    await user.click(screen.getByRole('button', { name: 'Back to USB setup' }))
    await user.click(screen.getByRole('button', { name: 'Back to Add machine' }))
    expect((screen.getByLabelText('Machine name') as HTMLInputElement).value).toBe('My brewer')
    await user.selectOptions(screen.getByLabelText('Connection type'), 'simulator')
    expect(screen.queryByRole('button', { name: 'USB setup and recovery' })).toBeNull()
    expect(fetchMock).not.toHaveBeenCalled()
  })
  it('registers a physical brewer without making the user choose or assume a model', async () => {
    fetchMock.mockResolvedValue(json(machine({ model: '', model_source: 'pending' })))
    const user = userEvent.setup()
    const state = show()
    const model = screen.getByLabelText('Machine model') as HTMLInputElement
    expect(model.readOnly).toBe(true)
    expect(model.value).toBe('Detect automatically')
    expect(screen.queryByRole('combobox', { name: 'Machine model' })).toBeNull()
    await user.type(screen.getByLabelText('Machine name'), 'My Pico S')
    await user.type(screen.getByLabelText('Machine URL'), 'http://192.168.1.42')
    await user.type(screen.getByLabelText('Machine API key'), 'local-test-key')
    await user.click(screen.getByRole('button', { name: 'Add machine' }))
    await waitFor(() => expect(onClose).toHaveBeenCalledOnce())
    expect(fetchMock).toHaveBeenCalledOnce()
    expect(fetchMock.mock.calls[0][0]).toBe('/api/v1/devices')
    expect(JSON.parse(String(fetchMock.mock.calls[0][1]?.body))).toMatchObject({
      adapter: 'pico-standalone',
      model: '',
      name: 'My Pico S',
      base_url: 'http://192.168.1.42',
    })
    expect(state.notify).toHaveBeenCalledWith(expect.stringContaining('setting up live readings'))
  })

  it('keeps simulator selection and resets to automatic identification when returning to a physical brewer', async () => {
    const user = userEvent.setup()
    show()
    await user.selectOptions(screen.getByLabelText('Connection type'), 'simulator')
    await user.selectOptions(screen.getByLabelText('Machine model'), 'pico-s')
    expect((screen.getByLabelText('Machine model') as HTMLSelectElement).value).toBe('pico-s')
    await user.selectOptions(screen.getByLabelText('Connection type'), 'pico-standalone')
    expect((screen.getByLabelText('Machine model') as HTMLInputElement).value).toBe(
      'Detect automatically'
    )
    expect(screen.queryByRole('combobox', { name: 'Machine model' })).toBeNull()
  })

  it('keeps a reported Pico S read-only when editing the same machine record', async () => {
    fetchMock.mockResolvedValue(json(machine()))
    const user = userEvent.setup()
    show(machine())
    expect((screen.getByLabelText('Machine model') as HTMLInputElement).value).toBe('Pico S')
    expect(screen.queryByRole('combobox', { name: 'Machine model' })).toBeNull()
    expect(screen.getByText('Saved. Live readings are configured.')).toBeTruthy()
    await user.clear(screen.getByLabelText('Machine name'))
    await user.type(screen.getByLabelText('Machine name'), 'Renamed S')
    await user.click(screen.getByRole('button', { name: 'Save machine' }))
    await waitFor(() => expect(onClose).toHaveBeenCalledOnce())
    expect(fetchMock.mock.calls[0][0]).toBe('/api/v1/devices/physical-s')
    expect(JSON.parse(String(fetchMock.mock.calls[0][1]?.body))).toMatchObject({
      name: 'Renamed S',
      model: '',
    })
  })

  it('offers an empty explicit fallback only when older firmware requires a selection', async () => {
    fetchMock.mockResolvedValue(json(machine({ model_source: 'manual' })))
    const user = userEvent.setup()
    show(
      machine({
        model: '',
        model_source: 'pending',
        model_selection_required: true,
        model_error_code: 'not_reported',
        model_error: 'This firmware does not report its model.',
      })
    )
    const model = screen.getByRole('combobox', { name: 'Machine model' }) as HTMLSelectElement
    expect(model.value).toBe('')
    expect(model.required).toBe(true)
    await user.selectOptions(model, 'pico-s')
    await user.click(screen.getByRole('button', { name: 'Save machine' }))
    await waitFor(() => expect(onClose).toHaveBeenCalledOnce())
    expect(JSON.parse(String(fetchMock.mock.calls[0][1]?.body))).toMatchObject({ model: 'pico-s' })
  })

  it('identifies an existing manual record without creating another machine or losing its connection', async () => {
    fetchMock.mockResolvedValue(json(machine()))
    const user = userEvent.setup()
    const state = show(machine({ model: 'pico-pro', model_source: 'manual' }))
    expect((screen.getByLabelText('Machine model') as HTMLSelectElement).value).toBe('pico-pro')
    await user.click(screen.getByRole('button', { name: 'Identify machine' }))
    await waitFor(() =>
      expect((screen.getByLabelText('Machine model') as HTMLInputElement).value).toBe('Pico S')
    )
    expect(screen.queryByRole('combobox', { name: 'Machine model' })).toBeNull()
    expect(fetchMock).toHaveBeenCalledOnce()
    expect(fetchMock.mock.calls[0][0]).toBe('/api/v1/devices/physical-s/test')
    expect(state.refresh).toHaveBeenCalledOnce()
    expect(onClose).not.toHaveBeenCalled()
  })

  it('preserves known identity after an unreachable-machine error', async () => {
    fetchMock.mockRejectedValue(new Error('Machine is unreachable.'))
    const user = userEvent.setup()
    show(machine())
    await user.click(screen.getByRole('button', { name: 'Identify machine' }))
    await screen.findByText('Machine is unreachable.')
    expect((screen.getByLabelText('Machine model') as HTMLInputElement).value).toBe('Pico S')
    expect(screen.queryByRole('combobox', { name: 'Machine model' })).toBeNull()
    expect(onClose).not.toHaveBeenCalled()
  })

  it('does not offer a manual override for invalid reported identity', () => {
    show(
      machine({
        model: '',
        model_source: 'manual',
        model_error_code: 'invalid_model',
        model_error: 'The reported model is not valid.',
      })
    )
    expect(screen.getByText('The reported model is not valid.')).toBeTruthy()
    expect((screen.getByLabelText('Machine model') as HTMLInputElement).value).toBe(
      'Model not identified'
    )
    expect(screen.queryByRole('combobox', { name: 'Machine model' })).toBeNull()
  })

  it('shows automatic Hub connection status without receiver configuration controls', () => {
    render(createElement(HubConnectionStatus, { device: machine() }))
    expect(screen.getByRole('status').textContent).toBe('Saved. Live readings are configured.')
    expect(screen.queryByRole('textbox')).toBeNull()
    expect(screen.queryByRole('spinbutton')).toBeNull()
    expect(screen.queryByRole('checkbox')).toBeNull()
    expect(screen.queryByRole('button')).toBeNull()
  })
})
