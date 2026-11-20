import { BookOpen, Library, Sparkles } from 'lucide-react'
import './recipe-workspace.scss'

export type RecipeView = 'recipes' | 'catalog'

const views = [
  { id: 'recipes', label: 'My recipes', icon: BookOpen },
  { id: 'catalog', label: 'Browse all', icon: Library },
] as const

export function recipeViewForRoute(route: string): RecipeView | null {
  const section = route.split('?')[0].split('/').filter(Boolean)[0]
  return views.find((view) => view.id === section)?.id || null
}

export function RecipeWorkspaceTabs({ route }: { route: string }) {
  const active = recipeViewForRoute(route)
  const query = route.includes('?') ? route.slice(route.indexOf('?')) : ''
  return (
    <div className="recipe-workspace">
      <nav className="recipe-workspace-tabs" aria-label="Recipe views">
        {views.map((view) => (
          <a
            key={view.id}
            href={`#/${view.id}${view.id === 'catalog' && active === 'catalog' ? query : ''}`}
            aria-current={active === view.id ? 'page' : undefined}
            onClick={(event) => {
              if (view.id === 'catalog' && active === 'catalog') {
                // Filter edits use replaceState, so they need not re-render this navigation.
                const hash = location.hash
                event.currentTarget.href =
                  '#/catalog' + (hash.includes('?') ? hash.slice(hash.indexOf('?')) : '')
              }
            }}
          >
            <view.icon size={18} aria-hidden="true" />
            <span>{view.label}</span>
          </a>
        ))}
      </nav>
      {route.split('?')[0] !== '/recipes/create-with-ai' && (
        <a className="button secondary recipe-create-ai" href="#/recipes/create-with-ai">
          <Sparkles size={17} aria-hidden="true" />
          Create with AI
        </a>
      )}
    </div>
  )
}
