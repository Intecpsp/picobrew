// @vitest-environment jsdom
import { createElement } from 'react'
import { afterEach, expect, it, vi } from 'vitest'
import { cleanup, fireEvent, render, screen } from '@testing-library/react'
import type { Ingredient } from '../types'
import { newRecipe } from '../lib/utils'
import { FermentationPlan } from './FermentationPlan'

afterEach(cleanup)

it.each([
  { existing: [], name: 'Starting keg water' },
  {
    existing: [
      { name: 'Well water', kind: 'water', amount: 1, unit: 'L', use: 'sparge', minutes: 20 },
    ],
    name: 'Well water',
  },
])('marks estimated water as the full starting charge', ({ existing, name }) => {
  const change = vi.fn()
  const recipe = { ...newRecipe(), boil_liters: 6, ingredients: existing as Ingredient[] }
  render(createElement(FermentationPlan, { recipe, change }))
  fireEvent.change(screen.getByLabelText('Grain absorption (L/kg)'), { target: { value: '0' } })
  fireEvent.change(screen.getByLabelText('Equipment loss (L)'), { target: { value: '1' } })
  fireEvent.click(screen.getByRole('button', { name: /Use .* of water/ }))
  expect(change).toHaveBeenCalledWith({
    ingredients: [{ name, amount: 7, kind: 'water', unit: 'L', use: 'starting keg', minutes: 0 }],
  })
})
