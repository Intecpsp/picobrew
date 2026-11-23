// @vitest-environment jsdom
import { createElement as h } from 'react'
import { cleanup, fireEvent, render, screen, within } from '@testing-library/react'
import { afterEach, expect, it, vi } from 'vitest'
import type { Device } from '../types'
import { MachineEvents } from './MachineEvents'

const supported = {
  id: 'kitchen',
  adapter: 'pico-standalone',
  capabilities: { events_persistent: false },
  online: true,
  enabled: true,
} as Device

afterEach(() => {
  cleanup()
  vi.unstubAllGlobals()
})

it('reads on demand, displays newest first, and refreshes only when requested', async () => {
  const fetchMock = vi
    .fn()
    .mockResolvedValueOnce(
      new Response(
        JSON.stringify({
          events: [
            { sequence: 5, uptime_ms: 400, kind: 'startup', detail: 'Device ready' },
            { sequence: 6, uptime_ms: 72400, kind: 'command', detail: 'deep_clean' },
          ],
        })
      )
    )
    .mockResolvedValueOnce(new Response(JSON.stringify({ events: [] })))
  vi.stubGlobal('fetch', fetchMock)
  render(h(MachineEvents, { device: supported }))
  expect(fetchMock).not.toHaveBeenCalled()
  fireEvent.click(screen.getByRole('button', { name: 'View device events' }))
  const events = await screen.findAllByRole('listitem')
  expect(within(events[0]).getByText('command')).toBeTruthy()
  expect(events[0].textContent).toContain('1:12')
  expect(within(events[1]).getByText('Device ready')).toBeTruthy()
  expect(fetchMock).toHaveBeenCalledTimes(1)
  expect(fetchMock.mock.calls[0]).toMatchObject([
    '/api/v1/devices/kitchen/events',
    { method: 'GET', credentials: 'same-origin' },
  ])
  fireEvent.click(screen.getByRole('button', { name: 'Refresh events' }))
  await screen.findByText('No events recorded since startup.')
  expect(fetchMock).toHaveBeenCalledTimes(2)
})

it('disables offline reads and excludes firmware without the event capability', () => {
  const fetchMock = vi.fn()
  vi.stubGlobal('fetch', fetchMock)
  const view = render(h(MachineEvents, { device: { ...supported, online: false } }))
  expect((screen.getByRole('button') as HTMLButtonElement).disabled).toBe(true)
  view.rerender(h(MachineEvents, { device: { ...supported, capabilities: null } }))
  expect(screen.queryByRole('button')).toBeNull()
  expect(fetchMock).not.toHaveBeenCalled()
})
