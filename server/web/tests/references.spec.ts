import { expect, test, type Locator } from '@playwright/test'
import { samplePDF } from './pdf-fixture'

test.beforeEach(async ({ request }) => {
  expect((await (await request.get('/api/v1/auth/session')).json()).demo).toBe(true)
})

function guideRow(dialog: Locator, title: string) {
  return dialog.locator('.reference-list article').filter({ hasText: title })
}

test('bundled Markdown guides stay in one accessible reader and follow local images and guides', async ({
  page,
  context,
}) => {
  const errors: string[] = []
  page.on('pageerror', (error) => errors.push(error.message))
  await page.goto('/#/catalog?model=zymatic')
  const opener = page.getByRole('button', { name: 'Machine guides & artwork', exact: true })
  await opener.click()
  let dialog = page.getByRole('dialog')
  const artwork = dialog.locator('.reference-artwork-button').first()
  await expect(artwork).toHaveCSS('background-color', 'rgb(38, 60, 52)')
  await artwork.click()
  await expect(dialog.locator('.reference-artwork-preview')).toHaveCSS(
    'background-color',
    'rgb(38, 60, 52)'
  )
  await expect(dialog.locator('.reference-artwork-preview img')).toBeVisible()
  await page.screenshot({
    path: test.info().outputPath('guide-artwork-preview-desktop.png'),
    fullPage: true,
  })
  await dialog.getByRole('button', { name: 'Back to guides', exact: true }).click()
  await guideRow(dialog, 'Zymatic: Firmware Installation')
    .getByRole('button', { name: 'Open guide' })
    .click()
  await expect(
    dialog.getByRole('heading', { level: 1, name: /Zymatic.*Firmware Installation/ })
  ).toBeVisible()
  const image = dialog.getByRole('img', { name: 'FLASH Now!', exact: true })
  await image.scrollIntoViewIfNeeded()
  await expect(image).toBeVisible()
  await expect
    .poll(() => image.evaluate((node: HTMLImageElement) => node.naturalWidth))
    .toBeGreaterThan(0)
  expect(await image.getAttribute('src')).toContain(
    '/api/v1/catalog/assets/support/legacy/common/flashButton.png'
  )
  await dialog.locator('a[href$="/Firmware.md"]').click()
  await expect(dialog.getByRole('heading', { level: 1, name: /Zymatic.*Firmware/ })).toBeVisible()
  expect(context.pages()).toHaveLength(1)
  expect(new URLSearchParams(page.url().split('?')[1]).get('model')).toBe('zymatic')
  await expect(page.getByRole('dialog')).toHaveCount(1)
  await dialog.getByRole('button', { name: 'Back to previous guide' }).click()
  await expect(dialog.getByRole('heading', { name: 'Installation Instructions' })).toBeVisible()
  await dialog.evaluate((node) => {
    node.scrollTop = 0
  })
  await page.screenshot({ path: test.info().outputPath('guide-modal-desktop.png'), fullPage: true })

  await page.setViewportSize({ width: 390, height: 844 })
  await dialog.evaluate((node) => {
    node.scrollTop = 0
  })
  expect(await dialog.evaluate((node) => node.scrollWidth <= node.clientWidth + 1)).toBe(true)
  await page.screenshot({ path: test.info().outputPath('guide-modal-mobile.png'), fullPage: true })
  await dialog.getByRole('button', { name: 'Close dialog' }).focus()
  for (let index = 0; index < 6; index++) {
    await page.keyboard.press('Tab')
    expect(await page.evaluate(() => Boolean(document.activeElement?.closest('dialog')))).toBe(true)
  }
  await page.keyboard.press('Escape')
  await expect(page.getByRole('dialog')).toHaveCount(0)
  await expect(opener).toBeFocused()
  expect(errors).toEqual([])
})

