// @vitest-environment jsdom
import { createElement } from 'react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { act, cleanup, render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { HubContext, type HubState } from '../context'
import { setCSRF } from '../lib/api'
import type { AISettings } from '../types'
import { AISettingsForm } from './AISettingsForm'

const initial: AISettings = {
  provider: 'openai',
  model: '',
  key_configured: false,
  enabled: false,
  monitor_enabled: false,
}
const notify = vi.fn()
const fetchMock = vi.fn<typeof fetch>()

function json(data: unknown, status = 200) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { 'Content-Type': 'application/json' },
  })
}

function mount() {
  return render(
    createElement(
      HubContext.Provider,
      { value: { notify } as unknown as HubState },
      createElement(AISettingsForm)
    )
  )
}

function modelCalls() {
  return fetchMock.mock.calls.filter(([path]) => String(path).startsWith('/api/v1/ai/models'))
}

beforeEach(() => {
  vi.stubGlobal('fetch', fetchMock)
  setCSRF('test-csrf')
  notify.mockReset()
  fetchMock.mockReset()
  sessionStorage.clear()
})
afterEach(() => {
  cleanup()
  vi.unstubAllGlobals()
})

describe('AI provider model discovery', () => {
  it('loads saved credentials automatically, preserves an unlisted model and refreshes on demand', async () => {
    fetchMock.mockImplementation(async (path) =>
      String(path) === '/api/v1/ai/settings'
        ? json({ ...initial, key_configured: true, model: 'private-model', enabled: true })
        : json({ provider: 'openai', models: [{ id: 'text-model', name: 'Text model' }] })
    )
    const user = userEvent.setup()
    mount()
    await screen.findByRole('option', { name: 'Text model (text-model)' })
    expect(screen.queryByLabelText('Check active brews every 5 minutes')).toBeNull()
    expect(screen.getByText(/Enable AI monitoring separately when you start a brew/)).toBeTruthy()
    expect(modelCalls()).toHaveLength(1)
    expect(modelCalls()[0][0]).toBe('/api/v1/ai/models?provider=openai&auth_mode=api_key')
    expect((screen.getByLabelText('Model') as HTMLSelectElement).value).toBe('private-model')
    expect(screen.getByRole('option', { name: 'private-model · current selection' })).toBeTruthy()
    await user.click(screen.getByRole('button', { name: 'Refresh models' }))
    await screen.findByRole('button', { name: 'Refresh models' })
    expect(modelCalls()).toHaveLength(2)
    expect((screen.getByLabelText('Model') as HTMLSelectElement).value).toBe('private-model')
  })

  it('connects an unsaved key before choosing a model and refreshes after explicit save', async () => {
    let saved = { ...initial }
    fetchMock.mockImplementation(async (path, options) => {
      if (String(path) === '/api/v1/ai/settings') {
        if (options?.method === 'PUT') {
          const body = JSON.parse(String(options.body))
          saved = { ...initial, ...body, key_configured: true }
        }
        return json(saved)
      }
      return json({ provider: 'openai', models: [{ id: 'text-model', name: 'Text model' }] })
    })
    const user = userEvent.setup()
    mount()
    const key = (await screen.findByLabelText('Provider API key')) as HTMLInputElement
    await user.type(key, 'test-provider-secret')
    expect(modelCalls()).toHaveLength(0)
    await user.click(screen.getByRole('button', { name: 'Connect and load models' }))
    await screen.findByRole('option', { name: 'Text model (text-model)' })
    const [, connection] = modelCalls()[0]
    expect(connection?.method).toBe('POST')
    expect(connection?.headers).toMatchObject({ 'X-CSRF-Token': 'test-csrf' })
    expect(JSON.parse(String(connection?.body))).toEqual({
      provider: 'openai',
      auth_mode: 'api_key',
      api_key: 'test-provider-secret',
    })
    expect((screen.getByLabelText('Model') as HTMLSelectElement).value).toBe('')
    expect(fetchMock.mock.calls.some(([, options]) => options?.method === 'PUT')).toBe(false)
    await user.selectOptions(screen.getByLabelText('Model'), 'text-model')
    await user.click(screen.getByLabelText('Enable the brew assistant'))
    await user.click(screen.getByRole('button', { name: 'Save assistant settings' }))
    await waitFor(() => expect(notify).toHaveBeenCalledWith('Brew assistant settings saved.'))
    await waitFor(() => expect(modelCalls()).toHaveLength(2))
    expect(modelCalls()[1][0]).toBe('/api/v1/ai/models?provider=openai&auth_mode=api_key')
    expect(key.value).toBe('')
    const update = fetchMock.mock.calls.find(([, options]) => options?.method === 'PUT')!
    expect(JSON.parse(String(update[1]?.body))).toMatchObject({
      provider: 'openai',
      model: 'text-model',
      enabled: true,
      api_key: 'test-provider-secret',
    })
  })

  it('does not reuse another provider’s key status or display its late model response', async () => {
    let resolveOld!: (response: Response) => void
    fetchMock.mockImplementation(async (path) => {
      if (String(path) === '/api/v1/ai/settings')
        return json({ ...initial, key_configured: true, model: 'old-model' })
      if (String(path).includes('provider=openai'))
        return new Promise<Response>((resolve) => {
          resolveOld = resolve
        })
      return json({ error: { code: 'ai_key_missing', message: 'Configure a provider key.' } }, 409)
    })
    const user = userEvent.setup()
    mount()
    await waitFor(() => expect(modelCalls()).toHaveLength(1))
    const oldSignal = modelCalls()[0][1]?.signal
    await user.selectOptions(screen.getByLabelText('Provider'), 'anthropic')
    await waitFor(() => expect(modelCalls()).toHaveLength(2))
    const button = await screen.findByRole('button', { name: 'Connect and load models' })
    expect((button as HTMLButtonElement).disabled).toBe(true)
    expect((screen.getByLabelText('Provider API key') as HTMLInputElement).placeholder).toBe('')
    expect(oldSignal?.aborted).toBe(true)
    await act(async () => {
      resolveOld(json({ provider: 'openai', models: [{ id: 'late-model', name: 'Late model' }] }))
    })
    expect(screen.queryByRole('option', { name: /Late model/ })).toBeNull()
    expect((screen.getByLabelText('Model') as HTMLSelectElement).value).toBe('')
    expect((screen.getByLabelText('Provider') as HTMLSelectElement).value).toBe('anthropic')
  })

  it('keeps manual model entry available when discovery fails and never saves the key on connect', async () => {
    fetchMock.mockImplementation(async (path) =>
      String(path) === '/api/v1/ai/settings'
        ? json({ ...initial, key_configured: true, model: 'kept-model' })
        : json({ error: { code: 'ai_provider', message: 'Model access is unavailable.' } }, 502)
    )
    const user = userEvent.setup()
    mount()
    await screen.findByRole('alert')
    expect(screen.getByRole('alert').textContent).toContain('Model access is unavailable.')
    expect((screen.getByLabelText('Model') as HTMLSelectElement).value).toBe('kept-model')
    await user.click(screen.getByLabelText('Enter model ID manually'))
    const model = screen.getByRole('textbox', { name: 'Model' }) as HTMLInputElement
    expect(model.value).toBe('kept-model')
    await user.clear(model)
    await user.type(model, 'private-finetuned-model')
    expect(model.value).toBe('private-finetuned-model')
    await user.selectOptions(screen.getByLabelText('Provider'), 'anthropic')
    await screen.findByRole('alert')
    await waitFor(() =>
      expect((screen.getByLabelText('Provider API key') as HTMLInputElement).placeholder).toContain(
        'key configured'
      )
    )
    expect(fetchMock.mock.calls.some(([, options]) => options?.method === 'PUT')).toBe(false)
  })

  it('cancels model discovery when the key changes and when the settings form unmounts', async () => {
    let resolveOld!: (response: Response) => void
    fetchMock.mockImplementation(async (path) => {
      if (String(path) === '/api/v1/ai/settings') return json({ ...initial, key_configured: true })
      return new Promise<Response>((resolve) => {
        resolveOld = resolve
      })
    })
    const user = userEvent.setup()
    const view = mount()
    await waitFor(() => expect(modelCalls()).toHaveLength(1))
    const firstSignal = modelCalls()[0][1]?.signal
    await user.type(screen.getByLabelText('Provider API key'), 'new-key')
    expect(firstSignal?.aborted).toBe(true)
    await act(async () => {
      resolveOld(
        json({ provider: 'openai', models: [{ id: 'old-key-model', name: 'Old key model' }] })
      )
    })
    expect(screen.queryByRole('option', { name: /Old key model/ })).toBeNull()
    expect(modelCalls()).toHaveLength(1)
    await user.click(screen.getByRole('button', { name: 'Connect and load models' }))
    await waitFor(() => expect(modelCalls()).toHaveLength(2))
    const secondSignal = modelCalls()[1][1]?.signal
    view.unmount()
    expect(secondSignal?.aborted).toBe(true)
  })
})

