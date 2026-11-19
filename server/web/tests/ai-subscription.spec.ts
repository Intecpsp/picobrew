import { test, expect } from '@playwright/test'

for (const viewport of [
  { name: 'desktop', width: 1280, height: 900 },
  { name: 'mobile', width: 390, height: 844 },
]) {
  test(`subscription setup and billing choices work on ${viewport.name}`, async ({
    page,
    request,
  }) => {
    expect((await (await request.get('/api/v1/auth/session')).json()).demo).toBe(true)
    await page.setViewportSize(viewport)
    const pageErrors: string[] = []
    page.on('pageerror', (error) => pageErrors.push(error.message))
    let pending = false
    let authenticated = false
    let saved = {
      provider: 'openai',
      auth_mode: 'api_key',
      model: '',
      enabled: false,
      key_configured: false,
    }
    const modelRequests: string[] = []
    const callbackAddress = 'http://127.0.0.1:1455/auth/callback'
    const callbackURL = `${callbackAddress}?code=browser-test-code&state=browser-test-state`
    const status = () => ({
      provider: 'openai',
      installed: true,
      authenticated,
      pending,
      callback_url: pending ? callbackAddress : undefined,
      message: authenticated ? 'Connected to ChatGPT.' : 'Finish your ChatGPT sign-in.',
      login_command: '',
    })
    await page.route('**/api/v1/ai/**', async (route) => {
      const incoming = route.request()
      const url = new URL(incoming.url())
      let result: unknown
      if (url.pathname.endsWith('/ai/settings')) {
        if (incoming.method() === 'PUT') {
          const body = incoming.postDataJSON()
          expect(body.auth_mode).toBe('subscription')
          expect(body.api_key).toBeUndefined()
          saved = { ...saved, ...body }
        }
        result = saved
      } else if (url.pathname.endsWith('/ai/subscription/login')) {
        expect(incoming.postDataJSON()).toEqual({
          provider: 'openai',
          redirect_uri: callbackAddress,
        })
        pending = true
        result = {
          ...status(),
          authorization_url: 'https://auth.openai.com/oauth/authorize?state=browser-test-state',
        }
      } else if (url.pathname.endsWith('/ai/subscription/callback')) {
        expect(incoming.postDataJSON()).toEqual({ provider: 'openai', callback_url: callbackURL })
        authenticated = true
        pending = false
        result = status()
      } else if (url.pathname.endsWith('/ai/subscription')) {
        result =
          url.searchParams.get('provider') === 'anthropic'
            ? {
                provider: 'anthropic',
                installed: true,
                authenticated: true,
                message: 'Connected through the official Claude Code app.',
                login_command: 'claude auth login',
              }
            : status()
      } else if (url.pathname.endsWith('/ai/models')) {
        modelRequests.push(url.search)
        const provider = url.searchParams.get('provider') || 'openai'
        const mode = url.searchParams.get('auth_mode') || 'api_key'
        result = {
          provider,
          auth_mode: mode,
          models:
            provider === 'anthropic'
              ? [
                  { id: 'default', name: 'Account default' },
                  { id: 'sonnet', name: 'Sonnet alias' },
                ]
              : [{ id: 'subscription-model', name: 'Subscription model' }],
        }
      } else {
        await route.continue()
        return
      }
      await route.fulfill({ json: result })
    })

    await page.goto('/#/settings')
    await page.getByLabel('Connection method').selectOption('subscription')
    await expect(page.getByLabel('Provider API key')).toHaveCount(0)
    await page.getByRole('button', { name: 'Sign in with ChatGPT', exact: true }).click()
    await expect(page.getByRole('link', { name: 'Open ChatGPT sign-in' })).toHaveAttribute(
      'href',
      /^https:\/\/auth\.openai\.com\//
    )
    await expect(page.getByText(callbackAddress, { exact: true })).toBeVisible()
    await page.getByLabel('Completed sign-in URL').fill(callbackURL)
    await page.screenshot({
      path: test.info().outputPath(`chatgpt-sign-in-${viewport.name}.png`),
      fullPage: true,
    })
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1)).toBe(
      true
    )

    // Refresh before saving the connection method. Only the nonsecret setup choice is retained in the browser.
    await page.reload()
    await expect(page.getByLabel('Connection method')).toHaveValue('subscription')
    await expect(page.getByText(callbackAddress, { exact: true })).toBeVisible()
    await page.getByLabel('Completed sign-in URL').fill(callbackURL)
    await page.getByRole('button', { name: 'Finish ChatGPT sign-in' }).click()
    await expect(
      page.getByRole('option', { name: 'Subscription model (subscription-model)' })
    ).toBeAttached()
    await page.getByLabel('Model', { exact: true }).selectOption('subscription-model')
    await page.getByLabel('Enable the brew assistant').check()
    await page.getByRole('button', { name: 'Save assistant settings' }).click()
    await expect(page.getByText('Brew assistant settings saved.', { exact: true })).toBeVisible()
    expect(saved).toMatchObject({
      provider: 'openai',
      auth_mode: 'subscription',
      enabled: true,
      model: 'subscription-model',
    })
    expect(modelRequests.every((query) => query.includes('auth_mode=subscription'))).toBe(true)

    await page.getByLabel('Provider', { exact: true }).selectOption('anthropic')
    await expect(page.getByText('claude auth login', { exact: true })).toBeVisible()
    await expect(page.getByRole('option', { name: 'Account default (default)' })).toBeAttached()
    await expect(page.getByText(/model availability depends on your plan/)).toBeVisible()
    await page.screenshot({
      path: test.info().outputPath(`claude-subscription-${viewport.name}.png`),
      fullPage: true,
    })
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1)).toBe(
      true
    )
    await page.getByLabel('Connection method').selectOption('api_key')
    await expect(page.getByLabel('Provider API key')).toBeVisible()
    await expect(page.getByText(/Uses provider API billing/)).toBeVisible()
    expect(pageErrors).toEqual([])
  })
}
