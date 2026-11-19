import { test, expect } from '@playwright/test'
import type { Device, Telemetry } from '../src/types'

for (const viewport of [
  { name: 'desktop', width: 1280, height: 900 },
  { name: 'mobile', width: 390, height: 844 },
]) {
  test(`automatic machine identity and advanced connection work on ${viewport.name}`, async ({
    page,
  }) => {
    await page.setViewportSize(viewport)
    const errors: string[] = []
    page.on('pageerror', (error) => errors.push(error.message))
    let device: Device | null = null
    let receiver: Telemetry = { mode: 'automatic', enabled: true, host: 'hub.local', port: 8080 }
    const overrides: unknown[] = []
    let registrations = 0
    await page.route('**/api/v1/auth/session', async (route) => {
      const response = await route.fetch()
      await route.fulfill({ json: { ...(await response.json()), demo: false } })
    })
    // Isolate these physical-machine response fixtures from the real simulator's fleet events.
    await page.route('**/api/v1/events', (route) =>
      route.fulfill({
        contentType: 'text/event-stream',
        body: ': isolated machine fixture\n\n',
      })
    )
    await page.route('**/api/v1/devices**', async (route) => {
      const request = route.request()
      const path = new URL(request.url()).pathname
      let result: unknown = null
      if (path === '/api/v1/devices') {
        if (request.method() === 'POST') {
          const body = request.postDataJSON()
          expect(body.adapter).toBe('pico-standalone')
          expect(body.model).toBe('')
          registrations++
          device = {
            ...body,
            id: 'browser-pico-s',
            model: 'pico-s',
            model_source: 'firmware',
            key_configured: true,
            online: true,
            created_at: '',
            last_seen: null,
            status: null,
            capabilities: {
              api_version: 1,
              firmware_version: '1.1.0-test',
              commands: [],
              recipe_capacity: 32,
              step_capacity: 15,
              front_panel: true,
              history_persistent: true,
              telemetry_forwarding: true,
              dedicated_pause_resume: false,
            },
            telemetry: { ...receiver, state: 'configured' },
          }
          result = device
        } else result = device ? [device] : []
      } else if (path.endsWith('/telemetry')) {
        if (request.method() === 'PUT') {
          const body = request.postDataJSON()
          overrides.push(body)
          receiver =
            body.mode === 'automatic'
              ? { mode: 'automatic', enabled: true, host: 'hub.local', port: 8080 }
              : body
          if (device) device.telemetry = { ...receiver, state: 'configured' }
        }
        result = receiver
      } else if (path.endsWith('/settings')) {
        result = { temperature_unit: 'F', language: 'en', detailed_status: true }
      } else if (path.endsWith('/recipes')) result = []
      else if (path.endsWith('/ai-monitor'))
        result = { enabled: false, state: 'off', configured: false }
      else if (path.endsWith('/test') || path.endsWith('/browser-pico-s')) result = device
      await route.fulfill({ json: result })
    })

    await page.goto('/#/overview')
    await page.getByRole('button', { name: 'Add machine', exact: true }).click()
    let dialog = page.getByRole('dialog', { name: 'Add machine', exact: true })
    await expect(dialog.getByLabel('Machine model')).toHaveValue('Detect automatically')
    await expect(dialog.getByRole('combobox', { name: 'Machine model' })).toHaveCount(0)
    await dialog.getByLabel('Machine name').fill('Automatically connected Pico S')
    await dialog.getByLabel('Machine URL').fill('http://192.168.1.42')
    await dialog.getByLabel('Machine API key').fill('isolated-browser-key')
    await dialog.getByRole('button', { name: 'Add machine', exact: true }).click()
    await expect(dialog).not.toBeVisible()
    const card = page
      .locator('.machine-card')
      .filter({ has: page.getByRole('heading', { name: 'Automatically connected Pico S' }) })
    await expect(card.getByText('Pico S', { exact: true })).toBeVisible()
    expect(overrides).toHaveLength(0)
    expect(registrations).toBe(1)
    await card.getByRole('link', { name: 'Open Automatically connected Pico S controls' }).click()
    await page.getByRole('button', { name: 'Brewer preferences' }).click()
    dialog = page.getByRole('dialog', { name: 'Brewer preferences' })
    await expect(dialog.getByText('Live readings configured', { exact: true })).toBeVisible()
    await expect(dialog.getByLabel('Receiver host')).toHaveCount(0)
    await dialog.getByText('Advanced Hub connection', { exact: true }).click()
    await expect(dialog.getByLabel('Connection management')).toHaveValue('automatic')
    await dialog.getByLabel('Connection management').selectOption('manual')
    await dialog.getByLabel('Receiver host').fill('brew-relay.local')
    await dialog.getByLabel('Receiver port').fill('49152')
    await dialog.getByRole('button', { name: 'Save manual connection' }).click()
    await expect(dialog.getByText('Manual receiver configured', { exact: true })).toBeVisible()
    expect(overrides).toEqual([
      { mode: 'manual', enabled: true, host: 'brew-relay.local', port: 49152 },
    ])
    await page.screenshot({
      path: test.info().outputPath(`machine-connection-${viewport.name}.png`),
      fullPage: true,
    })
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1)).toBe(
      true
    )
    await dialog.getByRole('button', { name: 'Close dialog' }).click()
    await page.getByRole('button', { name: 'Test connection' }).click()
    await page.getByRole('button', { name: 'Brewer preferences' }).click()
    dialog = page.getByRole('dialog', { name: 'Brewer preferences' })
    await dialog.getByText('Advanced Hub connection', { exact: true }).click()
    await expect(dialog.getByLabel('Connection management')).toHaveValue('manual')
    await expect(dialog.getByLabel('Receiver host')).toHaveValue('brew-relay.local')
    expect(overrides).toHaveLength(1)
    await dialog.getByLabel('Connection management').selectOption('automatic')
    await dialog.getByRole('button', { name: 'Return to automatic' }).click()
    await expect(dialog.getByText('Live readings configured', { exact: true })).toBeVisible()
    expect(overrides[1]).toEqual({ mode: 'automatic' })
    expect(errors).toEqual([])
    expect(registrations).toBe(1)
  })
}
