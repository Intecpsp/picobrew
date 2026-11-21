import { RecipeIngredients, RecipeSchedule } from '../components/RecipePlan'
import { useEffect, useRef, useState, type FormEvent } from 'react'
import {
  Plus,
  Upload,
  Download,
  ArrowUpRight,
  Trash2,
  Send,
  Save,
  FlaskConical,
  Clock3,
  Wheat,
  ShoppingBasket,
  History,
  Copy,
  Sparkles,
  Check,
  Loader2,
  BookOpen,
} from 'lucide-react'
import { useHub, useResource } from '../context'
import { api, download, errorMessage } from '../lib/api'
import { dateLabel, newRecipe, slug } from '../lib/utils'
import { isNative, kindLabel, programTiming } from '../lib/programs'
import { formatQuantity, formatVolume } from '../lib/recipeDisplay'
import NativeRecipeEditor from './NativeRecipeEditor'
import { TransferDialog } from '../components/TransferDialog'
import { AdaptDialog } from '../components/AdaptDialog'
import type { Recipe, Ingredient } from '../types'
import {
  Badge,
  Button,
  Confirm,
  Empty,
  ErrorBanner,
  Field,
  Loading,
  Modal,
  PageTitle,
  SearchBox,
  SectionHeading,
} from '../components/ui'
export default function Recipes({ id }: { id?: string }) {
  return id ? <RecipeEditor id={id} /> : <RecipeLibrary />
}
function RecipeLibrary() {
  const hub = useHub()
  const [query, setQuery] = useState('')
  const [importing, setImporting] = useState(false)
  const [selected, setSelected] = useState<string[]>([])
  const [shopping, setShopping] = useState(false)
  const [transfer, setTransfer] = useState(false)
  const shoppingIDs = selected.filter((id) =>
    hub.recipes.some((recipe) => recipe.id === id && !isNative(recipe))
  )
  const filtered = hub.recipes.filter((recipe) =>
    `${recipe.name} ${recipe.style} ${(recipe.tags || []).join(' ')}`
      .toLowerCase()
      .includes(query.toLowerCase())
  )
  return (
    <>
      <PageTitle
        eyebrow="A COLLECTION WORTH BREWING"
        title="Your recipe library."
        description="Create once. Refine with every brew. Adapt to the machine on your bench."
        actions={
          <>
            <Button variant="secondary" onClick={() => hub.navigate('/catalog')}>
              <BookOpen size={16} />
              Community catalogue
            </Button>
            <Button variant="secondary" onClick={() => setImporting(true)}>
              <Upload size={16} />
              Import recipe
            </Button>
            <Button onClick={() => hub.navigate('/recipes/new')}>
              <Plus size={16} />
              Create recipe
            </Button>
          </>
        }
      />
      <div className="list-toolbar">
        <SearchBox value={query} onChange={setQuery} placeholder="Search recipes, styles or tags" />
        <div className="inline-actions">
          <span className="muted">{filtered.length} recipes & programs</span>
          <Button variant="secondary" disabled={!selected.length} onClick={() => setTransfer(true)}>
            <Send size={16} />
            Send selected{selected.length ? ` (${selected.length})` : ''}
          </Button>
          <Button
            variant="secondary"
            disabled={!shoppingIDs.length}
            onClick={() => setShopping(true)}
          >
            <ShoppingBasket size={16} />
            Shopping list{shoppingIDs.length ? ` (${shoppingIDs.length})` : ''}
          </Button>
        </div>
      </div>
      {filtered.length ? (
        <div className="recipe-grid">
          {filtered.map((recipe, index) => (
            <article className="recipe-card" key={recipe.id}>
              <div className={`recipe-art tone-${index % 3}`}>
                <Wheat size={58} strokeWidth={1.1} />
                <div className="recipe-art-lines" />
                <label className="recipe-select">
                  <input
                    type="checkbox"
                    aria-label={`Select ${recipe.name}`}
                    checked={selected.includes(recipe.id)}
                    onChange={(event) =>
                      setSelected((values) =>
                        event.target.checked
                          ? [...values, recipe.id]
                          : values.filter((id) => id !== recipe.id)
                      )
                    }
                  />
                </label>
                <span>{recipe.style || kindLabel(recipe.kind)}</span>
              </div>
              <div className="recipe-card-content">
                <Badge tone={isNative(recipe) ? 'copper' : 'green'}>{kindLabel(recipe.kind)}</Badge>
                <h3>
                  <button onClick={() => hub.navigate(`/recipes/${recipe.id}`)}>
                    {recipe.name}
                  </button>
                </h3>
                <p>{recipe.description || 'A recipe ready for your next brew day.'}</p>
                <div className="recipe-meta">
                  {isNative(recipe) ? (
                    <>
                      <span>{recipe.source_model}</span>
                      <span>
                        <Clock3 size={14} />
                        {recipe.native_program
                          ? programTiming(recipe.native_program)
                          : 'Schedule unavailable'}
                      </span>
                      <span>Program only</span>
                    </>
                  ) : (
                    <>
                      <span>
                        <FlaskConical size={14} />
                        {formatVolume(recipe.batch_liters)}
                      </span>
                      <span>
                        <Clock3 size={14} />
                        {(recipe.steps || []).reduce(
                          (total, step) => total + step.minutes + step.drain_minutes,
                          0
                        )}{' '}
                        min
                      </span>
                      <span>{(recipe.ingredients || []).length} ingredients</span>
                    </>
                  )}
                </div>
                <div className="recipe-tags">
                  {(recipe.tags || []).slice(0, 3).map((tag) => (
                    <span key={tag}>{tag}</span>
                  ))}
                </div>
                <footer>
                  <small>Revision {recipe.revision}</small>
                  <button onClick={() => hub.navigate(`/recipes/${recipe.id}`)}>
                    Open recipe
                    <ArrowUpRight size={16} />
                  </button>
                </footer>
              </div>
            </article>
          ))}
        </div>
      ) : (
        <div className="panel">
          <Empty
            icon={<Wheat size={28} />}
            title={query ? 'No recipes match that search.' : 'Great brews start with an idea.'}
            description={
              query
                ? 'Try a different name or style.'
                : 'Create your first recipe, import BeerXML, or ask the brew assistant for a starting point.'
            }
            action={
              !query && (
                <Button onClick={() => hub.navigate('/recipes/new')}>
                  Create a recipe
                  <Plus size={16} />
                </Button>
              )
            }
          />
        </div>
      )}
      {importing && <ImportDialog onClose={() => setImporting(false)} />}{' '}
      {shopping && <ShoppingList ids={shoppingIDs} onClose={() => setShopping(false)} />}
      {transfer && (
        <TransferDialog
          recipes={hub.recipes}
          initialSelection={selected}
          onClose={() => setTransfer(false)}
        />
      )}
    </>
  )
}
function metadata(recipe: Recipe) {
  const {
    id,
    name,
    style,
    description,
    batch_liters,
    efficiency,
    ingredients,
    steps,
    tags,
    notes,
    revision,
    created_at,
    updated_at,
    type,
    boil_liters,
    boil_minutes,
    ...extras
  } = recipe as Recipe & Record<string, unknown>
  return extras
}
function RecipeEditor({ id }: { id: string }) {
  const hub = useHub()
  const isNew = id === 'new'
  const loaded = useResource<Recipe>(isNew ? null : `/recipes/${id}`)
  const [recipe, setRecipe] = useState<Recipe>(() =>
    isNew && hub.draft ? { ...hub.draft, id: '', revision: 0 } : newRecipe()
  )
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState('')
  const [dirty, setDirty] = useState(isNew)
  const [tagsInput, setTagsInput] = useState(isNew ? (hub.draft?.tags || []).join(', ') : '')
  const [deploy, setDeploy] = useState(false)
  const [remove, setRemove] = useState(false)
  const [revisions, setRevisions] = useState(false)
  const [advanced, setAdvanced] = useState<string | null>(null)
  const originalRevision = useRef(0)
  useEffect(() => {
    if (loaded.data) {
      setRecipe({
        ...loaded.data,
        ingredients: loaded.data.ingredients || [],
        steps: loaded.data.steps || [],
        tags: loaded.data.tags || [],
      })
      setAdvanced(null)
      originalRevision.current = loaded.data.revision
      setTagsInput((loaded.data.tags || []).join(', '))
      setDirty(false)
    }
  }, [loaded.data])
  useEffect(() => {
    if (isNew) hub.setDraft(null)
  }, [])
  useEffect(() => {
    const listener = (event: BeforeUnloadEvent) => {
      if (dirty) {
        event.preventDefault()
        event.returnValue = ''
      }
    }
    window.addEventListener('beforeunload', listener)
    return () => window.removeEventListener('beforeunload', listener)
  }, [dirty])
  function update(patch: Partial<Recipe>) {
    setRecipe((value) => ({ ...value, ...patch }))
    setDirty(true)
  }
  async function save(event?: FormEvent) {
    event?.preventDefault()
    setBusy(true)
    setError('')
    try {
      let payload = {
        ...recipe,
        tags: tagsInput
          .split(',')
          .map((tag) => tag.trim())
          .filter(Boolean),
      }
      if (advanced?.trim()) {
        const extras = JSON.parse(advanced)
        if (!extras || Array.isArray(extras) || typeof extras !== 'object')
          throw new Error('Advanced metadata must be a JSON object.')
        payload = { ...payload, ...extras, id: recipe.id, revision: recipe.revision }
      }
      const saved = await api<Recipe>(isNew ? '/recipes' : `/recipes/${id}`, {
        method: isNew ? 'POST' : 'PUT',
        body: payload,
      })
      setRecipe(saved)
      originalRevision.current = saved.revision
      setDirty(false)
      await hub.refresh()
      hub.notify('Recipe saved to your library.')
      if (isNew) hub.navigate(`/recipes/${saved.id}`)
    } catch (error) {
      setError(errorMessage(error))
    } finally {
      setBusy(false)
    }
  }
  async function exportRecipe(format: string) {
    try {
      await download(
        `/recipes/${id}/export?format=${format}`,
        `${slug(recipe.name)}.${format === 'beerxml' ? 'xml' : 'json'}`
      )
    } catch (error) {
      hub.notify(errorMessage(error), 'error')
    }
  }
  if (!isNew && loaded.loading) return <Loading label="Opening your recipe…" />
  if (!isNew && loaded.error) return <ErrorBanner message={loaded.error} retry={loaded.reload} />
  const nativeRecipe = isNew ? recipe : loaded.data
  if (nativeRecipe && isNative(nativeRecipe))
    return <NativeRecipeEditor initial={nativeRecipe} isNew={isNew} />
  return (
    <>
      <PageTitle
        back={() => {
          if (!dirty || window.confirm('Leave this recipe without saving your changes?'))
            hub.navigate('/recipes')
        }}
        eyebrow={
          isNew
            ? 'A FRESH START'
            : `RECIPE · REVISION ${recipe.revision}${dirty ? ' · UNSAVED CHANGES' : ''}`
        }
        title={isNew ? 'What will you brew next?' : recipe.name || 'Untitled recipe'}
        actions={
          <>
            {!isNew && (
              <Button
                variant="secondary"
                disabled={dirty}
                onClick={() => setDeploy(true)}
                title={dirty ? 'Save your changes before adapting this recipe' : ''}
              >
                <Send size={16} />
                Adapt & send
              </Button>
            )}
            <Button disabled={busy} onClick={() => void save()}>
              {busy ? <Loader2 size={16} className="spin" /> : <Save size={16} />}Save recipe
            </Button>
          </>
        }
      />
      <ErrorBanner message={error} />
      <div className="editor-layout">
        <div className="editor-main">
          <section className="panel">
            <SectionHeading title="Recipe details" />
            <div className="form-grid">
              <Field label="Recipe name">
                <input
                  value={recipe.name}
                  onChange={(event) => update({ name: event.target.value })}
                  placeholder="Sunday afternoon pale ale"
                  maxLength={100}
                  required
                />
              </Field>
              <Field label="Style">
                <input
                  value={recipe.style}
                  onChange={(event) => update({ style: event.target.value })}
                  placeholder="American Pale Ale"
                />
              </Field>
            </div>
            <Field label="Description">
              <textarea
                rows={2}
                value={recipe.description}
                onChange={(event) => update({ description: event.target.value })}
                placeholder="The flavors and character you're aiming for…"
              />
            </Field>
            <div className="form-grid thirds">
              <Field label="Batch size (L)">
                <input
                  type="number"
                  min="0.1"
                  step="0.1"
                  value={recipe.batch_liters}
                  onChange={(event) => update({ batch_liters: Number(event.target.value) })}
                />
              </Field>
              <Field label="Brewhouse efficiency (%)">
                <input
                  type="number"
                  min="1"
                  max="100"
                  value={recipe.efficiency}
                  onChange={(event) => update({ efficiency: Number(event.target.value) })}
                />
              </Field>
              <Field label="Tags">
                <input
                  value={tagsInput}
                  onChange={(event) => {
                    setTagsInput(event.target.value)
                    setDirty(true)
                  }}
                  placeholder="citrus, weekend"
                />
              </Field>
            </div>
          </section>
          <RecipeIngredients
            items={recipe.ingredients}
            change={(ingredients) => update({ ingredients })}
          />
          <RecipeSchedule
            steps={recipe.steps}
            unit={hub.unit}
            change={(steps) => update({ steps })}
          />
          <section className="panel">
            <SectionHeading title="Boil planning" />
            <div className="form-grid thirds">
              <Field label="Recipe type">
                <select
                  value={recipe.type || ''}
                  onChange={(event) => update({ type: event.target.value })}
                >
                  <option value="">Not specified</option>
                  <option>All Grain</option>
                  <option>Partial Mash</option>
                  <option>Extract</option>
                </select>
              </Field>
              <Field label="Boil volume (L)">
                <input
                  type="number"
                  min="0"
                  step="0.1"
                  value={recipe.boil_liters || ''}
                  onChange={(event) => update({ boil_liters: Number(event.target.value) })}
                />
              </Field>
              <Field label="Boil duration (min)">
                <input
                  type="number"
                  min="0"
                  value={recipe.boil_minutes || ''}
                  onChange={(event) => update({ boil_minutes: Number(event.target.value) })}
                />
              </Field>
            </div>
          </section>
          <section className="panel">
            <SectionHeading title="Notes for brew day" />
            <Field label="Recipe notes">
              <textarea
                rows={4}
                value={recipe.notes}
                onChange={(event) => update({ notes: event.target.value })}
                placeholder="Water treatment, preparation, tasting goals…"
              />
            </Field>
            <Field
              label="Water, fermentation, carbonation and other recipe metadata"
              hint="Water chemistry, fermentation and carbonation are stored with the recipe as JSON."
            >
              <textarea
                className="code-input"
                rows={5}
                value={advanced ?? JSON.stringify(metadata(recipe), null, 2)}
                onChange={(event) => {
                  setAdvanced(event.target.value)
                  setDirty(true)
                }}
              />
            </Field>
          </section>
        </div>
        <aside className="editor-aside">
          <div className="recipe-summary">
            <p className="eyebrow">ON THE BREW SHEET</p>
            <Wheat size={38} strokeWidth={1.2} />
            <h3>{recipe.name || 'A recipe in the making'}</h3>
            <dl>
              <div>
                <dt>Batch</dt>
                <dd>{formatVolume(recipe.batch_liters)}</dd>
              </div>
              <div>
                <dt>Ingredients</dt>
                <dd>{recipe.ingredients.length}</dd>
              </div>
              <div>
                <dt>Schedule</dt>
                <dd>
                  {recipe.steps.reduce(
                    (total, step) => total + step.minutes + step.drain_minutes,
                    0
                  )}{' '}
                  min
                </dd>
              </div>
              <div>
                <dt>Steps</dt>
                <dd>{recipe.steps.length}</dd>
              </div>
            </dl>
            <p>
              Save your recipe, then preview how it adapts to a specific machine before sending it.
            </p>
          </div>
          <div className="panel small-panel">
            <h3>Make it your own.</h3>
            <p>Need a starting point or another perspective?</p>
            <Button
              variant="secondary"
              className="full-width"
              onClick={() =>
                hub.navigate(`/recipes/create-with-ai?recipe=${encodeURIComponent(recipe.id)}`)
              }
            >
              <Sparkles size={15} />
              Ask the brew assistant
            </Button>
          </div>
          {!isNew && (
            <div className="editor-tools">
              <Button variant="ghost" onClick={() => void exportRecipe('json')}>
                <Download size={15} />
                Export JSON
              </Button>
              <Button variant="ghost" onClick={() => void exportRecipe('beerxml')}>
                <Download size={15} />
                Export BeerXML
              </Button>
              <Button variant="ghost" onClick={() => setRevisions(true)}>
                <History size={15} />
                Revision history
              </Button>
              <Button
                variant="ghost"
                onClick={() => {
                  hub.setDraft({ ...recipe, id: '', revision: 0, name: `${recipe.name} — copy` })
                  hub.navigate('/recipes/new')
                }}
              >
                <Copy size={15} />
                Duplicate recipe
              </Button>
              <Button variant="ghost" className="danger-text" onClick={() => setRemove(true)}>
                <Trash2 size={15} />
                Delete recipe
              </Button>
            </div>
          )}
        </aside>
      </div>
      {deploy && <AdaptDialog recipe={recipe} onClose={() => setDeploy(false)} />}{' '}
      {remove && (
        <Confirm
          title="Delete this recipe?"
          description="This removes it from the shared library. Existing brew history and recipes already on machines are retained."
          label="Delete recipe"
          danger
          busy={busy}
          onClose={() => setRemove(false)}
          onConfirm={async () => {
            setBusy(true)
            try {
              await api(`/recipes/${id}`, { method: 'DELETE' })
              await hub.refresh()
              hub.notify('Recipe removed.')
              hub.navigate('/recipes')
            } catch (error) {
              setError(errorMessage(error))
              setRemove(false)
            } finally {
              setBusy(false)
            }
          }}
        />
      )}{' '}
      {revisions && (
        <RevisionDialog
          id={id}
          onClose={() => setRevisions(false)}
          onLoad={(old) => {
            setRecipe({ ...old, id, revision: originalRevision.current })
            setTagsInput((old.tags || []).join(', '))
            setAdvanced('')
            setDirty(true)
            setRevisions(false)
            hub.notify(`Revision ${old.revision} loaded. Save to create a new revision.`)
          }}
        />
      )}
    </>
  )
}
function ImportDialog({ onClose }: { onClose: () => void }) {
  const hub = useHub()
  const [format, setFormat] = useState('beerxml')
  const [data, setData] = useState('')
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState('')
  async function submit() {
    setBusy(true)
    setError('')
    try {
      const items = await api<Recipe[]>('/recipes/import', {
        method: 'POST',
        body: { format, data },
      })
      await hub.refresh()
      hub.notify(`Imported ${items.length} ${items.length === 1 ? 'recipe' : 'recipes'}.`)
      onClose()
      if (items.length === 1) hub.navigate(`/recipes/${items[0].id}`)
    } catch (error) {
      setError(errorMessage(error))
    } finally {
      setBusy(false)
    }
  }
  return (
    <Modal
      title="Bring your recipes along."
      onClose={onClose}
      wide
      actions={
        <>
          <Button variant="secondary" onClick={onClose}>
            Cancel
          </Button>
          <Button disabled={!data.trim() || busy} onClick={() => void submit()}>
            <Upload size={16} />
            {busy ? 'Importing…' : 'Import recipes'}
          </Button>
        </>
      }
    >
      <p>Import BeerXML or PicoBrew Hub JSON. Recipes are added as new library entries.</p>
      <div className="form-grid">
        <Field label="Format">
          <select value={format} onChange={(event) => setFormat(event.target.value)}>
            <option value="beerxml">BeerXML (.xml)</option>
            <option value="json">Recipe JSON (.json)</option>
          </select>
        </Field>
        <Field label="Choose a file">
          <input
            type="file"
            accept=".xml,.json,application/json,application/xml,text/xml"
            onChange={async (event) => {
              const file = event.target.files?.[0]
              if (file) {
                setFormat(file.name.toLowerCase().endsWith('.json') ? 'json' : 'beerxml')
                setData(await file.text())
              }
            }}
          />
        </Field>
      </div>
      <Field label="Or paste the recipe">
        <textarea
          className="code-input"
          rows={10}
          value={data}
          onChange={(event) => setData(event.target.value)}
          placeholder={format === 'beerxml' ? '<RECIPES>…</RECIPES>' : '{"name": "Your recipe", …}'}
        />
      </Field>
      <ErrorBanner message={error} />
    </Modal>
  )
}
function ShoppingList({ ids, onClose }: { ids: string[]; onClose: () => void }) {
  const resource = useResource<Ingredient[]>(
    `/shopping-list?recipes=${encodeURIComponent(ids.join(','))}`
  )
  return (
    <Modal title="Your brew-day shopping list" onClose={onClose}>
      <p>
        Combined ingredients for {ids.length} selected recipes. Different units remain separate.
      </p>
      {resource.loading ? (
        <Loading />
      ) : resource.error ? (
        <ErrorBanner message={resource.error} />
      ) : (
        <table className="data-table">
          <thead>
            <tr>
              <th>Ingredient</th>
              <th>Kind</th>
              <th>Amount</th>
            </tr>
          </thead>
          <tbody>
            {resource.data?.map((item, index) => (
              <tr key={index}>
                <td>{item.name}</td>
                <td>{item.kind}</td>
                <td>{formatQuantity(item.amount, item.unit)}</td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </Modal>
  )
}
function RevisionDialog({
  id,
  onClose,
  onLoad,
}: {
  id: string
  onClose: () => void
  onLoad: (recipe: Recipe) => void
}) {
  const resource = useResource<Recipe[]>(`/recipes/${id}/revisions`)
  return (
    <Modal title="Recipe revision history" onClose={onClose}>
      <p>Load a prior version into the editor, then save it as a new revision.</p>
      {resource.loading ? (
        <Loading />
      ) : resource.error ? (
        <ErrorBanner message={resource.error} />
      ) : (
        <div className="revision-list">
          {resource.data?.map((recipe) => (
            <div key={recipe.revision}>
              <div>
                <strong>Revision {recipe.revision}</strong>
                <small>
                  {dateLabel(recipe.updated_at)} · {recipe.name}
                </small>
              </div>
              <Button variant="secondary" onClick={() => onLoad(recipe)}>
                Load revision
              </Button>
            </div>
          ))}
        </div>
      )}
    </Modal>
  )
}
