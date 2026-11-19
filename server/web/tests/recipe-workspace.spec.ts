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
  await expect(main.getByRole('link', { name: /Community catalogue|Brew assistant/ })).toHaveCount(
    0
  )
  await expect(views.getByRole('link')).toHaveCount(2)
  await views.getByRole('link', { name: 'Browse all' }).click()
  await page.getByPlaceholder('Search community recipes and programs').fill('Pico Pale')
  await page.getByLabel('Catalogue type').selectOption('native-program')
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
  await expect(page.getByLabel('Catalogue type')).toHaveValue('native-program')
  await page.getByRole('link', { name: 'Create with AI', exact: true }).click()
  await expect(page.getByRole('heading', { name: 'Create a recipe with AI.' })).toBeVisible()
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
