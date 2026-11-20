import { expect, test } from '@playwright/test'
import type { BrewDetail, Capabilities, Device, DeviceStatus } from '../src/types'

for (const width of [1280, 390]) {
  for (const model of ['picoferm', 'picostill', 'z-series']) {
    test(`${model} capability controls and history at ${width}px`, async ({ page }) => {
      await page.setViewportSize({ width, height: 900 })
      const errors: string[] = [],
        reads: string[] = [],
        commands: Record<string, unknown>[] = []
      page.on('pageerror', (error) => errors.push(error.message))
      const brewing = model === 'z-series',
        ferm = model === 'picoferm'
      const name = ferm ? 'Cellar PicoFerm' : brewing ? 'Bench Pico Z' : 'Studio PicoStill'
      const caps: Capabilities = {
        model,
        model_family: model,
        api_version: 1,
        firmware_version: '1.2.0-fixture',
        commands: brewing
          ? [
              'encoder',
              'stop',
              'manual',
              'clean',
              'drain',
              'chill',
              'pause',
              'resume',
              'resume-interrupted',
              'discard-interrupted',
            ]
          : ferm
            ? ['start', 'stop', 'calibrate', 'sample', 'set-sampling-interval']
            : ['start', 'essential-oils', 'clean', 'stop', 'set-pump', 'set-fan'],
        command_fields: {
          calibrate: { allowed: ['confirm'], required: ['confirm'] },
          'resume-interrupted': { allowed: ['confirm'], required: ['confirm'] },
          'discard-interrupted': { allowed: [], required: [] },
          'set-pump': { allowed: ['enabled'], required: ['enabled'] },
          'set-fan': { allowed: ['channel', 'level', 'enabled'], required: ['channel', 'level'] },
          'set-sampling-interval': { allowed: ['value'], required: ['value'] },
        },
        settings_fields: brewing
          ? ['temperature_unit', 'language', 'detailed_status']
          : ['temperature_unit'],
        recipe_capacity: brewing ? 16 : 0,
        step_capacity: brewing ? 15 : 0,
        front_panel: brewing,
        history_persistent: true,
        telemetry_forwarding: true,
        dedicated_pause_resume: false,
        capabilities: {
          recipes: brewing,
          brewing,
          heating: brewing,
          front_panel: brewing,
          temperature: true,
          pressure: !brewing,
          battery: ferm,
          calibration: ferm,
          fermentation: ferm,
          pump: !ferm,
          fans: !ferm && !brewing,
        },
      }
      const status: DeviceStatus = {
        model,
        model_family: model,
        uptime_ms: 60000,
        device_uid: 'family-browser-fixture',
        ready: true,
        busy: false,
        stopped: false,
        state: 'idle',
        recovery: brewing
          ? {
              available: true,
              valid: true,
              recipe_name: 'Saved pale ale',
              step_name: '',
              current_step: -1,
              step_seconds: 0,
              elapsed_seconds: 125,
            }
          : null,
        command: 'idle',
        device_state: 0,
        phase: 4,
        controller_status: 0,
        compartment: 0,
        wort_f: brewing ? 72 : null,
        thermoblock_f: brewing ? 73 : null,
        target_f: brewing ? 0 : null,
        step_seconds: 0,
        current_step: -1,
        step_count: 0,
        recipe_name: '',
        step_name: '',
        session_id: '',
        filter_inserted: null,
        stop_reason: 'none',
        encoder: { position: 0, min: 0, max: 5 },
        temperature_f: 68.5,
        pressure_kpa: ferm ? null : 6.89,
        battery_v: ferm ? 3.8 : null,
        sensors: brewing
          ? []
          : ferm
            ? [
                {
                  id: 'temperature',
                  label: 'Temperature',
                  kind: 'temperature',
                  unit: 'F',
                  value: 68.5,
                },
                {
                  id: 'pressure_native',
                  label: 'Native pressure',
                  kind: 'pressure',
                  unit: 'raw',
                  value: 532.125,
                },
                { id: 'battery', label: 'Battery', kind: 'voltage', unit: 'V', value: 3.8 },
              ]
            : [
                { id: 'inlet', label: 'Inlet', kind: 'temperature', unit: 'F', value: 152 },
                { id: 'pot', label: 'Pot', kind: 'temperature', unit: 'F', value: 170 },
                { id: 'pressure', label: 'Pressure', kind: 'pressure', unit: 'kPa', value: 6.89 },
              ],
        actuators:
          !ferm && !brewing
            ? [
                { id: 'pump', label: 'Pump', kind: 'pump', enabled: false, level: null, rpm: null },
                { id: 'fan1', label: 'Fan 1', kind: 'fan', enabled: true, level: 25, rpm: 1200 },
                { id: 'fan2', label: 'Fan 2', kind: 'fan', enabled: false, level: 0, rpm: 0 },
              ]
            : [],
      }
      const device: Device = {
        id: 'family-device',
        name,
        model,
        model_source: 'firmware',
        adapter: 'pico-standalone',
        enabled: true,
        online: true,
        base_url: 'http://192.168.1.77',
        location: 'Cellar',
        key_configured: true,
        created_at: '',
        last_seen: null,
        status,
        capabilities: caps,
      }
      const history: BrewDetail = {
        id: 'family-device-1',
        device_id: device.id,
        device_name: name,
        model,
        recipe_name: '',
        state: 'completed',
        command: 'start',
        started_at: null,
        observed_at: '2026-11-20T00:00:00Z',
        duration_ms: 15000,
        device_history_id: 1,
        gap_count: 0,
        truncated: false,
        notes: '',
        rating: 0,
        sample_count: 2,
        source: 'device',
        sample_fields: [
          'elapsed_ms',
          'wort_f',
          'thermoblock_f',
          'target_f',
          'step_seconds',
          'phase',
          'controller_status',
          'compartment',
          'current_step',
          'flags',
          'device_state',
          'temperature_f',
          'pressure_kpa',
          'battery_v',
          'sensor_pressure_native',
        ],
        field_metadata: {
          sensor_pressure_native: { label: 'Native pressure', unit: 'raw', kind: 'pressure' },
        },
        samples: [0, 15000].map((elapsed, index) => ({
          elapsed_ms: elapsed,
          wort_f: null,
          thermoblock_f: null,
          target_f: null,
          step_seconds: 0,
          phase: 0,
          controller_status: 0,
          compartment: 0,
          current_step: 255,
          flags: 3,
          device_state: 2,
          source: 'device',
          temperature_f: 68.5 + index,
          pressure_kpa: null,
          battery_v: 3.8,
          readings: { sensor_pressure_native: 532.125 + index },
        })),
      }
      await page.route('**/api/v1/events', (route) =>
        route.fulfill({ contentType: 'text/event-stream', body: ': isolated family fixture\n\n' })
      )
      await page.route('**/api/v1/brews**', (route) =>
        route.fulfill({
          json: new URL(route.request().url()).pathname === '/api/v1/brews' ? [history] : history,
        })
      )
      await page.route('**/api/v1/devices**', async (route) => {
        const request = route.request(),
          path = new URL(request.url()).pathname
        reads.push(request.method() + ' ' + path)
        let result: unknown,
          code = 200
        if (path === '/api/v1/devices') result = [device]
        else if (path.endsWith('/display')) {
          expect(brewing).toBe(true)
          result = {
            width: 128,
            height: 64,
            rotation: 0,
            format: 'row-gray4-msb-hex',
            data: '0123456789abcdef'.repeat(512),
          }
        } else if (path.endsWith('/recipes')) {
          expect(brewing).toBe(true)
          result = []
        } else if (path.endsWith('/ai-monitor')) {
          expect(brewing).toBe(true)
          result = { configured: false, enabled: false, state: 'off' }
        } else if (path.endsWith('/settings'))
          result = brewing
            ? { temperature_unit: 'F', language: 'en', detailed_status: false }
            : { temperature_unit: 'F' }
        else if (path.endsWith('/telemetry'))
          result = { enabled: false, host: '', port: 8080, state: 'disabled' }
        else if (path.endsWith('/commands')) {
          expect(request.method()).toBe('POST')
          expect(request.headers()['idempotency-key']).toBeTruthy()
          expect(request.headers()['x-csrf-token']).toBeTruthy()
          const body = request.postDataJSON()
          commands.push(body)
          if (['resume-interrupted', 'discard-interrupted'].includes(body.command))
            Object.assign(status.recovery!, { available: false, valid: true })
          code = 202
          result = { id: 'receipt', state: 'accepted', command: body }
        } else if (path.endsWith('/sync-history')) result = { synced: true }
        else if (path.endsWith('/test') || path.endsWith('/family-device')) result = device
        else throw new Error('Unexpected device request ' + path)
        await route.fulfill({ status: code, json: result })
      })
      await page.goto('/#/devices/family-device')
      await expect(page.getByRole('heading', { name, exact: true })).toBeVisible()
      expect(commands).toEqual([])
      if (brewing) {
        const canvas = page.locator('canvas[aria-label="Live Pico OLED display"]')
        await expect(canvas).toBeVisible()
        await expect(
          page.getByRole('button', { name: 'Pause session', exact: true })
        ).toBeDisabled()
        await expect(
          page.getByRole('button', { name: 'Resume session', exact: true })
        ).toBeVisible()
        await expect(page.getByLabel('Other programs', { exact: true })).toContainText('Clean')
        await expect(page.getByLabel('Other programs', { exact: true })).toContainText('Chill')
        const pixels = await canvas.evaluate((node) =>
          Array.from((node as HTMLCanvasElement).getContext('2d')!.getImageData(0, 0, 16, 1).data)
        )
        expect(pixels.slice(0, 4)).toEqual([28, 43, 35, 255])
        expect(pixels.slice(-4)).toEqual([196, 219, 171, 255])
        await page.getByRole('button', { name: 'Press dial', exact: true }).click()
        await expect.poll(() => commands.length).toBe(1)
        expect(commands[0]).toEqual({ command: 'encoder', click: true })
        await expect(page.getByRole('button', { name: 'Open program', exact: true })).toBeDisabled()
        await expect(page.getByText('Review checkpoint', { exact: true })).toBeVisible()
        expect(
          await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1)
        ).toBe(true)
        await page.getByRole('button', { name: 'Resume interrupted brew', exact: true }).click()
        let dialog = page.getByRole('dialog', { name: 'Resume Saved pale ale?' })
        await expect(dialog).toContainText('water, vessel and filter')
        await expect(dialog).toContainText('30 seconds')
        await expect(dialog).toContainText('saved remaining step time')
        await dialog.getByRole('button', { name: 'Cancel', exact: true }).click()
        expect(commands).toHaveLength(1)
        await page.getByRole('button', { name: 'Resume interrupted brew', exact: true }).click()
        await page
          .getByRole('dialog')
          .getByRole('button', { name: 'Resume brew', exact: true })
          .click()
        await expect.poll(() => commands.length).toBe(2)
        expect(commands[1]).toEqual({ command: 'resume-interrupted', confirm: true })
        await expect(
          page.getByRole('button', { name: 'Resume interrupted brew', exact: true })
        ).toHaveCount(0)
        Object.assign(status.recovery!, { available: false, valid: false })
        await page.getByRole('button', { name: 'Test connection', exact: true }).click()
        await expect(
          page.getByText(
            'The saved checkpoint could not be verified. Discard it before starting another recipe.',
            { exact: true }
          )
        ).toBeVisible()
        await expect(page.getByRole('button', { name: 'Open program', exact: true })).toBeDisabled()
        await page.getByRole('button', { name: 'Discard checkpoint', exact: true }).click()
        dialog = page.getByRole('dialog', { name: 'Discard the interrupted checkpoint?' })
        await dialog.getByRole('button', { name: 'Cancel', exact: true }).click()
        expect(commands).toHaveLength(2)
        await page.getByRole('button', { name: 'Discard checkpoint', exact: true }).click()
        await page
          .getByRole('dialog')
          .getByRole('button', { name: 'Discard checkpoint', exact: true })
          .click()
        await expect.poll(() => commands.length).toBe(3)
        expect(commands[2]).toEqual({ command: 'discard-interrupted', confirm: true })
        await expect(
          page.getByRole('button', { name: 'Discard checkpoint', exact: true })
        ).toHaveCount(0)
      } else {
        await expect(page.getByRole('button', { name: 'Press dial', exact: true })).toHaveCount(0)
        await expect(page.getByText('Thermoblock', { exact: true })).toHaveCount(0)
        await expect(page.getByRole('button', { name: 'Start recipe', exact: true })).toHaveCount(0)
        if (ferm) {
          await page.getByRole('button', { name: 'Calibrate pressure', exact: true }).click()
          let dialog = page.getByRole('dialog', { name: 'Calibrate pressure?' })
          await expect(dialog).toContainText('GRAY seal')
          await expect(dialog).toContainText('depressurized')
          await dialog.getByRole('button', { name: 'Cancel', exact: true }).click()
          expect(commands).toEqual([])
          await page.getByRole('button', { name: 'Calibrate pressure', exact: true }).click()
          dialog = page.getByRole('dialog', { name: 'Calibrate pressure?' })
          await dialog
            .getByRole('button', { name: 'Depressurized — calibrate', exact: true })
            .click()
          await expect(dialog).not.toBeVisible()
          expect(commands[0]).toEqual({ command: 'calibrate', confirm: true })
          await page.getByLabel('Sampling interval (seconds)', { exact: true }).fill('900')
          await page.getByRole('button', { name: 'Set sampling interval', exact: true }).click()
          await page
            .getByRole('dialog')
            .getByRole('button', { name: 'Set sampling interval', exact: true })
            .click()
          await expect.poll(() => commands.length).toBe(2)
          expect(commands[1]).toEqual({
            command: 'set-sampling-interval',
            value: 900,
            confirm: true,
          })
        } else {
          await page.getByRole('button', { name: 'Set pump', exact: true }).click()
          await page
            .getByRole('dialog')
            .getByRole('button', { name: 'Set pump', exact: true })
            .click()
          await expect.poll(() => commands.length).toBe(1)
          expect(commands[0]).toEqual({ command: 'set-pump', enabled: false, confirm: true })
          await page.getByLabel('Fan', { exact: true }).selectOption('2')
          await page.getByLabel('Output level (%)', { exact: true }).fill('35')
          await page.getByLabel('Fan enabled', { exact: true }).check()
          await page.getByRole('button', { name: 'Set fan', exact: true }).click()
          await page
            .getByRole('dialog')
            .getByRole('button', { name: 'Set fan', exact: true })
            .click()
          await expect.poll(() => commands.length).toBe(2)
          expect(commands[1]).toEqual({
            command: 'set-fan',
            channel: 2,
            level: 35,
            enabled: true,
            confirm: true,
          })
        }
        expect(reads.some((path) => /\/(display|recipes|ai-monitor)$/.test(path))).toBe(false)
      }
      await page.screenshot({
        path: test.info().outputPath(`${model}-${width}.png`),
        fullPage: true,
      })
      expect(
        await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1)
      ).toBe(true)
      if (!brewing) {
        await page.getByRole('button', { name: 'Preferences', exact: true }).click()
        const dialog = page.getByRole('dialog', { name: 'Device preferences' })
        await expect(dialog.getByLabel('Temperature unit', { exact: true })).toBeVisible()
        await expect(dialog.getByLabel('Front-panel language', { exact: true })).toHaveCount(0)
        await expect(dialog.getByLabel('Show detailed status', { exact: true })).toHaveCount(0)
        await dialog.getByRole('button', { name: 'Close dialog' }).click()
      }
      await page.goto('/#/history/family-device-1')
      await page.getByLabel('Recorded measurements', { exact: true }).selectOption('raw')
      await expect(page.getByRole('img', { name: 'Recorded Native pressure in raw' })).toBeVisible()
      await page.getByRole('button', { name: 'Show raw samples' }).click()
      await expect(page.getByRole('columnheader', { name: 'Native pressure (raw)' })).toBeVisible()
      expect(errors).toEqual([])
    })
  }
}