const chatGPTStatus = {
  provider: 'openai',
  installed: true,
  authenticated: false,
  message: 'Sign in with ChatGPT to connect your subscription.',
  login_command: '',
}
const callbackAddress = 'http://127.0.0.1:1455/auth/callback'
const subscriptionModels = {
  provider: 'openai',
  auth_mode: 'subscription',
  models: [{ id: 'subscription-model', name: 'Subscription model' }],
}

function subscriptionCalls() {
  return fetchMock.mock.calls.filter(([path]) => String(path).startsWith('/api/v1/ai/subscription'))
}

describe('AI subscription connections', () => {
  it('loads the saved subscription instead of a saved API key and saves the selected billing mode', async () => {
    let saved: AISettings = {
      ...initial,
      auth_mode: 'subscription',
      key_configured: true,
      model: 'subscription-model',
    }
    fetchMock.mockImplementation(async (path, options) => {
      if (String(path) === '/api/v1/ai/settings') {
        if (options?.method === 'PUT') saved = { ...saved, ...JSON.parse(String(options.body)) }
        return json(saved)
      }
      if (String(path).startsWith('/api/v1/ai/subscription'))
        return json({ ...chatGPTStatus, authenticated: true, message: 'Connected to ChatGPT.' })
      return json(subscriptionModels)
    })
    const user = userEvent.setup()
    mount()
    await screen.findByRole('option', { name: 'Subscription model (subscription-model)' })
    expect(screen.queryByLabelText('Provider API key')).toBeNull()
    expect(modelCalls()).toHaveLength(1)
    expect(modelCalls()[0][0]).toBe('/api/v1/ai/models?provider=openai&auth_mode=subscription')
    await user.click(screen.getByLabelText('Enable the brew assistant'))
    await user.click(screen.getByRole('button', { name: 'Save assistant settings' }))
    await waitFor(() => expect(notify).toHaveBeenCalled())
    const savedCall = fetchMock.mock.calls.find(([, options]) => options?.method === 'PUT')!
    expect(JSON.parse(String(savedCall[1]?.body))).toEqual({
      provider: 'openai',
      auth_mode: 'subscription',
      enabled: true,
      model: 'subscription-model',
    })
  })

  it('finishes browser sign-in with the full callback URL and then discovers subscription models', async () => {
    const completed = `${callbackAddress}?code=one-time-code&state=csrf-state`
    fetchMock.mockImplementation(async (path, options) => {
      if (String(path) === '/api/v1/ai/settings') return json(initial)
      if (String(path) === '/api/v1/ai/subscription/login') {
        expect(JSON.parse(String(options?.body))).toEqual({
          provider: 'openai',
          redirect_uri: callbackAddress,
        })
        expect(options?.headers).toMatchObject({ 'X-CSRF-Token': 'test-csrf' })
        return json({
          ...chatGPTStatus,
          pending: true,
          callback_url: callbackAddress,
          authorization_url: 'https://auth.openai.com/oauth/authorize?state=csrf-state',
        })
      }
      if (String(path) === '/api/v1/ai/subscription/callback') {
        expect(JSON.parse(String(options?.body))).toEqual({
          provider: 'openai',
          callback_url: completed,
        })
        return json({ ...chatGPTStatus, authenticated: true, message: 'Connected to ChatGPT.' })
      }
      if (String(path).startsWith('/api/v1/ai/subscription')) return json(chatGPTStatus)
      return json(subscriptionModels)
    })
    const user = userEvent.setup()
    mount()
    await user.type(await screen.findByLabelText('Provider API key'), 'unsaved-api-secret')
    await user.selectOptions(screen.getByLabelText('Connection method'), 'subscription')
    await user.click(await screen.findByRole('button', { name: 'Sign in with ChatGPT' }))
    const link = await screen.findByRole('link', { name: 'Open ChatGPT sign-in' })
    expect(link.getAttribute('href')).toBe(
      'https://auth.openai.com/oauth/authorize?state=csrf-state'
    )
    expect(link.getAttribute('rel')).toContain('noreferrer')
    expect(screen.getByText(callbackAddress)).toBeTruthy()
    expect(modelCalls()).toHaveLength(0)
    await user.type(screen.getByLabelText('Completed sign-in URL'), completed)
    await user.click(screen.getByRole('button', { name: 'Finish ChatGPT sign-in' }))
    await screen.findByRole('option', { name: 'Subscription model (subscription-model)' })
    expect(screen.queryByLabelText('Completed sign-in URL')).toBeNull()
    expect(
      subscriptionCalls().some(([, options]) =>
        String(options?.body).includes('unsaved-api-secret')
      )
    ).toBe(false)
    expect(modelCalls()[0][1]?.method).toBe('GET')
  })

  it('keeps the callback address visible when Settings is reopened during a pending sign-in', async () => {
    fetchMock.mockImplementation(async (path) =>
      String(path) === '/api/v1/ai/settings'
        ? json({ ...initial, auth_mode: 'subscription' })
        : json({
            ...chatGPTStatus,
            pending: true,
            callback_url: callbackAddress,
            message: 'Finish or restart sign-in.',
          })
    )
    mount()
    await screen.findByLabelText('Completed sign-in URL')
    expect(screen.getByText(callbackAddress)).toBeTruthy()
    expect(screen.getByRole('button', { name: 'Restart ChatGPT sign-in' })).toBeTruthy()
    expect(modelCalls()).toHaveLength(0)
  })

  it('ignores a late API model response after switching the connection method', async () => {
    let resolveOld!: (response: Response) => void
    fetchMock.mockImplementation(async (path) => {
      if (String(path) === '/api/v1/ai/settings') return json({ ...initial, key_configured: true })
      if (String(path).includes('auth_mode=api_key'))
        return new Promise<Response>((resolve) => {
          resolveOld = resolve
        })
      return json(chatGPTStatus)
    })
    const user = userEvent.setup()
    mount()
    await waitFor(() => expect(modelCalls()).toHaveLength(1))
    const oldSignal = modelCalls()[0][1]?.signal
    await user.selectOptions(screen.getByLabelText('Connection method'), 'subscription')
    await screen.findByRole('button', { name: 'Sign in with ChatGPT' })
    expect(oldSignal?.aborted).toBe(true)
    await act(async () =>
      resolveOld(json({ provider: 'openai', models: [{ id: 'api-only-model', name: 'API only' }] }))
    )
    expect(screen.queryByRole('option', { name: /API only/ })).toBeNull()
    expect((screen.getByLabelText('Connection method') as HTMLSelectElement).value).toBe(
      'subscription'
    )
    expect(screen.queryByLabelText('Provider API key')).toBeNull()
    expect(modelCalls()).toHaveLength(1)
  })

  it('ignores a late subscription check after changing providers', async () => {
    let resolveOld!: (response: Response) => void
    fetchMock.mockImplementation(async (path) => {
      if (String(path) === '/api/v1/ai/settings')
        return json({ ...initial, auth_mode: 'subscription' })
      if (String(path).includes('provider=openai'))
        return new Promise<Response>((resolve) => {
          resolveOld = resolve
        })
      return json({
        ...chatGPTStatus,
        provider: 'anthropic',
        installed: false,
        login_command: 'claude auth login',
        message: 'Install Claude Code first.',
      })
    })
    const user = userEvent.setup()
    mount()
    await waitFor(() => expect(subscriptionCalls()).toHaveLength(1))
    const oldSignal = subscriptionCalls()[0][1]?.signal
    await user.selectOptions(screen.getByLabelText('Provider'), 'anthropic')
    await screen.findByText('Install Claude Code first.')
    expect(oldSignal?.aborted).toBe(true)
    await act(async () =>
      resolveOld(json({ ...chatGPTStatus, authenticated: true, message: 'Old account connected.' }))
    )
    expect(screen.queryByText('Old account connected.')).toBeNull()
    expect(modelCalls()).toHaveLength(0)
    expect(screen.getByText('claude auth login')).toBeTruthy()
  })

  it('shows Claude model aliases only after checking the native subscription connection', async () => {
    fetchMock.mockImplementation(async (path) => {
      if (String(path) === '/api/v1/ai/settings')
        return json({ ...initial, provider: 'anthropic', auth_mode: 'subscription' })
      if (String(path).startsWith('/api/v1/ai/subscription'))
        return json({
          ...chatGPTStatus,
          provider: 'anthropic',
          authenticated: true,
          message: 'Claude subscription connected.',
          login_command: 'claude auth login',
        })
      return json({
        provider: 'anthropic',
        auth_mode: 'subscription',
        source: 'subscription-aliases',
        models: [
          { id: 'default', name: 'Account default' },
          { id: 'sonnet', name: 'Sonnet alias' },
        ],
      })
    })
    mount()
    await screen.findByRole('option', { name: 'Account default (default)' })
    expect(screen.getByText(/model availability depends on your plan/)).toBeTruthy()
    expect(screen.getByText(/not a list of your account entitlements/)).toBeTruthy()
    expect(modelCalls()[0][0]).toBe('/api/v1/ai/models?provider=anthropic&auth_mode=subscription')
    expect(screen.queryByRole('button', { name: 'Sign in with ChatGPT' })).toBeNull()
  })

  it('keeps subscription billing selected when its provider fails and never tries the saved key', async () => {
    fetchMock.mockImplementation(async (path) => {
      if (String(path) === '/api/v1/ai/settings')
        return json({ ...initial, auth_mode: 'subscription', key_configured: true })
      if (String(path).startsWith('/api/v1/ai/subscription'))
        return json({ ...chatGPTStatus, authenticated: true })
      return json(
        { error: { code: 'ai_provider', message: 'Your subscription limit has been reached.' } },
        502
      )
    })
    mount()
    await screen.findByRole('alert')
    expect(screen.getByRole('alert').textContent).toContain('subscription limit')
    expect((screen.getByLabelText('Connection method') as HTMLSelectElement).value).toBe(
      'subscription'
    )
    expect(modelCalls()).toHaveLength(1)
    expect(String(modelCalls()[0][0])).toContain('auth_mode=subscription')
    expect(screen.queryByLabelText('Provider API key')).toBeNull()
  })
})

