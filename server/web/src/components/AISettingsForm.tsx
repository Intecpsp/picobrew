import { useCallback, useEffect, useRef, useState, type FormEvent } from 'react'
import { RefreshCw } from 'lucide-react'
import { useHub, useResource } from '../context'
import { api, APIError, errorMessage } from '../lib/api'
import type { AIAuthMode, AIModel, AIModels, AISettings, AISubscription } from '../types'
import { Button, ErrorBanner, Field, Loading } from './ui'

const chatGPTCallback = 'http://127.0.0.1:1455/auth/callback'
const pendingLoginStorageKey = 'picobrew:pending-chatgpt-login'
const authMode = (value?: AIAuthMode): AIAuthMode => value || 'api_key'
type PendingRequest = { sequence: number; controller?: AbortController }

// Remember only the selected connection while browser sign-in is in progress.
// Credentials and callback/authorization URLs stay out of browser storage.
function rememberPendingLogin(pending: boolean) {
  try {
    if (pending)
      sessionStorage.setItem(
        pendingLoginStorageKey,
        JSON.stringify({ provider: 'openai', auth_mode: 'subscription' })
      )
    else sessionStorage.removeItem(pendingLoginStorageKey)
  } catch {
    // Browsers may disable storage. The server still retains the pending flow.
  }
}

function hasPendingLogin() {
  try {
    const value = JSON.parse(sessionStorage.getItem(pendingLoginStorageKey) || 'null')
    return value?.provider === 'openai' && value?.auth_mode === 'subscription'
  } catch {
    return false
  }
}

