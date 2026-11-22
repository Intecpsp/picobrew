import { test, expect } from '@playwright/test'
import type { Device, FirmwarePackage, FirmwareSettings } from '../src/types'

for (const viewport of [
  { name: 'desktop', width: 1280, height: 900 },
  { name: 'mobile', width: 390, height: 844 },
]) {
  test(`firmware source, version selection, and on-demand downloads work on ${viewport.name}`, async ({
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
      firmware_version: '1.3.0',
      hub_ota_protocol: 1,
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
      } else if (path.endsWith('/updates')) {
        if (request.method() === 'POST') githubChecks++
        result = {
          releases: [{ ...candidate, archive_sha256: githubPackage.id, kind: 'standalone' }],
          notices: [],
        }
      } else if (path.endsWith('/stage')) {
        imports.push(path)
        packages = [githubPackage]
        result = githubPackage
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
                device_uid: 'pinned-pico-s',
                stock_restore_guide: true,
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
    await expect(firmware.getByLabel('GitHub repository')).toHaveValue(source.repository)
    await firmware.getByLabel('GitHub repository').fill('brew-owner/machine-firmware')
    await firmware.getByLabel('Branch or tag').fill('stable')
    await firmware.getByRole('button', { name: 'Save source' }).click()
    await expect.poll(() => source.ref).toBe('stable')
    await expect(firmware.getByText('Packages on GitHub')).toHaveCount(0)
    await page.goto(`/#/devices/${machine.id}`)
    await page.getByRole('button', { name: /^Firmware / }).click()
    const dialog = page.getByRole('dialog', { name: 'Machine firmware' })
    await expect(dialog.getByText('Current version: 1.0.0')).toBeVisible()
    await expect(dialog.getByRole('status')).toHaveText(
      'Network installation is not available yet. It requires an initial USB recovery setup that is still being developed.'
    )
    await expect(dialog.getByLabel('Firmware version')).toHaveValue(githubPackage.id)
    await dialog.getByRole('button', { name: 'Check for Updates' }).click()
    await expect.poll(() => githubChecks).toBe(1)
    expect(imports).toHaveLength(0)
    const download = page.waitForEvent('download')
    await dialog.getByRole('button', { name: 'Download', exact: true }).click()
    expect((await download).suggestedFilename()).toBe('pico-s-1.3.0.zip')
    expect(imports).toHaveLength(1)
    await expect(dialog.getByRole('button', { name: 'Install' })).toHaveCount(0)
    await dialog.screenshot({
      path: test.info().outputPath(`firmware-machine-${viewport.name}.png`),
    })
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1)).toBe(
      true
    )
    await dialog.getByRole('button', { name: 'Restore original firmware via USB' }).click()
    let restore = page.getByRole('dialog', { name: 'Restore original PicoBrew firmware?' })
    await expect(restore.getByRole('button', { name: 'Review USB requirements' })).toBeDisabled()
    await restore.getByRole('checkbox').check()
    await restore.getByRole('button', { name: 'Review USB requirements' }).click()
    restore = page.getByRole('dialog', { name: 'Confirm your USB setup' })
    await expect(
      restore.getByRole('button', { name: 'Show USB restore instructions' })
    ).toBeDisabled()
    await restore.getByRole('checkbox').check()
    await restore.getByRole('button', { name: 'Show USB restore instructions' }).click()
    restore = page.getByRole('dialog', { name: 'Restore using your computer' })
    await expect(restore.locator('pre')).toHaveText(
      './picobrew-hub usb-restore --data ./data --checkout ..'
    )
    expect(await restore.evaluate((d) => d.scrollWidth <= d.clientWidth + 1)).toBe(true)
    await restore.getByRole('button', { name: 'Back to machine firmware' }).click()
    await expect(dialog).toBeVisible()
    expect(errors).toEqual([])
  })
}
