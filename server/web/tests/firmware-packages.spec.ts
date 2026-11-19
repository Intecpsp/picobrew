import { test, expect } from '@playwright/test'
import type { Device, FirmwarePackage, FirmwareSettings } from '../src/types'

for (const viewport of [
  { name: 'desktop', width: 1280, height: 900 },
  { name: 'mobile', width: 390, height: 844 },
]) {
  test(`firmware upload, GitHub source, and machine readiness work on ${viewport.name}`, async ({
    page,
  }) => {
    await page.setViewportSize(viewport)
    const errors: string[] = []
    page.on('pageerror', (error) => errors.push(error.message))
    let source: FirmwareSettings = {
      repository: 'jeffreydwalter/picobrew-pico-pro-firmware',
      ref: '',
    }
    const commit = 'a'.repeat(40)
    const candidate = {
      model: 'pico-s',
      model_family: 'pico-s-pro',
      firmware_version: '1.1.0',
      binary_sha256: 'b'.repeat(64),
      binary_bytes: 180000,
    }
    const githubPackage: FirmwarePackage = {
      ...candidate,
      id: 'c'.repeat(64),
      package_bytes: 210000,
      created_at: '2026-11-19T12:00:00Z',
      validation: 'passed',
      provenance: { source: 'github', repository: 'brew-owner/machine-firmware', commit },
    }
    const uploadedPackage: FirmwarePackage = {
      ...githubPackage,
      id: 'd'.repeat(64),
      model: 'pico-pro',
      binary_sha256: 'e'.repeat(64),
      provenance: { source: 'upload' },
    }
    let packages: FirmwarePackage[] = []
    let githubChecks = 0
    let uploads = 0
    const imports: unknown[] = []
    const machine = {
      id: 'firmware-pico-s',
      name: 'Firmware test Pico S',
      model: 'pico-s',
      model_source: 'firmware',
      adapter: 'pico-standalone',
      enabled: true,
      online: false,
      capabilities: null,
      base_url: 'http://192.168.1.42',
      location: 'Kitchen',
      key_configured: true,
      created_at: '',
      last_seen: null,
      status: null,
    } as Device
    await page.route('**/api/v1/events', (route) =>
      route.fulfill({ contentType: 'text/event-stream', body: ': firmware fixture\n\n' })
    )
    await page.route('**/api/v1/firmware/**', async (route) => {
      const request = route.request()
      const path = new URL(request.url()).pathname
      let result: unknown
      if (path.endsWith('/settings')) {
        if (request.method() === 'PUT') source = request.postDataJSON()
        result = source
      } else if (path.endsWith('/github/import')) {
        imports.push(request.postDataJSON())
        packages = [githubPackage]
        result = githubPackage
      } else if (path.endsWith('/github')) {
        githubChecks++
        result = { ...source, commit, packages: [candidate] }
      } else if (path.endsWith('/download')) {
        await route.fulfill({
          contentType: 'application/zip',
          headers: { 'Content-Disposition': 'attachment; filename="firmware.zip"' },
          body: Buffer.from('downloaded fixture package'),
        })
        return
      } else if (request.method() === 'DELETE') {
        packages = packages.filter((item) => !path.endsWith(item.id))
        result = { deleted: true }
      } else if (path.endsWith('/packages')) {
        if (request.method() === 'POST') {
          expect(request.headers()['content-type']).toContain('multipart/form-data; boundary=')
          expect(request.headers()['x-csrf-token']).toBeTruthy()
          expect(request.postDataBuffer()?.toString()).toContain(
            'name="file"; filename="pico-pro.zip"'
          )
          uploads++
          packages = [...packages, uploadedPackage]
          result = uploadedPackage
        } else result = packages
      }
      await route.fulfill({ json: result })
    })
    await page.route('**/api/v1/devices**', async (route) => {
      const path = new URL(route.request().url()).pathname
      const result =
        path === '/api/v1/devices'
          ? [machine]
          : path.endsWith('/firmware')
            ? {
                device_id: machine.id,
                model: 'pico-s',
                current_version: '1.0.0',
                install_supported: false,
                reason:
                  'Network installation is not available yet. It requires an initial USB recovery setup that is still being developed.',
                packages: packages.filter((item) => item.model === 'pico-s'),
              }
            : path.endsWith('/ai-monitor')
              ? { enabled: false, state: 'off', configured: false }
              : null
      await route.fulfill({ json: result })
    })

    await page.goto('/#/settings/firmware')
    const firmware = page.getByRole('region', { name: 'Firmware', exact: true })
    await expect(firmware.getByRole('button', { name: 'Check GitHub' })).toBeEnabled()
    expect(githubChecks).toBe(0)
    await expect(firmware.getByLabel('GitHub repository')).toHaveValue(source.repository)
    await firmware.getByLabel('GitHub repository').fill('brew-owner/machine-firmware')
    await firmware.getByLabel('Branch or tag').fill('stable')
    await firmware.getByRole('button', { name: 'Save source' }).click()
    await expect(firmware.getByRole('button', { name: 'Check GitHub' })).toBeEnabled()
    expect(source).toEqual({ repository: 'brew-owner/machine-firmware', ref: 'stable' })
    expect(githubChecks).toBe(0)
    await firmware.getByRole('button', { name: 'Check GitHub' }).click()
    const github = firmware.getByRole('region', { name: 'Packages on GitHub' })
    await expect(github.getByText(/Pico S/)).toBeVisible()
    await github.getByRole('button', { name: 'Fetch to Hub' }).click()
    const stored = firmware.getByRole('region', { name: 'Packages in this Hub' })
    await expect(stored.getByRole('article', { name: 'Pico S 1.1.0' })).toBeVisible()
    expect(imports).toEqual([
      { repository: 'brew-owner/machine-firmware', commit, model: 'pico-s' },
    ])
    await firmware.getByLabel('Upload firmware package (.zip)').setInputFiles({
      name: 'pico-pro.zip',
      mimeType: 'application/zip',
      buffer: Buffer.from('complete fixture ZIP'),
    })
    await firmware.getByRole('button', { name: 'Upload package', exact: true }).click()
    const uploaded = stored.getByRole('article', { name: 'Pico Pro 1.1.0' })
    await expect(uploaded.getByText('Uploaded package')).toBeVisible()
    expect(uploads).toBe(1)
    const fetched = stored.getByRole('article', { name: 'Pico S 1.1.0' })
    await fetched.getByText('Package details', { exact: true }).click()
    await expect(fetched.getByText(candidate.binary_sha256, { exact: true })).toBeVisible()
    // Success notifications dismiss after six seconds; keep the package details unobstructed.
    await expect(
      page.getByRole('status').filter({
        has: page.getByRole('button', { name: 'Dismiss notification', exact: true }),
      })
    ).toHaveCount(0, { timeout: 10000 })
    await firmware.screenshot({
      path: test.info().outputPath(`firmware-settings-${viewport.name}.png`),
    })
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1)).toBe(
      true
    )
    const downloaded = page.waitForEvent('download')
    await fetched.getByRole('button', { name: 'Download package' }).click()
    expect((await downloaded).suggestedFilename()).toBe('pico-s-1.1.0.zip')
    await uploaded.getByRole('button', { name: 'Remove', exact: true }).click()
    await page
      .getByRole('dialog', { name: 'Remove Pico Pro 1.1.0?' })
      .getByRole('button', { name: 'Remove package' })
      .click()
    await expect(uploaded).toHaveCount(0)
    await page.reload()
    await expect(firmware.getByLabel('GitHub repository')).toHaveValue(
      'brew-owner/machine-firmware'
    )
    await expect(firmware.getByLabel('Branch or tag')).toHaveValue('stable')
    expect(githubChecks).toBe(1)

    await page.goto(`/#/devices/${machine.id}`)
    await page.getByRole('button', { name: /^Firmware / }).click()
    const dialog = page.getByRole('dialog', { name: 'Machine firmware' })
    await expect(dialog.getByText('Current version: 1.0.0')).toBeVisible()
    await expect(dialog.getByRole('status')).toHaveText(
      'Network installation is not available yet. It requires an initial USB recovery setup that is still being developed.'
    )
    await expect(dialog.getByRole('article', { name: 'Pico S 1.1.0' })).toBeVisible()
    await expect(dialog.getByRole('article', { name: 'Pico Pro 1.1.0' })).toHaveCount(0)
    await expect(dialog.getByRole('button', { name: /^Install/ })).toHaveCount(0)
    await dialog.screenshot({
      path: test.info().outputPath(`firmware-machine-${viewport.name}.png`),
    })
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1)).toBe(
      true
    )
    await dialog.getByRole('button', { name: 'Manage firmware packages' }).click()
    await expect(page).toHaveURL(/#\/settings\/firmware$/)
    expect(errors).toEqual([])
  })
}
