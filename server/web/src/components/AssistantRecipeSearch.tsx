import { modelLabel } from '../lib/utils'
import { useEffect, useState } from 'react'
import { api, errorMessage } from '../lib/api'
import type { CatalogPage, Recipe } from '../types'
import { Button, ErrorBanner, Field } from './ui'

export type RecipeReference = { source: 'library' | 'catalog'; id: string; name: string }
export function AssistantRecipeSearch({
  recipes,
  selected,
  choose,
}: {
  recipes: Recipe[]
  selected: RecipeReference | null
  choose: (r: RecipeReference | null) => void
}) {
  const [query, setQuery] = useState(''),
    [offset, setOffset] = useState(0),
    [libraryLimit, setLibraryLimit] = useState(8)
  const term = query.trim()
  const [found, setFound] = useState<{
    term: string
    offset: number
    items: CatalogPage['items']
    total: number
    error: string
  } | null>(null)
  useEffect(() => {
    if (!term || selected) return
    const controller = new AbortController()
    const timer = setTimeout(async () => {
      try {
        const page = await api<CatalogPage>(
          '/catalog?' +
            new URLSearchParams({
              kind: 'all-brewers',
              q: term,
              limit: '8',
              offset: String(offset),
            }),
          { signal: controller.signal }
        )
        if (!controller.signal.aborted)
          setFound({ term, offset, items: page.items, total: page.total, error: '' })
      } catch (e) {
        if (!controller.signal.aborted)
          setFound({ term, offset, items: [], total: 0, error: errorMessage(e) })
      }
    }, 180)
    return () => {
      clearTimeout(timer)
      controller.abort()
    }
  }, [term, offset, selected])
  const results = found?.term === term && found.offset === offset ? found : null
  const library = recipes.filter((r) =>
    [r.name, r.style, ...(r.tags ?? [])].join(' ').toLowerCase().includes(term.toLowerCase())
  )
  function pick(r: RecipeReference) {
    choose(r)
    setQuery('')
    setOffset(0)
    setLibraryLimit(8)
  }
  return (
    <div className="assistant-recipe-search">
      <Field label="Existing recipe to build on (optional)">
        <input
          type="search"
          maxLength={128}
          value={selected?.name || query}
          placeholder="Search your library and the community catalog"
          onChange={(e) => {
            choose(null)
            setQuery(e.target.value)
            setOffset(0)
            setLibraryLimit(8)
          }}
        />
      </Field>
      {selected && (
        <p>
          {selected.source === 'library' ? 'My library' : 'Community catalog'} · {selected.name}{' '}
          <Button variant="ghost" onClick={() => choose(null)}>
            Clear selection
          </Button>
        </p>
      )}
      {!selected && term && (
        <div role="region" className="assistant-search-matches" aria-label="Recipe search results">
          <h4>My library</h4>
          {library.slice(0, libraryLimit).map((r) => (
            <Button
              key={r.id}
              className="assistant-search-match"
              variant="ghost"
              onClick={() => pick({ source: 'library', id: r.id, name: r.name })}
            >
              {r.name}
              {r.style && ' · ' + r.style}
            </Button>
          ))}
          {library.length === 0 && <p>No library matches.</p>}
          {library.length > libraryLimit && (
            <Button variant="secondary" onClick={() => setLibraryLimit(libraryLimit + 8)}>
              More library matches
            </Button>
          )}
          <h4>Community catalog</h4>
          {!results ? (
            <p role="status">Searching…</p>
          ) : results.error ? (
            <ErrorBanner message={results.error} />
          ) : (
            <>
              {results.items.map((r) => (
                <Button
                  key={r.id}
                  className="assistant-search-match"
                  variant="ghost"
                  onClick={() => pick({ source: 'catalog', id: r.id, name: r.name })}
                >
                  {r.name}
                  {r.style && ' · ' + r.style}
                  {r.source_model && ' · ' + modelLabel(r.source_model)}
                </Button>
              ))}
              {!results.total && <p>No catalog matches.</p>}
              {offset > 0 && (
                <Button variant="secondary" onClick={() => setOffset(offset - 8)}>
                  Previous catalog matches
                </Button>
              )}
              {offset + 8 < results.total && (
                <Button variant="secondary" onClick={() => setOffset(offset + 8)}>
                  More catalog matches
                </Button>
              )}
            </>
          )}
        </div>
      )}
    </div>
  )
}
