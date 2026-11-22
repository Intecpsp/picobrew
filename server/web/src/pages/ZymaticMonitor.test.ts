// @vitest-environment jsdom
import { createElement } from 'react'
import { act, cleanup, render, screen } from '@testing-library/react'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { HubContext, type HubState } from '../context'
import type { Device, DeviceStatus } from '../types'
import Fleet from './Fleet'
import Machine from './Machine'

const fetchMock = vi.fn<typeof fetch>()
const frame = { width: 128, height: 64, format: 'page-lsb-hex', data: 'a5'.repeat(1024) }
const status = {
  device_uid: 'private-enrollment-uid',
  state: 'monitoring',
  uptime_ms: 170000,
  ready: false,
  busy: false,
  stopped: false,
  wort_f: 98.5,
  thermoblock_f: 118.25,
} as DeviceStatus
const device = {
  id: 'paired-unit',
  model: 'zymatic',
  name: 'Countertop Zymatic',
  adapter: 'pico-standalone',
  enabled: true,
  online: true,
  status,
  capabilities: {
    firmware_version: '1.2.0',
    api_version: 1,
    commands: [],
    capabilities: { display: true, front_panel: false, temperature: true },
  },
} as unknown as Device
const pause = 'The display mirror will resume when the program ends.'
function response(data: unknown, status = 200) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { 'Content-Type': 'application/json' },
  })
}
function page(d: Device, fleet = false) {
  const hub = {
    devices: [d],
    brews: [],
    profiles: [],
    recipes: [],
    session: { demo: false },
    unit: 'F',
    connected: true,
    loading: false,
    notify: vi.fn(),
    navigate: vi.fn(),
  } as unknown as HubState
  return createElement(
    HubContext.Provider,
    { value: hub },
    fleet ? createElement(Fleet, {}) : createElement(Machine, { id: d.id })
  )
}
function displayCalls() {
  return fetchMock.mock.calls.filter(([url]) => String(url).endsWith('/paired-unit/display'))
}
beforeEach(() => {
  vi.stubGlobal('fetch', fetchMock)
  vi.useFakeTimers()
  vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue({
    createImageData: (w: number, h: number) => ({ data: new Uint8ClampedArray(w * h * 4) }),
    putImageData: vi.fn(),
  } as unknown as CanvasRenderingContext2D)
})
afterEach(() => {
  cleanup()
  vi.useRealTimers()
  vi.unstubAllGlobals()
  vi.restoreAllMocks()
  fetchMock.mockReset()
})

it('renders the real read-only panel, pauses after a busy response, and resumes it when the program ends', async () => {
  let busyResponse = false
  fetchMock.mockImplementation(async (url) => {
    if (!String(url).endsWith('/display')) return response({})
    return busyResponse
      ? response({ error: { code: 'device_display_busy', message: 'Device busy' } }, 503)
      : response(frame)
  })
  const view = render(page(device))
  await act(async () => {})
  expect(screen.getByText('LIVE OLED · 128 × 64')).toBeTruthy()
  expect(screen.getByText('Monitoring')).toBeTruthy()
  expect(screen.getByText('98.5°F')).toBeTruthy()
  expect(screen.queryByRole('button', { name: 'Press dial' })).toBeNull()
  expect(screen.queryByRole('button', { name: 'Import retained history' })).toBeNull()
  expect(screen.queryByText('Run a program')).toBeNull()
  busyResponse = true
  await act(async () => {
    await vi.advanceTimersByTimeAsync(5000)
  })
  expect(screen.getByText(pause)).toBeTruthy()
  expect(screen.queryByText('LIVE OLED · 128 × 64')).toBeNull()
  const before = displayCalls().length
  view.rerender(page({ ...device, status: { ...status, busy: true, state: 'program_running' } }))
  await act(async () => {
    await vi.advanceTimersByTimeAsync(20000)
  })
  expect(displayCalls()).toHaveLength(before)
  expect(screen.getByText('Program running')).toBeTruthy()
  busyResponse = false
  view.rerender(page(device))
  await act(async () => {})
  expect(displayCalls()).toHaveLength(before + 1)
  expect(screen.getByText('LIVE OLED · 128 × 64')).toBeTruthy()
})

it('waits for a full frame before polling again and abandons the old request when disconnected', async () => {
  let finish: ((value: Response) => void) | undefined
  fetchMock.mockImplementation((url) => {
    if (String(url).endsWith('/display'))
      return new Promise((resolve) => {
        finish = resolve
      })
    return Promise.resolve(response({}))
  })
  const view = render(page(device))
  await act(async () => {
    await vi.advanceTimersByTimeAsync(16000)
  })
  expect(displayCalls()).toHaveLength(1)
  view.rerender(page({ ...device, online: false }))
  expect(displayCalls()[0][1]?.signal?.aborted).toBe(true)
  await act(async () => {
    finish?.(response(frame))
  })
  expect(screen.queryByText('LIVE OLED · 128 × 64')).toBeNull()
  await act(async () => {
    await vi.advanceTimersByTimeAsync(16000)
  })
  expect(displayCalls()).toHaveLength(1)
})

it('lists a monitored machine without suggesting remote program controls', async () => {
  fetchMock.mockImplementation(async () => response({}))
  render(page(device, true))
  await act(async () => {})
  expect(screen.getByRole('link', { name: 'View Countertop Zymatic' }).textContent).toContain(
    'View machine'
  )
  expect(screen.getByText('Programs start at the brewer')).toBeTruthy()
  expect(screen.queryByText('Waiting for machine status')).toBeNull()
})