export function AISettingsForm() {
  const hub = useHub()
  const resource = useResource<AISettings>('/ai/settings')
  const [form, setForm] = useState<AISettings>({
    provider: 'openai',
    auth_mode: 'api_key',
    model: '',
    enabled: false,
    key_configured: false,
  })
  const [key, setKey] = useState('')
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState('')
  const [models, setModels] = useState<AIModel[]>([])
  const [modelsState, setModelsState] = useState<'idle' | 'loading' | 'ready' | 'error'>('idle')
  const [modelsError, setModelsError] = useState('')
  const [manualModel, setManualModel] = useState(false)
  const [subscription, setSubscription] = useState<AISubscription | null>(null)
  const [subscriptionBusy, setSubscriptionBusy] = useState(false)
  const [subscriptionError, setSubscriptionError] = useState('')
  const [authorizationURL, setAuthorizationURL] = useState('')
  const [callback, setCallback] = useState('')
  const modelRequest = useRef<PendingRequest>({ sequence: 0 })
  const subscriptionRequest = useRef<PendingRequest>({ sequence: 0 })
  const savedKeys = useRef<Record<string, boolean>>({})

  const cancelModels = useCallback(() => {
    modelRequest.current.sequence += 1
    modelRequest.current.controller?.abort()
  }, [])
  const cancelSubscription = useCallback(() => {
    subscriptionRequest.current.sequence += 1
    subscriptionRequest.current.controller?.abort()
  }, [])

  const loadModels = useCallback(
    async (provider: string, mode: AIAuthMode, apiKey = '') => {
      cancelModels()
      const sequence = modelRequest.current.sequence
      const controller = new AbortController()
      modelRequest.current.controller = controller
      const current = () => sequence === modelRequest.current.sequence && !controller.signal.aborted
      setModelsState('loading')
      setModelsError('')
      try {
        const result = await api<AIModels>(
          apiKey
            ? '/ai/models'
            : `/ai/models?provider=${encodeURIComponent(provider)}&auth_mode=${mode}`,
          {
            signal: controller.signal,
            ...(apiKey
              ? { method: 'POST', body: { provider, auth_mode: mode, api_key: apiKey } }
              : {}),
          }
        )
        if (!current()) return
        if (result.provider !== provider || authMode(result.auth_mode) !== mode)
          throw new Error('The provider returned an unexpected model list.')
        setModels(result.models)
        setModelsState('ready')
        if (mode === 'api_key' && !apiKey) {
          savedKeys.current[provider] = true
          setForm((value) =>
            value.provider === provider && authMode(value.auth_mode) === mode
              ? { ...value, key_configured: true }
              : value
          )
        }
      } catch (error) {
        if (!current()) return
        setModels([])
        if (mode === 'api_key' && error instanceof APIError && error.code === 'ai_key_missing') {
          savedKeys.current[provider] = false
          setForm((value) =>
            value.provider === provider && authMode(value.auth_mode) === mode
              ? { ...value, key_configured: false }
              : value
          )
          setModelsState('idle')
        } else {
          // An API rejection still establishes that this provider has a saved key.
          // Subscription errors must never affect key status or switch billing.
          if (
            mode === 'api_key' &&
            !apiKey &&
            error instanceof APIError &&
            error.code === 'ai_provider'
          ) {
            savedKeys.current[provider] = true
            setForm((value) =>
              value.provider === provider && authMode(value.auth_mode) === mode
                ? { ...value, key_configured: true }
                : value
            )
          }
          setModelsState('error')
          setModelsError(errorMessage(error))
        }
      }
    },
    [cancelModels]
  )

  const connectSubscription = useCallback(
    async (
      provider: string,
      action: 'status' | 'poll' | 'login' | 'callback' | 'disconnect' = 'status',
      callbackURL = ''
    ) => {
      cancelSubscription()
      if (action !== 'poll') cancelModels()
      const sequence = subscriptionRequest.current.sequence
      const controller = new AbortController()
      subscriptionRequest.current.controller = controller
      const current = () =>
        sequence === subscriptionRequest.current.sequence && !controller.signal.aborted
      if (action !== 'poll') {
        setSubscriptionBusy(true)
        setSubscriptionError('')
        setModels([])
        setModelsState('idle')
        setModelsError('')
      }
      try {
        const path =
          action === 'login' || action === 'callback'
            ? `/ai/subscription/${action}`
            : `/ai/subscription?provider=${encodeURIComponent(provider)}`
        const result = await api<AISubscription>(path, {
          signal: controller.signal,
          ...(action === 'login'
            ? { method: 'POST', body: { provider, redirect_uri: chatGPTCallback } }
            : action === 'callback'
              ? { method: 'POST', body: { provider, callback_url: callbackURL.trim() } }
              : action === 'disconnect'
                ? { method: 'DELETE' }
                : {}),
        })
        if (!current()) return
        if (result.provider !== provider)
          throw new Error('The provider returned an unexpected connection status.')
        if (action === 'login' && result.pending) rememberPendingLogin(true)
        if (action === 'disconnect' || (action === 'callback' && result.authenticated))
          rememberPendingLogin(false)
        if ((action === 'status' || action === 'poll') && provider === 'openai' && !result.pending)
          rememberPendingLogin(false)
        setSubscription(result)
        setSubscriptionError('')
        if ((action !== 'status' && action !== 'poll') || !result.pending)
          setAuthorizationURL(result.authorization_url || '')
        if (result.authenticated || action === 'disconnect') setCallback('')
        if (result.authenticated) void loadModels(provider, 'subscription')
      } catch (error) {
        if (!current()) return
        setSubscriptionError(errorMessage(error))
      } finally {
        if (current() && action !== 'poll') setSubscriptionBusy(false)
      }
    },
    [cancelSubscription, cancelModels, loadModels]
  )

  useEffect(
    () => () => {
      cancelModels()
      cancelSubscription()
    },
    [cancelModels, cancelSubscription]
  )

  useEffect(() => {
    if (!resource.data) return
    cancelModels()
    cancelSubscription()
    const resumeLogin = hasPendingLogin()
    const saved = resumeLogin
      ? {
          ...resource.data,
          provider: 'openai',
          auth_mode: 'subscription' as const,
          model: '',
          key_configured:
            resource.data.provider === 'openai'
              ? resource.data.key_configured
              : savedKeys.current.openai || false,
        }
      : { ...resource.data, auth_mode: authMode(resource.data.auth_mode) }
    savedKeys.current[resource.data.provider] = resource.data.key_configured
    setForm(saved)
    setKey('')
    setModels([])
    setModelsState('idle')
    setModelsError('')
    setSubscription(null)
    setSubscriptionBusy(false)
    setSubscriptionError('')
    setAuthorizationURL('')
    setCallback('')
    if (saved.auth_mode === 'subscription') void connectSubscription(saved.provider)
    else if (saved.key_configured) void loadModels(saved.provider, 'api_key')
  }, [resource.data, cancelModels, cancelSubscription, loadModels, connectSubscription])

  useEffect(() => {
    if (
      form.provider !== 'openai' ||
      authMode(form.auth_mode) !== 'subscription' ||
      !subscription?.pending ||
      subscription.authenticated ||
      subscriptionBusy
    )
      return
    let stopped = false
    let timer: ReturnType<typeof setTimeout>
    const deadline = Date.now() + 10 * 60 * 1000
    const poll = async () => {
      if (stopped || Date.now() >= deadline) return
      await connectSubscription('openai', 'poll')
      if (!stopped) timer = setTimeout(poll, 2000)
    }
    timer = setTimeout(poll, 2000)
    return () => {
      stopped = true
      clearTimeout(timer)
    }
  }, [
    form.provider,
    form.auth_mode,
    subscription?.pending,
    subscription?.authenticated,
    subscriptionBusy,
    connectSubscription,
  ])

  function chooseConnection(provider: string, mode: AIAuthMode) {
    if (provider !== 'openai' || mode !== 'subscription') rememberPendingLogin(false)
    cancelModels()
    cancelSubscription()
    setForm({
      ...form,
      provider,
      auth_mode: mode,
      model: '',
      key_configured: savedKeys.current[provider] || false,
    })
    setKey('')
    setModels([])
    setModelsState('idle')
    setModelsError('')
    setManualModel(false)
    setError('')
    setSubscription(null)
    setSubscriptionBusy(false)
    setSubscriptionError('')
    setAuthorizationURL('')
    setCallback('')
    if (mode === 'subscription') void connectSubscription(provider)
    else void loadModels(provider, 'api_key')
  }

  function changeKey(value: string) {
    cancelModels()
    setKey(value)
    setModels([])
    setModelsState('idle')
    setModelsError('')
    setError('')
  }

  async function save(event: FormEvent) {
    event.preventDefault()
    setBusy(true)
    setError('')
    try {
      const mode = authMode(form.auth_mode)
      const saved = await api<AISettings>('/ai/settings', {
        method: 'PUT',
        body: {
          provider: form.provider,
          auth_mode: mode,
          model: form.model.trim(),
          enabled: form.enabled,
          ...(mode === 'api_key' && key ? { api_key: key } : {}),
        },
      })
      setKey('')
      rememberPendingLogin(false)
      resource.setData(saved)
      hub.notify('Brew assistant settings saved.')
    } catch (error) {
      setError(errorMessage(error))
    } finally {
      setBusy(false)
    }
  }

  if (resource.loading) return <Loading label="Loading assistant settings…" />
  const mode = authMode(form.auth_mode)
  const subscriptionMode = mode === 'subscription'
  const claudeSubscription = subscriptionMode && form.provider === 'anthropic'
  const loadingModels = modelsState === 'loading'
  const currentNotListed = form.model && !models.some((model) => model.id === form.model)
  const canLoadModels = subscriptionMode
    ? Boolean(subscription?.authenticated)
    : Boolean(key || form.key_configured)
  return (
    <form onSubmit={save}>
      <label className="check-field">
        <input
          type="checkbox"
          checked={form.enabled}
          disabled={busy}
          onChange={(event) => setForm({ ...form, enabled: event.target.checked })}
        />
        Enable the brew assistant
      </label>
      <Field label="Provider">
        <select
          value={form.provider}
          disabled={busy}
          onChange={(event) => chooseConnection(event.target.value, mode)}
        >
          <option value="openai">OpenAI / ChatGPT</option>
          <option value="anthropic">Anthropic / Claude</option>
        </select>
      </Field>
      <Field label="Connection method">
        <select
          value={mode}
          disabled={busy}
          onChange={(event) => chooseConnection(form.provider, event.target.value as AIAuthMode)}
        >
          <option value="api_key">API key · provider API billing</option>
          <option value="subscription">
            {form.provider === 'openai' ? 'ChatGPT subscription' : 'Claude subscription'}
          </option>
        </select>
      </Field>
      {subscriptionMode ? (
        <section aria-label="Subscription connection">
          <p className="help-text">
            {claudeSubscription
              ? 'Uses the official Claude Code app signed in with your Claude subscription on the computer running the Hub. Your plan’s limits and account settings apply.'
              : 'Sign in with ChatGPT to use your plan’s available models and usage limits. Your Hub API key is not used.'}
          </p>
          <p className="help-text" role="status" aria-live="polite">
            {subscriptionBusy
              ? 'Checking subscription connection…'
              : subscription?.message || 'Check your subscription connection to continue.'}
          </p>
          {subscription?.account && (
            <p className="help-text">Connected account: {subscription.account}</p>
          )}
          {claudeSubscription ? (
            <>
              <p className="help-text">
                On the Hub host, install the{' '}
                <a href="https://code.claude.com/docs/en/setup" target="_blank" rel="noreferrer">
                  official native Claude Code app
                </a>{' '}
                (2.1.259 or newer), then run{' '}
                <code>{subscription?.login_command || 'claude auth login'}</code> as the same user
                running the Hub. Choose your Claude subscription when signing in.
              </p>
              <Button
                type="button"
                variant="secondary"
                disabled={busy || subscriptionBusy}
                onClick={() => void connectSubscription(form.provider)}
              >
                Check Claude connection
              </Button>
            </>
          ) : (
            <>
              <div className="inline-actions">
                <Button
                  type="button"
                  variant="secondary"
                  disabled={busy || subscriptionBusy}
                  onClick={() => void connectSubscription('openai', 'login')}
                >
                  {subscription?.authenticated
                    ? 'Sign in with another ChatGPT account'
                    : subscription?.pending
                      ? 'Restart ChatGPT sign-in'
                      : 'Sign in with ChatGPT'}
                </Button>
                {subscription?.authenticated && (
                  <Button
                    type="button"
                    variant="secondary"
                    disabled={busy || subscriptionBusy}
                    onClick={() => void connectSubscription('openai', 'disconnect')}
                  >
                    Disconnect ChatGPT
                  </Button>
                )}
                <Button
                  type="button"
                  variant="secondary"
                  disabled={busy || subscriptionBusy}
                  onClick={() => void connectSubscription('openai')}
                >
                  Check ChatGPT connection
                </Button>
              </div>
              {authorizationURL && (
                <p>
                  <a href={authorizationURL} target="_blank" rel="noreferrer">
                    Open ChatGPT sign-in
                  </a>
                </p>
              )}
              {(subscription?.pending || authorizationURL) && (
                <>
                  <p className="help-text">
                    Sign-in completes automatically when this browser can reach the Hub’s local
                    callback. If the Hub runs on another computer, or the final tab shows a
                    connection error, copy the full address from that tab and paste it below. Keep
                    Settings open while signing in.
                  </p>
                  <p className="help-text">
                    Callback address: <code>{subscription?.callback_url || chatGPTCallback}</code>
                  </p>
                  <Field
                    label="Completed sign-in URL"
                    hint="Paste the full callback address, including its code and state. It is sent only to this Hub to finish your sign-in."
                  >
                    <input
                      type="text"
                      value={callback}
                      disabled={busy || subscriptionBusy}
                      autoComplete="off"
                      autoCapitalize="none"
                      spellCheck={false}
                      onChange={(event) => setCallback(event.target.value)}
                    />
                  </Field>
                  <Button
                    type="button"
                    variant="secondary"
                    disabled={busy || subscriptionBusy || !callback.trim()}
                    onClick={() => void connectSubscription('openai', 'callback', callback)}
                  >
                    Finish ChatGPT sign-in
                  </Button>
                </>
              )}
            </>
          )}
          <ErrorBanner message={subscriptionError} />
        </section>
      ) : (
        <Field
          label="Provider API key"
          hint={
            form.key_configured
              ? 'A key is saved for this provider. Leave this blank to keep it. Uses provider API billing.'
              : 'Enter an API key from this provider. Uses provider API billing and any eligible account credits.'
          }
        >
          <input
            type="password"
            autoComplete="new-password"
            required={form.enabled && !form.key_configured}
            disabled={busy}
            value={key}
            onChange={(event) => changeKey(event.target.value)}
            placeholder={form.key_configured ? '•••••••• · key configured' : ''}
          />
        </Field>
      )}
      <div className="inline-actions">
        <Button
          type="button"
          variant="secondary"
          disabled={busy || subscriptionBusy || loadingModels || !canLoadModels}
          onClick={() => void loadModels(form.provider, mode, subscriptionMode ? '' : key)}
        >
          <RefreshCw size={15} className={loadingModels ? 'spin' : ''} />
          {loadingModels
            ? 'Loading models…'
            : !subscriptionMode && (key || !form.key_configured)
              ? 'Connect and load models'
              : 'Refresh models'}
        </Button>
      </div>
      <p className="help-text" role="status" aria-live="polite">
        {loadingModels
          ? claudeSubscription
            ? 'Checking the Claude Code connection…'
            : 'Loading models for your selected connection…'
          : modelsState === 'ready'
            ? models.length
              ? claudeSubscription
                ? 'Connected. Choose a Claude Code model alias; model availability depends on your plan.'
                : `Connected. ${models.length} model${models.length === 1 ? '' : 's'} available. Choose a model and save your settings.`
              : 'Connected, but no models were returned. Check your account permissions or enter a model ID manually.'
            : 'Connect your provider to load its models. You can also enter a model ID manually.'}
      </p>
      <ErrorBanner message={modelsError} />
      <Field
        label="Model"
        hint={
          claudeSubscription
            ? 'These are official Claude Code aliases, not a list of your account entitlements. Default uses your account’s default model.'
            : subscriptionMode
              ? 'Choose a model available through your connected ChatGPT account.'
              : 'Choose a model that supports the provider’s chat API. The available list comes from your provider account.'
        }
      >
        {manualModel ? (
          <input
            value={form.model}
            required={form.enabled}
            disabled={busy}
            onChange={(event) => setForm({ ...form, model: event.target.value })}
            placeholder="Enter a model ID"
          />
        ) : (
          <select
            value={form.model}
            required={form.enabled}
            disabled={busy || loadingModels}
            onChange={(event) => setForm({ ...form, model: event.target.value })}
          >
            <option value="">Choose a model</option>
            {currentNotListed && (
              <option value={form.model}>{form.model} · current selection</option>
            )}
            {models.map((model) => (
              <option key={model.id} value={model.id}>
                {model.name && model.name !== model.id ? `${model.name} (${model.id})` : model.id}
              </option>
            ))}
          </select>
        )}
      </Field>
      <label className="check-field">
        <input
          type="checkbox"
          checked={manualModel}
          disabled={busy}
          onChange={(event) => setManualModel(event.target.checked)}
        />
        Enter model ID manually
      </label>
      <p className="help-text">
        Enable AI monitoring separately when you start a brew or from that machine’s live controls.
        Observations stay with that session in the brew journal.{' '}
        <a href="#/overview">Open your machines</a> to choose a brew to monitor.
      </p>
      <p className="help-text">
        Selected recipes and readings are sent to your chosen provider. Suggestions are advisory;
        the assistant cannot start a machine or change its outputs. You review recipe drafts and
        send changes yourself.
      </p>
      <ErrorBanner message={error || resource.error} />
      <Button type="submit" disabled={busy || subscriptionBusy || loadingModels}>
        {busy ? 'Saving…' : 'Save assistant settings'}
      </Button>
    </form>
  )
}