test('optional PDFs show source options and installed PDFs have an inline preview with a download fallback', async ({
  page,
}) => {
  const browserErrors: string[] = []
  const resourceRequests: string[] = []
  page.on('pageerror', (error) => browserErrors.push(error.message))
  page.on('console', (message) => {
    if (message.type() === 'error' && /Content Security Policy|Refused to/i.test(message.text()))
      browserErrors.push(message.text())
  })
  page.on('request', (request) => resourceRequests.push(request.url()))
  await page.setViewportSize({ width: 390, height: 844 })
  await page.goto('/#/catalog?model=zymatic')
  await page.getByRole('button', { name: 'Machine guides & artwork', exact: true }).click()
  let dialog = page.getByRole('dialog')
  await guideRow(dialog, 'Zymatic: Zymatic Manual')
    .getByRole('button', { name: 'View options' })
    .click()
  await expect(dialog.getByText('This guide isn’t installed on this Hub.')).toBeVisible()
  await expect(dialog.locator('iframe')).toHaveCount(0)
  await expect(dialog.getByRole('link', { name: 'Open archived PDF' })).toHaveAttribute(
    'href',
    /raw\.githubusercontent\.com\/.*\/Zymatic_Manual\.pdf$/
  )
  await page.screenshot({
    path: test.info().outputPath('guide-optional-pdf-mobile.png'),
    fullPage: true,
  })
  await page.keyboard.press('Escape')

  await page.route('**/api/v1/catalog/references', async (route) => {
    const response = await route.fetch()
    const data = await response.json()
    data.items.find((item: { asset_path: string }) =>
      item.asset_path.endsWith('/Zymatic_Manual.pdf')
    ).available = true
    await route.fulfill({ response, json: data })
  })
  await page.route('**/api/v1/catalog/assets/support/legacy/zymatic/Zymatic_Manual.pdf', (route) =>
    route.fulfill({
      contentType: 'application/pdf',
      body: samplePDF(),
    })
  )
  await page.getByRole('button', { name: 'Machine guides & artwork', exact: true }).click()
  dialog = page.getByRole('dialog')
  await guideRow(dialog, 'Zymatic: Zymatic Manual')
    .getByRole('button', { name: 'Open guide' })
    .click()
  const canvas = dialog.getByRole('img', { name: 'Zymatic: Zymatic Manual, page 1' })
  await expect(canvas).toHaveAttribute('data-rendered-page', '1')
  await expect.poll(() => countPDFInk(canvas)).toBeGreaterThan(50)
  await expect(dialog.getByText('Page 1 of 2', { exact: true })).toBeVisible()
  await dialog.getByText('Page text', { exact: true }).click()
  await expect(dialog.getByText('Guide preview', { exact: true })).toBeVisible()
  await dialog.getByText('Page text', { exact: true }).click()
  await expect(dialog.locator('iframe, object, embed')).toHaveCount(0)
  await expect(
    dialog.getByRole('button', { name: 'Download original', exact: true }).first()
  ).toBeVisible()
  await expect(dialog.getByRole('link', { name: 'Open archived PDF' })).toBeVisible()
  expect(await dialog.evaluate((node) => node.scrollWidth <= node.clientWidth + 1)).toBe(true)
  await page.screenshot({
    path: test.info().outputPath('guide-installed-pdf-mobile.png'),
    fullPage: true,
  })
  await dialog.getByRole('button', { name: 'Next', exact: true }).click()
  const secondPage = dialog.getByRole('img', { name: 'Zymatic: Zymatic Manual, page 2' })
  await expect(secondPage).toHaveAttribute('data-rendered-page', '2')
  await expect.poll(() => countPDFInk(secondPage)).toBeGreaterThan(50)
  await expect(dialog.getByRole('button', { name: 'Next', exact: true })).toBeDisabled()
  await dialog.getByText('Page text', { exact: true }).click()
  await expect(dialog.getByText('Second page instructions', { exact: true })).toBeVisible()
  await dialog.getByRole('button', { name: 'Previous', exact: true }).click()
  await expect(canvas).toHaveAttribute('data-rendered-page', '1')
  expect(
    resourceRequests.filter((url) =>
      url.endsWith('/assets/support/legacy/zymatic/Zymatic_Manual.pdf')
    )
  ).toHaveLength(1)
  expect(
    resourceRequests.some((url) => url.includes('/vendor/pdfjs-6.5.438/standard_fonts/'))
  ).toBe(true)
  expect(browserErrors).toEqual([])
  await page.keyboard.press('Escape')
})

async function countPDFInk(canvas: Locator) {
  return canvas.evaluate((element: HTMLCanvasElement) => {
    const pixels = element.getContext('2d')!.getImageData(0, 0, element.width, element.height).data
    let count = 0
    for (let i = 0; i < pixels.length; i += 4)
      if (pixels[i + 3] > 0 && pixels[i] + pixels[i + 1] + pixels[i + 2] < 700) count++
    return count
  })
}

