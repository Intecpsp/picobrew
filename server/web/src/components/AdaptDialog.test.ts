// @vitest-environment jsdom
import { createElement } from 'react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { act, cleanup, render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { HubContext, type HubState } from '../context'
import { setCSRF } from '../lib/api'
import type { Adapted, Device, Profile, Recipe } from '../types'
import { AdaptDialog } from './AdaptDialog'

const source: Recipe = {
  id: 'imported-pale',
  name: 'Imported pale',
  kind: 'recipe',
  style: '',
  description: '',
  batch_liters: 14.0060236008,
  efficiency: 75,
  ingredients: [
    { name: 'Pale malt', kind: 'fermentable', amount: 2, unit: 'kg', use: 'mash', minutes: 0 },
  ],
  steps: [
    {
      name: 'Mash',
      kind: 'mash',
      target_c: 65.5555555,
      minutes: 60,
      drain_minutes: 2,
      location: 0,
    },
  ],
  tags: [],
  notes: '',
  revision: 4,
  created_at: '',
  updated_at: '',
}
const pro: Profile = {
  id: 'pico-pro',
  name: 'Pico Pro',
  batch_liters: 5,
  efficiency: 50,
  max_steps: 15,
  max_target_c: 100,
  max_grain_kg: 0,
  hop_slots: 0,
  control_supported: true,
  notes: '',
}
const s: Profile = {
  ...pro,
  id: 'pico-s',
  name: 'Pico S',
  batch_liters: 4.8,
  user_configured: true,
}
const z: Profile = {
  ...pro,
  id: 'z',
  name: 'Pico Z',
  batch_liters: 9.46352946,
  control_supported: false,
}
const devices = ['pico-pro', 'pico-s'].map(
  (model) =>
    ({
      id: model,
      model,
      name: model === 'pico-pro' ? 'Kitchen Pro' : 'Bench S',
      online: true,
      status: { ready: true, busy: false, stopped: false },
    }) as Device
)
const fetchMock = vi.fn<typeof fetch>()
const onClose = vi.fn()
const dialogMethods = ['showModal', 'close'] as const
const originalDialogMethods = dialogMethods.map((name) =>
  Object.getOwnPropertyDescriptor(HTMLDialogElement.prototype, name)
)

function json(value: unknown, status = 200) {
  return new Response(JSON.stringify(value), {
    status,
    headers: { 'Content-Type': 'application/json' },
  })
}

function plan(batch = 5, ready = false, profile = pro): Adapted {
  return {
    recipe: {
      ...source,
      batch_liters: batch,
      efficiency: profile.efficiency,
      ingredients: source.ingredients.map((item) => ({
        ...item,
        amount:
          (((item.amount * batch) / source.batch_liters) * source.efficiency) / profile.efficiency,
      })),
    },
    profile,
    machine_recipe: ready
      ? {
          id: source.id,
          name: source.name,
          steps: [{ name: 'Mash', target_f: 150, minutes: 60, drain_minutes: 2, location: 1 }],
        }
      : null,
    deployment_blockers: ready
      ? []
      : ['Step 1 requires an explicitly verified chamber location 1–6; no routing is inferred.'],
    warnings: [],
    can_deploy: ready,
    source_revision: source.revision,
    profile_fingerprint: `profile-${batch}`,
  }
}

function state(overrides: Partial<HubState> = {}): HubState {
  return {
    profiles: [pro, s, z],
    devices: [],
    notify: vi.fn(),
    navigate: vi.fn(),
    refresh: vi.fn(async () => {}),
    setDraft: vi.fn(),
    ...overrides,
  } as unknown as HubState
}

function element(hub: HubState, recipe = source) {
  return createElement(
    HubContext.Provider,
    { value: hub },
    createElement(AdaptDialog, { recipe, onClose })
  )
}

function batchInput() {
  return screen.getByRole('spinbutton', { name: 'Target batch size (L)' }) as HTMLInputElement
}

function requests(suffix: string) {
  return fetchMock.mock.calls.filter(([path]) => String(path).endsWith(suffix))
}

beforeEach(() => {
  fetchMock.mockReset()
  onClose.mockReset()
  vi.stubGlobal('fetch', fetchMock)
  setCSRF('test-csrf')
  for (const name of dialogMethods)
    Object.defineProperty(HTMLDialogElement.prototype, name, {
      configurable: true,
      value(this: HTMLDialogElement) {
        this.open = name === 'showModal'
      },
    })
})
afterEach(() => {
  cleanup()
  vi.restoreAllMocks()
  vi.unstubAllGlobals()
  dialogMethods.forEach((name, index) => {
    const original = originalDialogMethods[index]
    if (original) Object.defineProperty(HTMLDialogElement.prototype, name, original)
    else Reflect.deleteProperty(HTMLDialogElement.prototype, name)
  })
})

