// @vitest-environment jsdom
import { createElement } from 'react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { cleanup, render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { HubContext, type HubState } from '../context'
import { AIWorkbench } from '../pages/Assistant'
import { BrewAIMonitor } from './BrewAIMonitor'
import { setCSRF } from '../lib/api'
import Machine from '../pages/Machine'

vi.mock('./OLED', () => ({ default: () => null }))
vi.mock('./Charts', () => ({ TemperatureChart: () => null }))

const fetchMock = vi.fn<typeof fetch>()
const setDraft = vi.fn()
const navigate = vi.fn()
const reload = vi.fn()
const originalShowModal = Object.getOwnPropertyDescriptor(HTMLDialogElement.prototype, 'showModal')
const recipe = { id: 'porter', name: 'Library porter', revision: 3, ingredients: [], steps: [] }
const draft = {
  id: 'provider-id',
  name: 'New citrus ale',
  revision: 9,
  ingredients: [],
  steps: [],
  limitations: [],
}

function json(value: unknown, status = 200) {
  return new Response(JSON.stringify(value), {
    status,
    headers: { 'Content-Type': 'application/json' },
  })
}
function mount(element: ReturnType<typeof createElement>) {
  return render(
    createElement(
      HubContext.Provider,
      {
        value: {
          recipes: [recipe],
          devices: [],
          brews: [],
          setDraft,
          navigate,
        } as unknown as HubState,
      },
      element
    )
  )
}
function requestBodies() {
  return fetchMock.mock.calls
    .filter(([url, options]) => String(url) === '/api/v1/ai/assist' && options?.method === 'POST')
    .map(([, options]) => JSON.parse(String(options?.body)))
}

beforeEach(() => {
  Object.defineProperty(HTMLDialogElement.prototype, 'showModal', {
    configurable: true,
    value() {
      this.setAttribute('open', '')
    },
  })
  fetchMock.mockReset()
  setDraft.mockReset()
  navigate.mockReset()
  reload.mockReset()
  setCSRF('test')
  vi.stubGlobal('fetch', fetchMock)
  fetchMock.mockImplementation(async (url) => {
    if (String(url).endsWith('/ai/settings'))
      return json({ enabled: true, key_configured: true, provider: 'openai', model: 'test-model' })
    if (String(url).endsWith('/ai/assist'))
      return json({ text: 'A balanced draft.', recipe: draft, limitations: [] })
    return json([])
  })
})
afterEach(() => {
  cleanup()
  vi.unstubAllGlobals()
  if (originalShowModal)
    Object.defineProperty(HTMLDialogElement.prototype, 'showModal', originalShowModal)
  else Reflect.deleteProperty(HTMLDialogElement.prototype, 'showModal')
})

