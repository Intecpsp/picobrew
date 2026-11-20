// @vitest-environment jsdom
import { createElement } from 'react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { cleanup, render, screen, waitFor, within } from '@testing-library/react'
import App from './App'

vi.mock('./pages/Fleet', () => ({ default: () => createElement('h1', null, 'Fleet view') }))
vi.mock('./pages/Recipes', () => ({
  default: ({ id }: { id?: string }) =>
    createElement('h1', null, id ? `Recipe ${id}` : 'My recipes view'),
}))
vi.mock('./pages/Catalog', () => ({
  default: ({ id }: { id?: string }) =>
    createElement('h1', null, id ? `Source ${id}` : 'Browse all view'),
}))
vi.mock('./pages/Assistant', () => ({
  default: () => createElement('h1', null, 'AI recipe creation'),
}))
vi.mock('./pages/History', () => ({
  default: ({ id }: { id?: string }) =>
    createElement('h1', null, id ? `Journal ${id}` : 'Journal view'),
}))

beforeEach(() => {
  vi.stubGlobal(
    'fetch',
    vi.fn(
      async (path) =>
        new Response(
          JSON.stringify(
            String(path).endsWith('/auth/session')
              ? { authenticated: true, csrf_token: 'test', version: 'test' }
              : []
          ),
          { headers: { 'Content-Type': 'application/json' } }
        )
    )
  )
  vi.stubGlobal(
    'EventSource',
    class {
      onopen = null
      onerror = null
      addEventListener() {}
      close() {}
    }
  )
  vi.spyOn(window, 'scrollTo').mockImplementation(() => {})
})

afterEach(() => {
  cleanup()
  vi.unstubAllGlobals()
  vi.restoreAllMocks()
})

describe('recipe workspace navigation', () => {
  it.each([
    ['/recipes', 'My recipes view', 'My recipes'],
    ['/recipes/new', 'Recipe new', 'My recipes'],
    ['/recipes/create-with-ai?recipe=studio-porter', 'AI recipe creation', 'My recipes'],
    ['/catalog?kind=native-program&q=Pico%20Pale', 'Browse all view', 'Browse all'],
    ['/catalog/picopak-64?kind=native-program&q=Pico%20Pale', 'Source picopak-64', 'Browse all'],
  ])(
    'keeps %s inside the single Recipes navigation entry',
    async (route, heading, selectedView) => {
      history.replaceState(null, '', '/#' + route)
      render(createElement(App))
      await screen.findByRole('heading', { name: heading })
      const main = within(screen.getByRole('navigation', { name: 'Main navigation' }))
      expect(main.getByRole('link', { name: /^Recipes/ }).getAttribute('aria-current')).toBe('page')
      expect(main.queryByRole('link', { name: 'Community catalogue' })).toBeNull()
      expect(main.queryByRole('link', { name: 'Brew assistant' })).toBeNull()
      const views = within(screen.getByRole('navigation', { name: 'Recipe views' }))
      expect(views.getAllByRole('link')).toHaveLength(2)
      expect(views.getByRole('link', { name: selectedView }).getAttribute('aria-current')).toBe(
        'page'
      )
      if (route.split('?')[0] === '/recipes/create-with-ai') {
        expect(screen.queryByRole('link', { name: 'Create with AI' })).toBeNull()
      } else {
        expect(screen.getByRole('link', { name: 'Create with AI' }).getAttribute('href')).toBe(
          '#/recipes/create-with-ai'
        )
      }
      if (route.startsWith('/catalog')) {
        expect(views.getByRole('link', { name: 'Browse all' }).getAttribute('href')).toBe(
          '#/catalog?kind=native-program&q=Pico%20Pale'
        )
      }
    }
  )

  it.each([
    [
      '/assistant?recipe=studio-porter',
      '/recipes/create-with-ai?recipe=studio-porter',
      'AI recipe creation',
    ],
    [
      '/assistant?brew=demo-history-demo-pro',
      '/history/demo-history-demo-pro?analysis=1',
      'Journal demo-history-demo-pro',
    ],
  ])(
    'keeps legacy %s links tied to their relevant workspace',
    async (route, destination, heading) => {
      history.replaceState(null, '', '/#' + route)
      render(createElement(App))
      await screen.findByRole('heading', { name: heading })
      await waitFor(() => expect(location.hash).toBe('#' + destination))
      if (destination.startsWith('/history')) {
        expect(screen.queryByRole('navigation', { name: 'Recipe views' })).toBeNull()
        const main = within(screen.getByRole('navigation', { name: 'Main navigation' }))
        expect(main.getByRole('link', { name: 'Brew journal' }).getAttribute('aria-current')).toBe(
          'page'
        )
      }
    }
  )
})