describe('recipe adaptation planning and deployment review', () => {
  it('does not assume a Pro profile belongs to an unidentified physical machine', () => {
    const unknown = { ...devices[0], id: 'unidentified', model: '', name: 'New machine' }
    render(element(state({ devices: [unknown] })))
    expect((screen.getByLabelText('Target machine') as HTMLSelectElement).value).toBe('')
    expect(
      (
        screen.getByRole('option', {
          name: 'New machine (model not identified)',
        }) as HTMLOptionElement
      ).disabled
    ).toBe(true)
  })
  it('defaults a 14 L imported recipe to the 5 L Pro profile and retains an unmapped planning schedule', async () => {
    fetchMock.mockResolvedValue(json(plan()))
    const user = userEvent.setup()
    const hub = state()
    render(element(hub))
    expect(batchInput().value).toBe('5')
    expect((screen.getByLabelText('Target machine') as HTMLSelectElement).value).toBe('')
    await user.click(screen.getByRole('button', { name: 'Preview adaptation' }))
    await screen.findByRole('heading', { name: 'Recipe schedule' })
    expect(JSON.parse(String(requests('/adapt')[0][1]?.body))).toEqual({
      profile_id: 'pico-pro',
      batch_liters: 5,
    })
    expect(screen.getByRole('heading', { name: 'Scaled ingredients' })).toBeTruthy()
    expect(screen.getByRole('region', { name: 'Deployment blockers' }).textContent).toContain(
      'chamber location'
    )
    expect(screen.getByText('Unmapped')).toBeTruthy()
    expect(
      (screen.getByRole('button', { name: 'Send to machine' }) as HTMLButtonElement).disabled
    ).toBe(true)
    await user.click(
      screen.getByRole('button', { name: 'Save this adaptation as a separate recipe' })
    )
    expect(hub.setDraft).toHaveBeenCalledWith(
      expect.objectContaining({ id: '', revision: 0, batch_liters: 5, steps: source.steps })
    )
    expect(hub.navigate).toHaveBeenCalledWith('/recipes/new')
    expect(requests('/deploy')).toHaveLength(0)
  })

  it('uses each profile default, retains explicit valid sizes and replaces sizes that no longer fit', async () => {
    const user = userEvent.setup()
    render(element(state({ devices })))
    await user.selectOptions(screen.getByLabelText('Equipment profile'), 'z')
    expect(batchInput().value).toBe('9.463')
    expect(Number(batchInput().value)).toBeLessThanOrEqual(z.batch_liters)
    await user.clear(batchInput())
    await user.type(batchInput(), '8')
    await user.selectOptions(screen.getByLabelText('Target machine'), 'pico-pro')
    expect(batchInput().value).toBe('5')
    expect((screen.getByLabelText('Equipment profile') as HTMLSelectElement).value).toBe('pico-pro')
    await user.clear(batchInput())
    await user.type(batchInput(), '4.25')
    await user.selectOptions(screen.getByLabelText('Target machine'), 'pico-s')
    expect(batchInput().value).toBe('4.25')
    await user.selectOptions(screen.getByLabelText('Equipment profile'), 'z')
    expect(batchInput().value).toBe('4.25')
    expect((screen.getByLabelText('Target machine') as HTMLSelectElement).value).toBe('')
  })

  it('invalidates reviewed plans on calibration changes even when the explicit batch still fits', async () => {
    fetchMock.mockResolvedValue(json(plan(4.25, true)))
    const user = userEvent.setup()
    const hub = state({ devices })
    const view = render(element(hub))
    await user.clear(batchInput())
    await user.type(batchInput(), '4.25')
    await user.click(screen.getByRole('button', { name: 'Preview adaptation' }))
    await screen.findByText('Machine recipe ready')
    expect(
      (screen.getByRole('button', { name: 'Send to machine' }) as HTMLButtonElement).disabled
    ).toBe(false)
    view.rerender(
      element({ ...hub, profiles: [{ ...pro, efficiency: 60, batch_liters: 4.8 }, s, z] })
    )
    await screen.findByRole('button', { name: 'Preview adaptation' })
    expect(batchInput().value).toBe('4.25')
    expect(screen.queryByRole('button', { name: 'Send to machine' })).toBeNull()
    view.rerender(element({ ...hub, profiles: [{ ...pro, batch_liters: 4 }, s, z] }))
    await waitFor(() => expect(batchInput().value).toBe('4'))
    expect(requests('/deploy')).toHaveLength(0)
  })

  it('ignores a late preview and sends only the currently reviewed batch and calibration fingerprint', async () => {
    let resolveOld!: (value: Response) => void
    fetchMock.mockImplementation(async (path) => {
      if (String(path).endsWith('/deploy')) return json({ deployed: true, started: false })
      if (requests('/adapt').length === 1)
        return new Promise<Response>((resolve) => {
          resolveOld = resolve
        })
      return json(plan(4, true))
    })
    const user = userEvent.setup()
    render(element(state({ devices })))
    await user.click(screen.getByRole('button', { name: 'Preview adaptation' }))
    const oldSignal = requests('/adapt')[0][1]?.signal
    await user.clear(batchInput())
    await user.type(batchInput(), '4')
    expect(oldSignal?.aborted).toBe(true)
    await user.click(screen.getByRole('button', { name: 'Preview adaptation' }))
    await screen.findByText('Machine recipe ready')
    await act(async () => {
      resolveOld(json(plan(5, true)))
    })
    await user.click(screen.getByRole('button', { name: 'Send to machine' }))
    await waitFor(() => expect(requests('/deploy')).toHaveLength(1))
    expect(JSON.parse(String(requests('/deploy')[0][1]?.body))).toEqual({
      recipe_id: source.id,
      profile_id: 'pico-pro',
      batch_liters: 4,
      source_revision: source.revision,
      profile_fingerprint: 'profile-4',
    })
  })

  it('clears errors and prevents an old device response from enabling deployment after a switch', async () => {
    let resolveOld!: (value: Response) => void
    fetchMock.mockImplementation(
      async () =>
        new Promise<Response>((resolve) => {
          resolveOld = resolve
        })
    )
    const user = userEvent.setup()
    render(element(state({ devices })))
    await user.click(screen.getByRole('button', { name: 'Preview adaptation' }))
    await user.selectOptions(screen.getByLabelText('Target machine'), 'pico-s')
    await act(async () => {
      resolveOld(json(plan(5, true)))
    })
    expect(batchInput().value).toBe('4.8')
    expect(screen.queryByRole('button', { name: 'Send to machine' })).toBeNull()
    fetchMock.mockResolvedValue(
      json({ error: { code: 'adaptation', message: 'Old capacity error' } }, 422)
    )
    await user.click(screen.getByRole('button', { name: 'Preview adaptation' }))
    await screen.findByRole('alert')
    await user.selectOptions(screen.getByLabelText('Equipment profile'), 'pico-pro')
    expect(screen.queryByRole('alert')).toBeNull()
    expect(requests('/deploy')).toHaveLength(0)
  })

  it('requires a positive batch within capacity instead of treating an empty or zero input as a default', async () => {
    const user = userEvent.setup()
    render(element(state()))
    for (const value of ['', '0', '6']) {
      await user.clear(batchInput())
      if (value) await user.type(batchInput(), value)
      expect(
        (screen.getByRole('button', { name: 'Preview adaptation' }) as HTMLButtonElement).disabled
      ).toBe(true)
    }
    expect(requests('/adapt')).toHaveLength(0)
    await user.clear(batchInput())
    await user.type(batchInput(), '4.2')
    expect(screen.queryByRole('alert')).toBeNull()
    expect(
      (screen.getByRole('button', { name: 'Preview adaptation' }) as HTMLButtonElement).disabled
    ).toBe(false)
  })

  it('keeps native programs at batch zero with their exact source schedule and no invented ingredients', async () => {
    const program = {
      id: 'pale-pak',
      name: 'Pale PicoPak',
      steps: [{ name: 'Heat mash', target_f: 152, minutes: 0, drain_minutes: 3, location: 1 }],
    }
    const native: Recipe = {
      ...source,
      name: program.name,
      kind: 'native-program',
      source_model: 'pico',
      batch_liters: 0,
      efficiency: 0,
      ingredients: [],
      steps: [],
      native_program: program,
    }
    const result: Adapted = { ...plan(0, true), recipe: native, machine_recipe: program }
    fetchMock.mockImplementation(async (path) =>
      String(path).endsWith('/adapt') ? json(result) : json({ deployed: true })
    )
    const user = userEvent.setup()
    render(element(state({ devices }), native))
    expect(screen.queryByRole('spinbutton', { name: 'Target batch size (L)' })).toBeNull()
    await user.click(screen.getByRole('button', { name: 'Preview adaptation' }))
    await screen.findByText('Exact native schedule')
    expect(JSON.parse(String(requests('/adapt')[0][1]?.body)).batch_liters).toBe(0)
    expect(screen.getByText('152°F')).toBeTruthy()
    expect(screen.getByText('0 min')).toBeTruthy()
    expect(screen.queryByRole('heading', { name: 'Scaled ingredients' })).toBeNull()
    await user.click(screen.getByRole('button', { name: 'Send to machine' }))
    await waitFor(() => expect(requests('/deploy')).toHaveLength(1))
    expect(JSON.parse(String(requests('/deploy')[0][1]?.body)).batch_liters).toBe(0)
  })
})