describe('contextual AI workflow', () => {
  it.each(
    (['openai', 'anthropic'] as const).flatMap((provider) =>
      (['recipe', 'live', 'analysis'] as const).map((mode) => ({ provider, mode }))
    )
  )(
    'uses a saved $provider subscription for $mode without an API key',
    async ({ provider, mode }) => {
      fetchMock.mockImplementation(async (url) => {
        if (String(url).endsWith('/ai/settings'))
          return json({
            enabled: true,
            auth_mode: 'subscription',
            key_configured: false,
            provider,
            model: 'subscription-model',
          })
        if (String(url).endsWith('/ai/assist'))
          return json({
            text: 'Subscription response.',
            recipe: mode === 'recipe' ? draft : undefined,
            limitations: [],
          })
        return json([])
      })
      const user = userEvent.setup()
      mount(createElement(AIWorkbench, { mode, deviceID: 'pro', brewID: 'brew-a' }))
      await screen.findByText(`${provider} · subscription-model`)
      expect(screen.queryByText('Not configured')).toBeNull()
      expect(screen.queryByRole('button', { name: 'Configure assistant' })).toBeNull()
      await user.type(
        screen.getByLabelText('What would you like to explore?'),
        'Use my subscription'
      )
      const ask = screen.getByRole('button', { name: 'Ask the assistant' }) as HTMLButtonElement
      expect(ask.disabled).toBe(false)
      await user.click(ask)
      await screen.findByText('Subscription response.')
      expect(requestBodies()).toEqual([
        {
          mode,
          prompt: 'Use my subscription',
          ...(mode === 'live' ? { device_id: 'pro' } : {}),
          ...(mode !== 'recipe' ? { brew_id: 'brew-a' } : {}),
        },
      ])
      if (mode === 'recipe') {
        await user.click(screen.getByRole('button', { name: 'Review in recipe editor' }))
        expect(setDraft).toHaveBeenCalledWith({ ...draft, id: '', revision: 0 })
        expect(navigate).toHaveBeenCalledWith('/recipes/new')
      }
    }
  )

  it.each([
    {
      name: 'disabled subscription',
      enabled: false,
      auth_mode: 'subscription',
      model: 'saved-model',
    },
    {
      name: 'subscription without a selected model',
      enabled: true,
      auth_mode: 'subscription',
      model: '',
    },
    {
      name: 'subscription with a blank model',
      enabled: true,
      auth_mode: 'subscription',
      model: '   ',
    },
    {
      name: 'API billing without a key',
      enabled: true,
      auth_mode: 'api_key',
      model: 'saved-model',
    },
    {
      name: 'legacy API billing without a key',
      enabled: true,
      auth_mode: undefined,
      model: 'saved-model',
    },
  ])('keeps $name unavailable', async ({ name: _name, ...config }) => {
    fetchMock.mockImplementation(async (url) =>
      String(url).endsWith('/ai/settings')
        ? json({ ...config, key_configured: false, provider: 'openai' })
        : json([])
    )
    const user = userEvent.setup()
    mount(createElement(AIWorkbench, { mode: 'recipe' }))
    await screen.findByText('Not configured')
    expect(await screen.findByText(/Connect a ChatGPT or Claude subscription/)).toBeTruthy()
    await user.type(screen.getByLabelText('What would you like to explore?'), 'Make a recipe')
    expect(
      (screen.getByRole('button', { name: 'Ask the assistant' }) as HTMLButtonElement).disabled
    ).toBe(true)
    expect(requestBodies()).toEqual([])
  })

  it('requests optional monitoring only with an explicitly confirmed brew start', async () => {
    let failAfterAcceptedStart = false
    fetchMock.mockImplementation(async (url) => {
      if (String(url).endsWith('/commands') && failAfterAcceptedStart)
        return json(
          {
            error: {
              code: 'ai_monitor_receipt_error',
              message:
                'The machine accepted the command, but AI monitoring was not saved. Do not repeat the start command.',
            },
          },
          500
        )
      if (String(url).endsWith('/ai-monitor'))
        return json({ enabled: false, state: 'off', configured: true })
      if (String(url).endsWith('/recipes')) return json([recipe])
      return json({})
    })
    const user = userEvent.setup()
    const value = {
      devices: [
        {
          id: 'pro',
          name: 'Bench Pro',
          model: 'pico-pro',
          online: true,
          enabled: true,
          status: { ready: true, busy: false },
          capabilities: { commands: ['brew', 'manual', 'rinse'], recipe_capacity: 32 },
        },
      ],
      recipes: [recipe],
      brews: [],
      loading: false,
      unit: 'F',
      refresh: vi.fn(async () => {}),
      notify: vi.fn(),
      navigate,
    } as unknown as HubState
    render(createElement(HubContext.Provider, { value }, createElement(Machine, { id: 'pro' })))
    await screen.findByRole('option', { name: 'Library porter' })
    await user.selectOptions(screen.getByLabelText('Recipe on this machine'), 'porter')
    await user.click(screen.getByRole('button', { name: 'Start recipe' }))
    let consent = screen.getByRole('checkbox', {
      name: 'Monitor this brew with AI',
    }) as HTMLInputElement
    expect(consent.checked).toBe(false)
    await user.click(consent)
    await user.click(screen.getByRole('button', { name: 'Cancel' }))
    await user.click(screen.getByRole('button', { name: 'Start recipe' }))
    consent = screen.getByRole('checkbox', {
      name: 'Monitor this brew with AI',
    }) as HTMLInputElement
    expect(consent.checked).toBe(false)
    await user.click(consent)
    expect(
      fetchMock.mock.calls.some(
        ([url, options]) => String(url).endsWith('/commands') && options?.method === 'POST'
      )
    ).toBe(false)
    await user.click(screen.getByRole('button', { name: 'Start program' }))
    await waitFor(() => {
      const start = fetchMock.mock.calls.find(
        ([url, options]) => String(url).endsWith('/commands') && options?.method === 'POST'
      )
      expect(JSON.parse(String(start?.[1]?.body))).toEqual({
        command: 'brew',
        recipe_id: 'porter',
        ai_monitor: true,
        confirm: true,
      })
    })
    await waitFor(() => expect(screen.queryByRole('dialog')).toBeNull())
    // Consent is a choice for the single start, not an option silently kept for later brews.
    await user.selectOptions(screen.getByLabelText('Other programs'), 'manual')
    await user.click(screen.getByRole('button', { name: 'Open program' }))
    expect(
      (screen.getByRole('checkbox', { name: 'Monitor this brew with AI' }) as HTMLInputElement)
        .checked
    ).toBe(false)
    await user.click(screen.getByRole('button', { name: 'Start program' }))
    await waitFor(() => {
      const starts = fetchMock.mock.calls.filter(
        ([url, options]) => String(url).endsWith('/commands') && options?.method === 'POST'
      )
      expect(JSON.parse(String(starts.at(-1)?.[1]?.body))).toEqual({
        command: 'manual',
        ai_monitor: false,
        confirm: true,
      })
    })
    await waitFor(() => expect(screen.queryByRole('dialog')).toBeNull())
    failAfterAcceptedStart = true
    await user.click(screen.getByRole('button', { name: 'Start recipe' }))
    await user.click(screen.getByRole('checkbox', { name: 'Monitor this brew with AI' }))
    await user.click(screen.getByRole('button', { name: 'Start program' }))
    await waitFor(() => expect(screen.queryByRole('dialog')).toBeNull())
    expect((await screen.findByRole('alert')).textContent).toContain(
      'Do not repeat the start command.'
    )
    expect(
      fetchMock.mock.calls.filter(
        ([url, options]) => String(url).endsWith('/commands') && options?.method === 'POST'
      )
    ).toHaveLength(3)
  })

  it('uses the selected library recipe and requires explicit draft review', async () => {
    const user = userEvent.setup()
    mount(createElement(AIWorkbench, { mode: 'recipe', recipeID: 'porter' }))
    await screen.findByText('openai · test-model')
    expect(screen.queryByRole('tablist', { name: 'Assistant task' })).toBeNull()
    expect(
      (screen.getByLabelText('Existing recipe to build on (optional)') as HTMLSelectElement).value
    ).toBe('porter')
    await user.type(
      screen.getByLabelText('What would you like to explore?'),
      'Make a citrus variation'
    )
    await user.click(screen.getByRole('button', { name: 'Ask the assistant' }))
    await screen.findByRole('heading', { name: 'New citrus ale' })
    expect(requestBodies()).toEqual([
      { mode: 'recipe', prompt: 'Make a citrus variation', recipe_id: 'porter' },
    ])
    expect(setDraft).not.toHaveBeenCalled()
    expect(
      fetchMock.mock.calls.some(
        ([url, options]) => String(url).endsWith('/recipes') && options?.method === 'POST'
      )
    ).toBe(false)
    await user.click(screen.getByRole('button', { name: 'Review in recipe editor' }))
    expect(setDraft).toHaveBeenCalledWith({ ...draft, id: '', revision: 0 })
    expect(navigate).toHaveBeenCalledWith('/recipes/new')
  })

  it('binds journal analysis and retained observations to the selected brew', async () => {
    fetchMock.mockImplementation(async (url) => {
      if (String(url).endsWith('/ai/settings'))
        return json({
          enabled: true,
          key_configured: true,
          provider: 'openai',
          model: 'test-model',
        })
      if (String(url).endsWith('/ai/assist'))
        return json({ text: 'Session analysis.', limitations: [] })
      return json([
        {
          id: 'current',
          mode: 'live',
          brew_id: 'brew-a',
          device_id: 'pro',
          status: 'complete',
          created_at: '2026-01-01T12:00:00Z',
          text: 'Selected session observation',
        },
        {
          id: 'other',
          mode: 'analysis',
          brew_id: 'brew-b',
          device_id: 'pro',
          status: 'complete',
          created_at: '2026-01-01T12:00:00Z',
          text: 'Different session observation',
        },
      ])
    })
    const user = userEvent.setup()
    mount(
      createElement(AIWorkbench, {
        mode: 'analysis',
        brewID: 'brew-a',
        recipeID: 'deleted-library-recipe',
        embedded: true,
      })
    )
    await screen.findByText('Selected session observation')
    expect(screen.queryByText('Different session observation')).toBeNull()
    expect(screen.queryByLabelText('Brew to analyze')).toBeNull()
    expect(
      fetchMock.mock.calls.some(([url]) => String(url) === '/api/v1/ai/advice?brew_id=brew-a')
    ).toBe(true)
    await user.type(
      screen.getByLabelText('What would you like to explore?'),
      'Review mash stability'
    )
    await user.click(screen.getByRole('button', { name: 'Ask the assistant' }))
    await waitFor(() =>
      expect(requestBodies()).toEqual([
        { mode: 'analysis', prompt: 'Review mash stability', brew_id: 'brew-a' },
      ])
    )
  })

  it('enables only the current session and keeps live questions bound to it', async () => {
    const user = userEvent.setup()
    mount(
      createElement(BrewAIMonitor, {
        deviceID: 'pro',
        deviceName: 'Bench Pro',
        monitor: {
          enabled: false,
          state: 'off',
          current_brew_id: 'native-current',
          configured: true,
        },
        error: '',
        reload,
      })
    )
    await user.click(screen.getByRole('button', { name: 'Monitor this brew with AI' }))
    await waitFor(() => expect(reload).toHaveBeenCalled())
    const put = fetchMock.mock.calls.find(
      ([url, options]) =>
        String(url) === '/api/v1/devices/pro/ai-monitor' && options?.method === 'PUT'
    )
    expect(JSON.parse(String(put?.[1]?.body))).toEqual({ enabled: true, brew_id: 'native-current' })
    await user.click(screen.getByRole('button', { name: 'Ask about this brew' }))
    await screen.findByText('openai · test-model')
    expect(screen.queryByLabelText('Connected machine')).toBeNull()
    await user.type(screen.getByLabelText('What would you like to explore?'), 'Explain this phase')
    await user.click(screen.getByRole('button', { name: 'Ask the assistant' }))
    await waitFor(() =>
      expect(requestBodies()).toEqual([
        { mode: 'live', prompt: 'Explain this phase', device_id: 'pro', brew_id: 'native-current' },
      ])
    )
  })

  it('shows observations automatically only after monitoring is bound to the current brew', async () => {
    fetchMock.mockImplementation(async (url) => {
      if (String(url).endsWith('/ai/settings'))
        return json({
          enabled: true,
          key_configured: true,
          provider: 'openai',
          model: 'test-model',
        })
      return json([
        {
          id: 'current',
          mode: 'live',
          device_id: 'pro',
          brew_id: 'native-current',
          created_at: '2026-01-01T12:00:00Z',
          status: 'complete',
          text: 'Current mash is stable.',
        },
        {
          id: 'old',
          mode: 'live',
          device_id: 'pro',
          brew_id: 'native-old',
          created_at: '2026-01-01T11:00:00Z',
          status: 'complete',
          text: 'Old session must stay hidden.',
        },
      ])
    })
    mount(
      createElement(BrewAIMonitor, {
        deviceID: 'pro',
        deviceName: 'Bench Pro',
        monitor: {
          enabled: true,
          state: 'active',
          brew_id: 'native-current',
          current_brew_id: 'native-current',
          configured: true,
        },
        error: '',
        reload,
      })
    )
    const observation = await screen.findByText('Current mash is stable.')
    expect(observation.closest('details')?.open).toBe(true)
    expect(screen.queryByText('Old session must stay hidden.')).toBeNull()
    expect(screen.getByRole('button', { name: 'Hide AI observations' })).toBeTruthy()
    expect(
      fetchMock.mock.calls.some(
        ([url]) =>
          String(url) === '/api/v1/ai/advice?brew_id=native-current&device_id=pro&mode=live'
      )
    ).toBe(true)
  })

  it('keeps saved advice readable but prevents live requests when session verification fails', async () => {
    const user = userEvent.setup()
    mount(
      createElement(BrewAIMonitor, {
        deviceID: 'pro',
        deviceName: 'Bench Pro',
        monitor: {
          enabled: false,
          state: 'off',
          current_brew_id: 'last-verified',
          configured: true,
        },
        error: 'The machine could not be verified.',
        reload,
      })
    )
    expect(
      (screen.getByRole('button', { name: 'Monitor this brew with AI' }) as HTMLButtonElement)
        .disabled
    ).toBe(true)
    await user.click(screen.getByRole('button', { name: 'View saved observations' }))
    await screen.findByText('openai · test-model')
    await user.type(
      screen.getByLabelText('What would you like to explore?'),
      'Question against stale data'
    )
    expect(
      (screen.getByRole('button', { name: 'Ask the assistant' }) as HTMLButtonElement).disabled
    ).toBe(true)
    expect(requestBodies()).toEqual([])
  })

  it('allows older journal advice and saved qualifications to be read with the provider disabled', async () => {
    fetchMock.mockImplementation(async (url) => {
      if (String(url).endsWith('/ai/settings'))
        return json({ enabled: false, key_configured: false, provider: 'openai', model: '' })
      return json(
        Array.from({ length: 10 }, (_, index) => ({
          id: `saved-${index}`,
          mode: 'analysis',
          brew_id: 'brew-a',
          device_id: 'removed-device',
          status: 'complete',
          created_at: '2026-01-01T12:00:00Z',
          text: `Saved observation ${index}`,
          limitations: [`Qualification ${index}`],
        }))
      )
    })
    const user = userEvent.setup()
    mount(createElement(AIWorkbench, { mode: 'analysis', brewID: 'brew-a', embedded: true }))
    await screen.findByText('Saved observation 0')
    expect(screen.queryByText('Saved observation 9')).toBeNull()
    await user.click(
      screen.getByRole('button', { name: 'Show all 10 saved observations and analyses' })
    )
    expect(screen.getByText('Saved observation 9')).toBeTruthy()
    expect(screen.getByText('Qualification 9')).toBeTruthy()
    expect(
      (screen.getByRole('button', { name: 'Ask the assistant' }) as HTMLButtonElement).disabled
    ).toBe(true)
    expect(requestBodies()).toEqual([])
  })
})