describe('ChatGPT sign-in recovery', () => {
  it('restores unfinished sign-in after reload before settings were saved, without storing credential URLs', async () => {
    let pending = false
    fetchMock.mockImplementation(async (path) => {
      if (String(path) === '/api/v1/ai/settings') return json(initial)
      if (String(path) === '/api/v1/ai/subscription/login') {
        pending = true
        return json({
          ...chatGPTStatus,
          pending,
          callback_url: callbackAddress,
          authorization_url: 'https://auth.openai.com/oauth/authorize?state=secret-state',
        })
      }
      return json({
        ...chatGPTStatus,
        pending,
        callback_url: pending ? callbackAddress : undefined,
      })
    })
    const user = userEvent.setup()
    let view = mount()
    await user.selectOptions(await screen.findByLabelText('Connection method'), 'subscription')
    await user.click(await screen.findByRole('button', { name: 'Sign in with ChatGPT' }))
    await screen.findByRole('link', { name: 'Open ChatGPT sign-in' })
    const stored = sessionStorage.getItem('picobrew:pending-chatgpt-login')!
    expect(JSON.parse(stored)).toEqual({ provider: 'openai', auth_mode: 'subscription' })
    expect(stored).not.toContain('secret-state')
    view.unmount()
    view = mount()
    await screen.findByLabelText('Completed sign-in URL')
    expect((screen.getByLabelText('Connection method') as HTMLSelectElement).value).toBe(
      'subscription'
    )
    expect(screen.getByText(callbackAddress)).toBeTruthy()
    expect(screen.queryByLabelText('Provider API key')).toBeNull()
    expect(fetchMock.mock.calls.some(([, options]) => options?.method === 'PUT')).toBe(false)
    view.unmount()
  })
})

