import { ConditioningFields } from '../components/FermentationPlan'
import { ArchivedIngredients } from '../components/ArchivedIngredients'
import { RecipeStyleField } from '../components/RecipeStyleField'
import { BeerCrafter } from '../components/BeerCrafter'
import { useEffect, useState } from 'react'
import { Copy, Download, History, Save, Send, Trash2 } from 'lucide-react'
import type { CatalogDetail, Recipe } from '../types'
import { useHub, useResource } from '../context'
import { api, download, errorMessage } from '../lib/api'
import { copyProgram, kindLabel, programTiming } from '../lib/programs'
import { dateLabel, modelLabel, slug } from '../lib/utils'
import {
  Button,
  Confirm,
  ErrorBanner,
  Field,
  Loading,
  Modal,
  PageTitle,
  SectionHeading,
} from '../components/ui'
import { ProgramFields, Provenance } from '../components/Program'
import { TransferDialog } from '../components/TransferDialog'

export default function NativeRecipeEditor({
  initial,
  isNew,
}: {
  initial: Recipe
  isNew: boolean
}) {
  const hub = useHub()
  const [recipe, setRecipe] = useState<Recipe>(() => ({
    ...initial,
    tags: initial.tags || [],
    native_program: initial.native_program
      ? copyProgram(initial.native_program)
      : { id: '', name: initial.name, steps: [] },
  }))
  const [dirty, setDirty] = useState(isNew)
  const [tags, setTags] = useState((initial.tags || []).join(', '))
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState('')
  const [transfer, setTransfer] = useState(false)
  const [revisions, setRevisions] = useState(false)
  const [remove, setRemove] = useState(false)
  const catalogID = recipe.provenance?.catalog_id
  const original = useResource<CatalogDetail>(
    catalogID ? '/catalog/' + encodeURIComponent(catalogID) : null
  )
  const program = recipe.native_program!
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
  async function save() {
    setBusy(true)
    setError('')
    try {
      const saved = await api<Recipe>(isNew ? '/recipes' : `/recipes/${recipe.id}`, {
        method: isNew ? 'POST' : 'PUT',
        body: {
          ...recipe,
          native_program: { ...program, name: recipe.name },
          tags: tags
            .split(',')
            .map((tag) => tag.trim())
            .filter(Boolean),
        },
      })
      setRecipe(saved)
      setDirty(false)
      await hub.refresh()
      hub.notify(`${kindLabel(saved.kind, saved.source_model)} saved to your library.`)
      if (isNew) hub.navigate(`/recipes/${saved.id}`)
    } catch (error) {
      setError(errorMessage(error))
    } finally {
      setBusy(false)
    }
  }
  return (
    <>
      <PageTitle
        back={() => {
          if (!dirty || window.confirm('Leave this program without saving your changes?'))
            hub.navigate('/recipes')
        }}
        eyebrow={`${kindLabel(recipe.kind, recipe.source_model).toUpperCase()} · ${isNew ? 'NEW COPY' : `REVISION ${recipe.revision}`}${dirty ? ' · UNSAVED CHANGES' : ''}`}
        title={recipe.name || 'Untitled program'}
        description="Preview a transfer to check the brewer’s firmware and available recipe slots."
        actions={
          <>
            {!isNew && (
              <Button
                variant="secondary"
                disabled={dirty || busy}
                onClick={() => setTransfer(true)}
              >
                <Send size={16} />
                Send
              </Button>
            )}
            <Button disabled={busy} onClick={() => void save()}>
              <Save size={16} />
              {busy ? 'Saving…' : 'Save program'}
            </Button>
          </>
        }
      />
      <ErrorBanner message={error} />
      <dl className="program-overview" aria-label="Program summary">
        <div>
          <dt>Source machine</dt>
          <dd>{modelLabel(recipe.source_model)}</dd>
        </div>
        <div>
          <dt>Steps</dt>
          <dd>{program.steps.length}</dd>
        </div>
        <div>
          <dt>Schedule</dt>
          <dd>{programTiming(program)}</dd>
        </div>
      </dl>
      <div className={isNew ? undefined : 'editor-layout'}>
        <div className="editor-main">
          <BeerCrafter recipe={recipe} />
          {recipe.kind !== 'maintenance' && <ConditioningFields recipe={recipe} change={update} />}
          <section className="panel">
            <SectionHeading
              title={recipe.kind === 'maintenance' ? 'Program details' : 'Recipe details'}
            />
            <div className="form-grid">
              <Field label="Program name">
                <input
                  value={recipe.name}
                  maxLength={100}
                  onChange={(event) =>
                    update({
                      name: event.target.value,
                      native_program: { ...program, name: event.target.value },
                    })
                  }
                />
              </Field>
              <RecipeStyleField value={recipe.style} change={(style) => update({ style })} />
            </div>
            <Field label="Description">
              <textarea
                rows={2}
                value={recipe.description}
                onChange={(event) => update({ description: event.target.value })}
              />
            </Field>
            <div className="form-grid">
              <Field label="Program type">
                <select
                  value={recipe.kind}
                  onChange={(event) => update({ kind: event.target.value })}
                >
                  <option value="native-program">
                    {kindLabel('native-program', recipe.source_model)}
                  </option>
                  <option value="maintenance">Cleaning program</option>
                </select>
              </Field>
              <Field
                label="Source machine"
                hint="Flow paths follow the original program for this machine."
              >
                <input readOnly value={modelLabel(recipe.source_model)} />
              </Field>
            </div>
            <Field label="Tags">
              <input
                value={tags}
                onChange={(event) => {
                  setTags(event.target.value)
                  setDirty(true)
                }}
              />
            </Field>
            {recipe.kind !== 'maintenance' && !original.loading && !original.data?.brew_sheet && (
              <ArchivedIngredients product={original.data?.marketplace} />
            )}
          </section>
          <section className="panel">
            <ProgramFields
              title={kindLabel(recipe.kind, recipe.source_model)}
              model={recipe.source_model}
              archived
              program={program}
              onChange={(value) => update({ native_program: value })}
            />
          </section>
          <section className="panel">
            <SectionHeading title="Preparation & notes" />
            <Field label="Program notes">
              <textarea
                rows={5}
                value={recipe.notes}
                onChange={(event) => update({ notes: event.target.value })}
              />
            </Field>
            <Provenance value={recipe.provenance} />
          </section>
        </div>
        <aside className="editor-aside">
          {!isNew && (
            <div className="editor-tools">
              <Button
                variant="ghost"
                onClick={async () => {
                  try {
                    await download(
                      `/recipes/${recipe.id}/export?format=json`,
                      `${slug(recipe.name)}.json`
                    )
                  } catch (error) {
                    setError(errorMessage(error))
                  }
                }}
              >
                <Download size={15} />
                Export program JSON
              </Button>
              <p className="help-text">
                Machine-only programs use JSON. BeerXML export requires an ingredient recipe.
              </p>
              <Button variant="ghost" onClick={() => setRevisions(true)}>
                <History size={15} />
                Revision history
              </Button>
              <Button
                variant="ghost"
                onClick={() => {
                  const name = `${recipe.name} copy`
                  hub.setDraft({
                    ...recipe,
                    id: '',
                    revision: 0,
                    name,
                    native_program: { ...copyProgram(program), name },
                  })
                  hub.navigate('/recipes/new')
                }}
              >
                <Copy size={15} />
                Duplicate program
              </Button>
              <Button variant="ghost" className="danger-text" onClick={() => setRemove(true)}>
                <Trash2 size={15} />
                Delete program
              </Button>
            </div>
          )}
        </aside>
      </div>
      {transfer && (
        <TransferDialog
          recipes={[recipe]}
          initialSelection={[recipe.id]}
          onClose={() => setTransfer(false)}
        />
      )}
      {revisions && (
        <ProgramRevisions
          id={recipe.id}
          onClose={() => setRevisions(false)}
          onLoad={(old) => {
            setRecipe({ ...old, id: recipe.id, revision: recipe.revision })
            setTags((old.tags || []).join(', '))
            setDirty(true)
            setRevisions(false)
          }}
        />
      )}
      {remove && (
        <Confirm
          title="Delete this library program?"
          description="Saved copies on your machines and existing brew history remain available."
          label="Delete program"
          danger
          busy={busy}
          onClose={() => setRemove(false)}
          onConfirm={async () => {
            setBusy(true)
            try {
              await api(`/recipes/${recipe.id}`, { method: 'DELETE' })
              await hub.refresh()
              hub.navigate('/recipes')
            } catch (error) {
              setError(errorMessage(error))
              setRemove(false)
            } finally {
              setBusy(false)
            }
          }}
        />
      )}
    </>
  )
}
function ProgramRevisions({
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
    <Modal title="Program revision history" onClose={onClose}>
      <p>Load a previous version, then save it as a new revision.</p>
      <ErrorBanner message={resource.error} />
      {resource.loading ? (
        <Loading />
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
