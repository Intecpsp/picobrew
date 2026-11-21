import { test, expect } from '@playwright/test'

test.beforeEach(async ({ request }) => {
  expect((await (await request.get('/api/v1/auth/session')).json()).demo).toBe(true)
})

test('recipe browsing, filtered details and AI creation share one Recipes workspace', async ({
  page,
}) => {
  await page.goto('/#/recipes')
  const main = page.getByRole('navigation', { name: 'Main navigation' })
  const views = page.getByRole('navigation', { name: 'Recipe views' })
  await expect(main.getByRole('link', { name: /^Recipes/ })).toHaveAttribute('aria-current', 'page')
  await expect(main.getByRole('link', { name: /Community catalog|Brew assistant/ })).toHaveCount(0)
  await expect(views.getByRole('link')).toHaveCount(2)
  await expect(page.getByRole('button', { name: /community catalog/i })).toHaveCount(0)
  await views.getByRole('link', { name: 'Browse all' }).click()
  await page.getByPlaceholder('Search community recipes and programs').fill('Pico Pale')
  await page.getByLabel('Catalog type').selectOption('native-program')
  await expect(page).toHaveURL(/q=Pico\+Pale/)
  await page.getByRole('button', { name: 'Pico Pale', exact: true }).first().click()
  await expect(
    page.getByRole('heading', { name: 'Pico Pale', exact: true, level: 1 })
  ).toBeVisible()
  await expect(page).toHaveURL(/catalog\/picopak-64\?.*q=Pico\+Pale/)
  await views.getByRole('link', { name: 'Browse all' }).click()
  await expect(page.getByPlaceholder('Search community recipes and programs')).toHaveValue(
    'Pico Pale'
  )
  await expect(page.getByLabel('Catalog type')).toHaveValue('native-program')
  await expect(page.getByRole('link', { name: 'Create with AI', exact: true })).toHaveCount(0)
  await page.goto('/#/recipes/weekend-pale')
  await page.getByRole('button', { name: 'Ask the brew assistant', exact: true }).click()
  await expect(page.getByLabel('Existing recipe to build on (optional)')).toHaveValue(
    'Weekend Pale'
  )
  await expect(page.getByRole('heading', { name: 'Create a recipe with AI.' })).toBeVisible()
  await expect(page.getByRole('link', { name: 'Create with AI', exact: true })).toHaveCount(0)
  await expect(page.getByRole('tablist', { name: 'Assistant task' })).toHaveCount(0)
  await expect(main.getByRole('link', { name: /^Recipes/ })).toHaveAttribute('aria-current', 'page')
  await page.goto('/#/assistant?recipe=studio-porter')
  await expect(page).toHaveURL(/#\/recipes\/create-with-ai\?recipe=studio-porter$/)
  await expect(page.getByLabel('Existing recipe to build on (optional)')).toHaveValue(
    'Studio Porter'
  )
})

for (const width of [1440, 390]) {
  test(`readable typography and contained page layouts at ${width}px`, async ({ page }) => {
    await page.setViewportSize({ width, height: 960 })
    for (const route of [
      '/overview',
      '/recipes',
      '/recipes/studio-porter',
      '/catalog?kind=native-program&q=Pico%20Pale',
      '/recipes/create-with-ai',
      '/devices/demo-pro',
      '/history/demo-history-demo-pro?analysis=1',
      '/settings',
      '/api',
    ]) {
      await page.goto('/#' + route)
      await expect(page.locator('h1')).toBeVisible()
      await expect(page.getByRole('link', { name: 'Create with AI', exact: true })).toHaveCount(0)
      await expect(page.getByRole('button', { name: 'Create with AI', exact: true })).toHaveCount(0)
      const layout = await page.evaluate(() => ({
        width: innerWidth,
        scroll: document.documentElement.scrollWidth,
        base: parseFloat(getComputedStyle(document.body).fontSize),
        smallText: [
          ...document.querySelectorAll<HTMLElement | SVGElement>(
            'main p, main small, main label, main th, main td, main button, main input, main select, main svg text'
          ),
        ]
          .filter(
            (element) => element.checkVisibility() && element.getBoundingClientRect().height > 0
          )
          .map((element) => ({
            text: (element.textContent || element.getAttribute('aria-label') || element.tagName)
              .trim()
              .slice(0, 50),
            size: parseFloat(getComputedStyle(element).fontSize),
          }))
          .filter((element) => element.size > 0 && element.size < 14),
      }))
      expect(layout.scroll, `${route} horizontal overflow at ${width}px`).toBeLessThanOrEqual(
        layout.width + 1
      )
      expect(layout.base, `${route} body text`).toBeGreaterThanOrEqual(16)
      expect(layout.smallText, `${route} supporting text below 14px`).toEqual([])
      if (route === '/recipes' || route.startsWith('/history')) {
        await page.screenshot({
          path: test
            .info()
            .outputPath(`${route === '/recipes' ? 'recipes' : 'journal'}-${width}.png`),
          fullPage: true,
        })
      }
    }
  })
}

test('an empty library has one create action and the editor names each PicoPak flow path', async ({
  page,
}) => {
  await page.route('**/api/v1/recipes', (route) => route.fulfill({ json: [] }))
  await page.goto('/#/recipes')
  await expect(page.getByRole('button', { name: 'Create recipe', exact: true })).toHaveCount(0)
  const add = page.getByText('Add recipe', { exact: true })
  await expect(add).toHaveCount(1)
  await add.click()
  await page.getByRole('link', { name: 'Ask the brew assistant' }).click()
  await expect(page).toHaveURL(/#\/recipes\/create-with-ai$/)
  await expect(page.getByLabel('Existing recipe to build on (optional)')).toHaveValue('')
  await page.goBack()
  await add.click()
  await page.getByRole('link', { name: 'Create a recipe', exact: true }).click()
  await page.getByRole('button', { name: 'Advanced editor' }).click()
  const valve = page.getByLabel('Flow path', { exact: true }).first()
  await expect(valve).toHaveValue('0')
  await expect(valve.getByRole('option', { name: 'Hop addition 1', exact: true })).toHaveAttribute(
    'value',
    '3'
  )
  await expect(valve).toHaveCSS('appearance', 'none')
  await expect(
    page.getByLabel('Step type', { exact: true }).first().locator('option:checked')
  ).toHaveText('Mash rest')
})

test('library actions follow selection and Escape uses the visible Back action', async ({
  page,
}) => {
  await page.goto('/#/recipes')
  const send = page.getByRole('button', { name: /Send selected/ })
  const shopping = page.getByRole('button', { name: /Shopping list/ })
  await expect(send).toHaveCount(0)
  await page.getByLabel('Select Weekend Pale').check()
  await expect(send).toBeVisible()
  await expect(shopping).toBeVisible()
  const add = page.getByText('Add recipe', { exact: true })
  await add.click()
  await expect(page.getByRole('link', { name: 'Ask the brew assistant' })).toBeVisible()
  await page.keyboard.press('Escape')
  await expect(add).toBeFocused()
  await expect(page.getByRole('link', { name: 'Ask the brew assistant' })).toHaveCount(0)
  await page.goto('/#/recipes/weekend-pale')
  await page.getByRole('button', { name: 'Revision history' }).click()
  await page.keyboard.press('Escape')
  await expect(page.getByRole('dialog')).toHaveCount(0)
  await expect(page).toHaveURL(/weekend-pale$/)
  await page.keyboard.press('Escape')
  await expect(page).toHaveURL(/#\/recipes$/)
  await page.goto('/#/recipes/new')
  await expect(page.getByRole('region', { name: 'Recipe wizard' })).toBeVisible()
  const header = await page.locator('.page-heading').boundingBox()
  const form = await page.getByRole('region', { name: 'Recipe wizard' }).boundingBox()
  expect(form?.width).toBe(header?.width)
  await page.getByLabel('Recipe name').fill('Unsaved test recipe')
  page.once('dialog', (dialog) => dialog.dismiss())
  await page.keyboard.press('Escape')
  await expect(page.getByLabel('Recipe name')).toHaveValue('Unsaved test recipe')
  page.once('dialog', (dialog) => dialog.accept())
  await page.keyboard.press('Escape')
  await expect(page).toHaveURL(/#\/recipes$/)
})

test('guide saves a Z recipe with correctly timed hop bins and a deployable preview', async ({
  page,
  request,
}) => {
  const name = 'Guided Z pale ale'
  await page.goto('/#/recipes/new')
  await page.getByLabel('Recipe name').fill(name)
  await page.getByLabel('Brewer', { exact: true }).selectOption('z')
  await page.getByLabel('Batch size (L)').fill('8')
  await expect(page.getByLabel('Flow path')).toHaveCount(0)
  await page.getByRole('button', { name: 'Continue' }).click()
  await page.getByLabel('Grain', { exact: true }).fill('Pale malt')
  await page.getByLabel('Weight (kg)').fill('2')
  await page.getByRole('button', { name: 'Hop slot 4', exact: true }).click()
  await page.getByLabel('Hop variety').fill('Cascade')
  await page.getByLabel('Weight (g)').fill('20')
  await page.getByLabel('Minutes before boil ends').selectOption('15')
  await page.getByRole('button', { name: 'Add variety' }).click()
  await page.getByLabel('Hop variety').nth(1).fill('Amarillo')
  await page.getByLabel('Weight (g)').nth(1).fill('30')
  await expect(page.getByRole('button', { name: 'Continue' })).toBeDisabled()
  await page.getByLabel('Weight (g)').nth(1).fill('10')
  await page.getByRole('button', { name: 'Hop slot 1', exact: true }).click()
  await expect(
    page.getByLabel('Minutes before boil ends').locator('option[value="15"]')
  ).toHaveCount(0)
  await page.getByLabel('Minutes before boil ends').selectOption('60')
  await page.getByLabel('Hop variety').fill('Columbus')
  await page.getByLabel('Weight (g)').fill('8')
  await page.getByRole('button', { name: 'Continue' }).click()
  await page.getByRole('button', { name: 'Continue' }).click()
  await expect(page.getByText('Hop slot 4 · 15 min before boil ends')).toBeVisible()
  await page.getByRole('button', { name: 'Save recipe' }).click()
  const entry = page.getByRole('link', { name: new RegExp(name) })
  await expect(entry).toBeVisible()
  const id = (await entry.getAttribute('href'))!.split('/').at(-1)!
  await entry.click()
  await page.getByRole('button', { name: 'Adapt & send' }).click()
  await expect(page.getByLabel('Equipment profile')).toHaveValue('z')
  await expect(
    page.getByRole('dialog').getByLabel('Target batch size (L)', { exact: true })
  ).toHaveValue('8')
  try {
    const saved = await (await request.get('/api/v1/recipes/' + id)).json()
    expect(saved).toMatchObject({ name, source_model: 'z', batch_liters: 8, boil_minutes: 60 })
    const result = await request.post('/api/v1/recipes/' + id + '/adapt', {
      headers: { 'X-CSRF-Token': 'demo' },
      data: { profile_id: 'z', batch_liters: 8 },
    })
    expect(result.ok()).toBe(true)
    const plan = await result.json()
    expect(plan.can_deploy).toBe(true)
    expect(
      plan.machine_recipe.steps.map((s: { location: number; minutes: number }) => [
        s.location,
        s.minutes,
      ])
    ).toEqual([
      [1, 0],
      [2, 90],
      [3, 45],
      [6, 15],
    ])
  } finally {
    await request.delete('/api/v1/recipes/' + id, { headers: { 'X-CSRF-Token': 'demo' } })
  }
})

test('mobile Pico wizard carries ingredients and generated steps into advanced editing', async ({
  page,
}) => {
  await page.setViewportSize({ width: 390, height: 844 })
  await page.goto('/#/recipes/new')
  await page.getByLabel('Brewer', { exact: true }).selectOption('pico-s')
  await page.getByLabel('Recipe name').fill('Pico wheat')
  await page.getByRole('button', { name: 'Continue' }).click()
  await page.getByLabel('Grain', { exact: true }).fill('Wheat malt')
  await page.getByLabel('Weight (kg)').fill('100')
  await expect(page.getByRole('alert')).toContainText('Reduce it to 2 kg')
  await expect(page.getByRole('button', { name: 'Continue' })).toBeDisabled()
  await page.getByLabel('Weight (kg)').fill('1.5')
  await page.getByRole('button', { name: 'Hop slot 3', exact: true }).click()
  await expect(page.getByLabel('Minutes before boil ends')).toBeVisible()
  await page.getByRole('button', { name: 'Continue' }).click()
  await page.getByRole('button', { name: 'Continue' }).click()
  await expect(page.getByText('Wheat malt · 1.5 kg')).toBeVisible()
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true)
  await page.getByRole('button', { name: 'Advanced editor' }).click()
  await expect(page.getByLabel('Recipe name', { exact: true })).toHaveValue('Pico wheat')
  await expect(page.getByLabel('Ingredient 1 name')).toHaveValue('Wheat malt')
  await expect(page.getByLabel('2. Step name', { exact: true })).toHaveValue('Dough in')
  await expect(page.getByLabel('Flow path', { exact: true }).nth(1)).toHaveValue('2')
})

test('four loaded slots support eight distinct visits through save and adaptation', async ({
  page,
  request,
}) => {
  await page.goto('/#/recipes/new')
  await page.getByLabel('Brewer', { exact: true }).selectOption('pico-s')
  const name = 'Eight visits pale ale'
  await page.getByLabel('Recipe name').fill(name)
  await page.getByRole('button', { name: 'Continue' }).click()
  await page.getByLabel('Grain', { exact: true }).fill('Pale malt')
  await page.getByLabel('Weight (kg)').fill('1.2')
  const countdowns = [
    [60, 12],
    [52, 29],
    [45, 5],
    [38, 20],
  ]
  for (const [i, [first, again]] of countdowns.entries()) {
    await page.getByRole('button', { name: 'Hop slot ' + (i + 1), exact: true }).click()
    await page.getByLabel('Minutes before boil ends', { exact: true }).selectOption(String(first))
    await page.getByLabel('Hop variety').fill(['Cascade', 'Citra', 'Mosaic', 'Amarillo'][i])
    await page.getByLabel('Weight (g)').fill('8')
    await page.getByRole('button', { name: 'Add return visit' }).click()
    const repeat = page.getByLabel('Return 1 (minutes before boil ends)')
    await expect(repeat.locator('option[value="60"]')).toHaveCount(0)
    await repeat.selectOption(String(again))
  }
  await expect(page.getByText('Boil route', { exact: true }).locator('..')).toContainText(
    'Slot 4 (38 min) → Slot 2 (29 min) → Slot 4 (20 min) → Slot 1 (12 min) → Slot 3 (5 min)'
  )
  await page.getByRole('button', { name: 'Continue' }).click()
  await page.getByRole('button', { name: 'Continue' }).click()
  await page.getByRole('button', { name: 'Save recipe' }).click()
  const id = (await page.getByRole('link', { name: new RegExp(name) }).getAttribute('href'))!
    .split('/')
    .at(-1)!
  const headers = { 'X-CSRF-Token': 'demo' }
  try {
    const result = await request.post('/api/v1/recipes/' + id + '/adapt', {
      headers,
      data: { profile_id: 'pico-s', batch_liters: 5 },
    })
    expect(result.ok()).toBe(true)
    const plan = await result.json()
    expect(plan.can_deploy).toBe(true)
    expect(plan.recipe.ingredients.filter((i: { kind: string }) => i.kind === 'hop')).toHaveLength(
      4
    )
    expect(
      plan.machine_recipe.steps
        .slice(-8)
        .map((s: { location: number; minutes: number }) => [s.location, s.minutes])
    ).toEqual([
      [3, 8],
      [4, 7],
      [5, 7],
      [6, 9],
      [4, 9],
      [6, 8],
      [3, 7],
      [5, 5],
    ])
  } finally {
    await request.delete('/api/v1/recipes/' + id, { headers })
  }
})

test('starting with an archived PicoPak preserves every original step through transfer preview', async ({
  page,
  request,
}) => {
  await page.goto('/#/recipes/new')
  await page.getByRole('button', { name: 'Start from an original PicoPak' }).click()
  await expect(page).toHaveURL(/catalog\?[^#]*source=picopak-library/)
  const path = '/api/v1/catalog/picopak-1557'
  const original = await (await request.get(path + '/source?record=1557')).json()
  const headers = { 'X-CSRF-Token': 'demo' }
  const copy = await request.post(path + '/copy', { headers, data: {} })
  expect(copy.ok()).toBe(true)
  const { id } = await copy.json()
  try {
    for (const profile_id of ['pico-c', 'pico-s', 'pico-pro']) {
      const result = await request.post('/api/v1/recipes/' + id + '/adapt', {
        headers,
        data: { profile_id },
      })
      expect(result.ok()).toBe(true)
      const plan = await result.json()
      expect(plan.can_deploy).toBe(true)
      expect(plan.machine_recipe.steps).toEqual(
        original.Steps.map(
          (s: { Name: string; Location: number; Temp: number; Time: number; Drain: number }) => ({
            name: s.Name,
            location: s.Location,
            target_f: s.Temp,
            minutes: s.Time,
            drain_minutes: s.Drain,
          })
        )
      )
    }
  } finally {
    await request.delete('/api/v1/recipes/' + id, { headers })
  }
})

test('assistant filters both recipe sources and can change a catalog selection to a library recipe', async ({
  page,
}) => {
  await page.goto('/#/recipes/create-with-ai')
  const search = page.getByRole('searchbox', { name: 'Existing recipe to build on (optional)' })
  await search.fill('pale')
  const results = page.locator('[aria-label="Recipe search results"]')
  await expect(results.getByRole('button', { name: /Weekend Pale/ }).first()).toBeVisible()
  await results
    .getByRole('button', { name: /Pico Pale/i })
    .first()
    .click()
  await expect(search).toHaveValue(/Pico Pale/i)
  await expect(page.getByText(/^Community catalog ·/)).toBeVisible()
  await page.getByRole('button', { name: 'Clear selection' }).click()
  await search.fill('studio porter')
  await results
    .getByRole('button', { name: /Studio Porter/ })
    .first()
    .click()
  await expect(page.getByText(/^My library · Studio Porter/)).toBeVisible()
})

test('preview and publish a Zymatic recipe from the real transfer dialog', async ({ page }) => {
  await page.goto('/#/recipes/new')
  await page.getByLabel('Recipe name').fill('Stock sync test ale')
  await page.getByLabel('Brewer', { exact: true }).selectOption('zymatic')
  await page.getByRole('button', { name: 'Continue' }).click()
  await page.getByLabel('Grain', { exact: true }).fill('Pale malt')
  await page.getByLabel('Weight (kg)').fill('1.5')
  await page.getByRole('button', { name: 'Continue' }).click()
  await page.getByRole('button', { name: 'Continue' }).click()
  await page.getByRole('button', { name: /save recipe/i }).click()
  await page.getByRole('link', { name: /^Stock sync test ale/ }).click()
  await page.getByRole('button', { name: /adapt.*send/i }).click()
  await page.getByLabel('Target machine').selectOption('stock-zymatic-menu')
  const dialog = page.getByRole('dialog')
  await dialog.getByLabel('Zymatic serial number').fill('ABC123XYZ999')
  await dialog.getByRole('button', { name: 'Preview stock menu' }).click()
  await expect(dialog.getByRole('cell', { name: 'Keg circulation' }).first()).toBeVisible()
  await dialog.getByRole('button', { name: 'Publish to brewer menu' }).click()
  await expect(dialog.getByText(/Published for ABC123XYZ999/)).toBeVisible()
})