describe('ChatGPT local callback completion', () => {
  it('polls only a pending subscription and loads models after the local callback completes', async () => {
    let statusChecks = 0
    sessionStorage.setItem(
      'picobrew:pending-chatgpt-login',
      JSON.stringify({ provider: 'openai', auth_mode: 'subscription' })
    )
    fetchMock.mockImplementation(async (path) => {
      if (String(path) === '/api/v1/ai/settings') return json(initial)
      if (String(path).startsWith('/api/v1/ai/subscription')) {
        statusChecks++
        return json({
          ...chatGPTStatus,
          pending: statusChecks === 1,
          authenticated: statusChecks > 1,
          callback_url: callbackAddress,
          message: statusChecks === 1 ? 'Waiting for sign-in.' : 'Connected locally.',
        })
      }
      return json(subscriptionModels)
    })
    const view = mount()
    await screen.findByLabelText('Completed sign-in URL')
    await screen.findByRole(
      'option',
      { name: 'Subscription model (subscription-model)' },
      { timeout: 5000 }
    )
    expect(statusChecks).toBe(2)
    expect(screen.queryByLabelText('Completed sign-in URL')).toBeNull()
    expect(sessionStorage.getItem('picobrew:pending-chatgpt-login')).toBeNull()
    expect(subscriptionCalls().some(([, options]) => options?.method === 'POST')).toBe(false)
    expect(modelCalls()).toHaveLength(1)
    view.unmount()
  })
})