test('recipe detail presents readable quantities and credits without losing catalog filters', async ({
  page,
  request,
}) => {
  await page.setViewportSize({ width: 390, height: 844 })
  await page.goto('/#/catalog?kind=archive&q=Alpine')
  const card = page
    .locator('.catalog-card')
    .filter({ has: page.getByRole('button', { name: /Alpine Delight/ }) })
    .first()
  await card.getByRole('button', { name: /Alpine Delight/ }).click()
  await expect(page.getByRole('heading', { level: 1, name: /Alpine Delight/ })).toBeVisible()
  const versions = page.getByRole('region', { name: 'Machine versions' })
  const pages = page.context().pages().length
  const opener = versions.getByRole('button', { name: 'View original recipe' })
  await page.route('https://picobrew.com/**', (route) => route.abort())
  await opener.click()
  const original = page.getByRole('dialog', { name: 'Original recipe' })
  await expect(original.getByRole('heading', { name: /Alpine Delight/ })).toBeVisible()
  await expect(original).toContainText('2.5 US gal')
  await expect(original.getByRole('heading', { name: 'Brew schedule' })).toBeVisible()
  expect(page.context().pages()).toHaveLength(pages)
  await page.keyboard.press('Escape')
  await expect(original).toHaveCount(0)
  await expect(opener).toBeFocused()
  await expect(versions.getByRole('option', { name: /Original/i })).toHaveCount(0)
  const table = versions.getByRole('table', { name: 'Scaled ingredient bill' })
  await expect(
    table
      .getByRole('cell', { name: 'Source starting water', exact: false })
      .or(table.getByRole('cell', { name: 'Scaled source water (reference)', exact: true }))
      .first()
  ).toBeVisible()
  expect(await table.innerText()).not.toMatch(/\d+\.\d{5,}/)
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1)).toBe(
    true
  )
  expect(
    await table
      .locator('td')
      .evaluateAll((cells) => cells.every((cell) => cell.scrollWidth <= cell.clientWidth + 1))
  ).toBe(true)
  await expect(page.getByText('Archive illustration', { exact: false })).toHaveCount(0)
  const credits = page.locator('.catalog-credits')
  await expect(credits).not.toHaveAttribute('open')
  await page.screenshot({
    path: test.info().outputPath('catalog-recipe-readable-mobile.png'),
    fullPage: true,
  })
  await credits.locator('summary').click()
  await expect(credits.getByText('Recipe creator', { exact: true })).toBeVisible()
  await expect(credits.getByText('Illustration credit', { exact: true })).toBeVisible()
  await expect(credits.getByRole('link', { name: 'View original artwork' })).toBeVisible()
  const id = page.url().split('/catalog/')[1].split('?')[0]
  const source = await (await request.get(`/api/v1/catalog/${id}`)).json()
  expect(source.recipe.batch_liters).toBe(9.46352946)
  await page.getByRole('button', { name: 'Back', exact: true }).click()
  await expect(page.getByPlaceholder('Search community recipes')).toHaveValue('Alpine')
  await expect(page.getByLabel('Archive record type')).toHaveValue('archive')
})

test('reviewed source recipes show the evidence and keep filters on return', async ({ page }) => {
  await page.goto('/#/catalog?kind=reviewed&q=Chill+Cycle&model=zymatic&source=public-recipes')
  await expect(page.getByRole('heading', { name: 'Source archive' })).toBeVisible()
  await expect(page.getByLabel('Archive record type')).toHaveValue('reviewed')
  await page.getByRole('button', { name: 'Chill Cycle', exact: true }).first().click()
  const review = page.getByRole('region', { name: 'Source review' })
  await expect(review).toContainText('circulates at 69°F')
  await expect(review).toContainText('chiller utility with placeholder beer ingredients')
  await page.getByRole('button', { name: 'Back', exact: true }).click()
  await expect(page.getByLabel('Archive record type')).toHaveValue('reviewed')
  await expect(page.getByLabel('Source machine')).toHaveValue('zymatic')
  await expect(page.getByLabel('Source archive', { exact: true })).toHaveValue('public-recipes')
  await expect(page.getByPlaceholder('Search community recipes')).toHaveValue('Chill Cycle')
})
