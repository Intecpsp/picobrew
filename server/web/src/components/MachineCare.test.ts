// @vitest-environment jsdom
import { createElement } from 'react'
import { cleanup, render, screen } from '@testing-library/react'
import { afterEach, expect, it } from 'vitest'
import { CareNotice, MachineCare } from './MachineCare'

afterEach(cleanup)

it.each([
  ['pico-s', 'the brewing keg', 'pico/Pico_Manual.pdf'],
  ['pico-pro', 'the brewing keg', 'picopro/PicoPro_Manual.pdf'],
  ['pico-c', 'the center of the step filter', 'picoc/PicoC_Manual.pdf'],
])(
  'shows the cleaning tablet location and post-descaling procedure for %s',
  (model, location, manual) => {
    render(createElement(MachineCare, { model }))
    expect(screen.getByText(/cleaning tablet/i).textContent).toContain(location)
    expect(screen.getByText(/after descaling/i).textContent).toContain(
      'Run Deep Clean after descaling.'
    )
    expect(screen.getByRole('link').getAttribute('href')).toContain(manual)
  }
)

it('shows device-reported tasks and clears the reminder after completion', () => {
  const view = render(createElement(CareNotice, {}))
  expect(screen.queryByRole('status')).toBeNull()
  view.rerender(createElement(CareNotice, { maintenance: { due: ['rinse', 'clean_filter'] } }))
  expect(screen.getByRole('status').textContent).toContain('Rinse the brewer')
  expect(screen.getByRole('status').textContent).toContain('Clean the inline filter')
  view.rerender(createElement(CareNotice, { maintenance: { due: [] } }))
  expect(screen.queryByRole('status')).toBeNull()
})
