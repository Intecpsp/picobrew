import { expect, test, type Locator } from '@playwright/test'

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
  const frame = dialog.locator('iframe')
  await expect(frame).toBeVisible()
  await expect(frame).toHaveAttribute('title', 'Zymatic: Zymatic Manual')
  await expect(frame).toHaveAttribute('src', /^blob:/)
  await expect(frame).toHaveAttribute('sandbox', 'allow-scripts allow-same-origin')
  await expect(
    dialog.getByRole('button', { name: 'Download original', exact: true }).first()
  ).toBeVisible()
  await expect(dialog.getByText(/If your browser cannot display this PDF/)).toBeVisible()
  await page.screenshot({
    path: test.info().outputPath('guide-installed-pdf-mobile.png'),
    fullPage: true,
  })
  await page.keyboard.press('Escape')
})

test('recipe detail presents readable quantities and credits without losing catalogue filters', async ({
  page,
  request,
}) => {
  await page.setViewportSize({ width: 390, height: 844 })
  await page.goto('/#/catalog?kind=recipe&q=Alpine')
  const card = page
    .locator('.catalog-card')
    .filter({ has: page.getByRole('button', { name: /Alpine Delight/ }) })
    .first()
  await card.getByRole('button', { name: /Alpine Delight/ }).click()
  await expect(page.getByRole('heading', { level: 1, name: /Alpine Delight/ })).toBeVisible()
  await expect(page.locator('.catalog-recipe-meta')).toContainText('9.46 L')
  const table = page.getByRole('table', { name: 'Recipe ingredients' })
  await expect(table.getByRole('cell', { name: '13.97 L', exact: true })).toBeVisible()
  await expect(table.getByRole('cell', { name: '136.1 g', exact: true })).toBeVisible()
  await expect(page.locator('.catalog-schedule')).toContainText('65.6 °C')
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
  await expect(page.getByPlaceholder('Search community recipes and programs')).toHaveValue('Alpine')
  await expect(page.getByLabel('Catalogue type', { exact: true })).toHaveValue('recipe')
})

// A complete, deterministic one-page PDF for the installed-reference path. The
// ordinary demo keeps optional vendor manuals uninstalled.
function samplePDF(): Buffer {
  const stream = 'BT /F1 12 Tf 20 150 Td (Guide preview) Tj ET'
  const objects = [
    '<< /Type /Catalog /Pages 2 0 R >>',
    '<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
    '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>',
    `<< /Length ${stream.length} >>\nstream\n${stream}\nendstream`,
    '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
  ]
  let pdf = '%PDF-1.4\n'
  const offsets = [0]
  objects.forEach((object, i) => {
    offsets.push(Buffer.byteLength(pdf))
    pdf += `${i + 1} 0 obj\n${object}\nendobj\n`
  })
  const xref = Buffer.byteLength(pdf)
  pdf += `xref\n0 ${objects.length + 1}\n0000000000 65535 f \n`
  pdf += offsets
    .slice(1)
    .map((offset) => `${String(offset).padStart(10, '0')} 00000 n \n`)
    .join('')
  pdf += `trailer\n<< /Size ${objects.length + 1} /Root 1 0 R >>\nstartxref\n${xref}\n%%EOF\n`
  return Buffer.from(pdf)
}
