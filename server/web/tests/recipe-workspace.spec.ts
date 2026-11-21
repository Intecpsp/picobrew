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
    'weekend-pale'
  )
  await expect(page.getByRole('heading', { name: 'Create a recipe with AI.' })).toBeVisible()
  await expect(page.getByRole('link', { name: 'Create with AI', exact: true })).toHaveCount(0)
  await expect(page.getByRole('tablist', { name: 'Assistant task' })).toHaveCount(0)
  await expect(main.getByRole('link', { name: /^Recipes/ })).toHaveAttribute('aria-current', 'page')
  await page.goto('/#/assistant?recipe=studio-porter')
  await expect(page).toHaveURL(/#\/recipes\/create-with-ai\?recipe=studio-porter$/)
  await expect(page.getByLabel('Existing recipe to build on (optional)')).toHaveValue(
    'studio-porter'
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
  await page.getByRole('button', { name: 'Create a recipe', exact: true }).click()
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
