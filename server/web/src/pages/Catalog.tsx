import { OriginalRecipeLink } from '../components/OriginalRecipe'
import { MachineVersions } from '../components/MachineVersions'
import { ArchivedIngredients } from '../components/ArchivedIngredients'
import { modelLabel } from '../lib/utils'
import { useEffect, useState } from 'react'
import { ArrowDownToLine, ArrowLeft, ArrowRight, Copy, Library, Search, Wheat } from 'lucide-react'
import { useHub, useResource } from '../context'
import { api, download, errorMessage } from '../lib/api'
import { catalogAssetURL, externalURL, kindLabel } from '../lib/programs'
import { formatNumber, formatQuantity, formatTemperature, formatVolume } from '../lib/recipeDisplay'
import { referenceDocument, referenceSourceURL } from '../lib/referenceLinks'
import type {
  CatalogDetail,
  CatalogSummary,
  CatalogPage,
  CatalogReferences,
  CatalogSource,
  Recipe,
} from '../types'
import {
  Badge,
  Button,
  Empty,
  ErrorBanner,
  Field,
  Loading,
  Modal,
  PageTitle,
  SearchBox,
  SectionHeading,
} from '../components/ui'
import { SourceEvidence } from '../components/SourceEvidence'
import { ProgramTable } from '../components/Program'
import { ReferenceLibrary } from '../components/ReferenceViewer'
import './Catalog.scss'

function entryLabel(entry: CatalogSummary) {
  if (entry.kind === 'maintenance') return 'Cleaning program'
  if (entry.source_id === 'picopak-library') return 'Archived PicoPak schedule'
  return entry.kind === 'native-program' ? 'Archived brew program' : 'Ingredient recipe'
}

const catalogQuery = () => {
  const query = location.hash.split('?')[1]
  return query ? `?${query}` : ''
}

export default function Catalog({ id }: { id?: string }) {
  return id ? <CatalogEntry id={id} /> : <CatalogBrowse />
}
function CatalogBrowse() {
  const hub = useHub()
  const params = new URLSearchParams(location.hash.split('?')[1] || '')
  const [query, setQuery] = useState(params.get('q') || '')
  const [debounced, setDebounced] = useState(query)
  const [source, setSource] = useState(params.get('source') || '')
  const [kind, setKind] = useState(() => {
    const requested = params.get('kind') || 'recipe'
    return ['recipe', 'archive', 'reviewed', 'native-program', 'maintenance'].includes(requested)
      ? requested
      : 'recipe'
  })
  const [model, setModel] = useState(params.get('model') || '')
  const [style, setStyle] = useState(params.get('style') || '')
  const [offset, setOffset] = useState(0)
  const [sourcesOpen, setSourcesOpen] = useState(false)
  const [referencesOpen, setReferencesOpen] = useState(false)
  useEffect(() => {
    const timer = setTimeout(() => setDebounced(query), 200)
    return () => clearTimeout(timer)
  }, [query])
  const archive = ['archive', 'reviewed', 'native-program', 'maintenance'].includes(kind)
  const filters = new URLSearchParams({
    q: debounced,
    source: archive ? source : '',
    kind: archive ? kind : 'all-brewers',
    model: archive ? model : '',
    style,
    offset: String(offset),
    limit: '24',
  })
  const resource = useResource<CatalogPage>(`/catalog?${filters}`)
  function change(update: (value: string) => void, value: string) {
    setOffset(0)
    update(value)
  }
  useEffect(() => {
    // A pending browse effect can run after the user opens a recipe.
    if (location.hash.split('?')[0] !== '#/catalog') return
    const current = new URLSearchParams({ q: query, source, kind, model, style })
    for (const key of [...current.keys()]) if (!current.get(key)) current.delete(key)
    history.replaceState(null, '', `#/catalog${current.size ? '?' + current : ''}`)
  }, [query, source, kind, model, style])
  const page = resource.data
  return (
    <>
      <PageTitle
        title={archive ? 'Source archive' : 'Community recipes'}
        description={
          kind === 'reviewed'
            ? 'Recipes checked against their original records. Open a recipe to see what was recovered and any remaining questions.'
            : archive
              ? 'Original records, including PicoPak schedules and cleaning programs.'
              : 'Recipes with measured ingredients and a brew program for Pico C, S, Pro, Z-series and Zymatic.'
        }
      />
      <div className="catalog-intro panel">
        <div>
          <Library size={27} />
          <div>
            <strong>Bundled with your Hub</strong>
            <p>Recipes and illustrations stay available without the old PicoBrew services.</p>
          </div>
        </div>
        <div className="inline-actions">
          {archive && (
            <Button
              variant="secondary"
              onClick={() => {
                change(setKind, 'recipe')
                setSource('')
              }}
            >
              Back to recipes
            </Button>
          )}
          <Button variant="ghost" onClick={() => setSourcesOpen(true)}>
            Sources & credits
          </Button>
          <Button variant="secondary" onClick={() => setReferencesOpen(true)}>
            Machine guides & artwork
          </Button>
        </div>
      </div>
      <div className="catalog-filters panel">
        <SearchBox
          value={query}
          onChange={(value) => change(setQuery, value)}
          placeholder="Search community recipes"
        />
        <div className={archive ? 'form-grid quarters' : 'form-grid'}>
          {archive && (
            <Field label="Archive record type">
              <select value={kind} onChange={(event) => change(setKind, event.target.value)}>
                <option value="archive">All source records</option>
                <option value="reviewed">Reviewed recipes</option>
                <option value="native-program">Machine schedules</option>
                <option value="maintenance">Cleaning programs</option>
              </select>
            </Field>
          )}
          {archive && (
            <Field label="Source archive">
              <select value={source} onChange={(event) => change(setSource, event.target.value)}>
                <option value="">All archives</option>
                {page?.sources.map((item) => (
                  <option key={item.id} value={item.id}>
                    {item.name}
                  </option>
                ))}
              </select>
            </Field>
          )}
          <Field label={archive ? 'Source machine' : 'Adapt for'}>
            <select value={model} onChange={(event) => change(setModel, event.target.value)}>
              <option value="">All machines</option>
              {['pico-s', 'pico-pro', 'pico-c', 'z', 'zymatic'].map((key) => (
                <option key={key} value={key}>
                  {modelLabel(key)}
                </option>
              ))}
            </select>
          </Field>
          <Field label="Beer style">
            <select value={style} onChange={(event) => change(setStyle, event.target.value)}>
              <option value="">All styles</option>
              {page?.styles.map((value) => (
                <option key={value}>{value}</option>
              ))}
            </select>
          </Field>
        </div>
      </div>
      <ErrorBanner message={resource.error} retry={resource.reload} />
      {resource.loading ? (
        <Loading label="Opening the community catalog…" />
      ) : (
        page && (
          <>
            <div className="list-toolbar">
              <span className="muted" role="status">
                {page.total.toLocaleString()} matching entries
              </span>
            </div>
            {page.items.length ? (
              <div className="recipe-grid catalog-grid">
                {page.items.map((entry, index) => (
                  <article className="recipe-card catalog-card" key={entry.id}>
                    <div className={`recipe-art tone-${index % 3}`}>
                      {catalogAssetURL(entry.image_url) ? (
                        <img
                          src={entry.image_url}
                          alt={`${entry.style || 'Beer'} illustration`}
                          loading="lazy"
                        />
                      ) : (
                        <Wheat size={58} strokeWidth={1.1} />
                      )}
                      <span>
                        {entry.style ||
                          (entry.kind !== 'recipe' && kindLabel(entry.kind, entry.source_model))}
                      </span>
                      {entry.featured && (
                        <strong className="catalog-featured">Featured source</strong>
                      )}
                    </div>
                    <div className="recipe-card-content">
                      {entry.kind !== 'recipe' && (
                        <Badge tone={entry.kind === 'maintenance' ? 'neutral' : 'copper'}>
                          {entryLabel(entry)}
                        </Badge>
                      )}
                      <h3>
                        <button
                          onClick={() =>
                            hub.navigate(
                              `/catalog/${encodeURIComponent(entry.id)}${catalogQuery()}`
                            )
                          }
                        >
                          {entry.name}
                        </button>
                      </h3>
                      {entry.edition && <p>Hub adaptation · {entry.edition}</p>}
                      <p>
                        {entry.description ||
                          (entry.kind === 'native-program'
                            ? 'Original temperature, timing and compartment sequence. Ingredient quantities are not included.'
                            : entry.kind === 'maintenance'
                              ? 'An archived cleaning routine for its source machine.'
                              : 'An archived recipe ready to copy and review.')}
                      </p>
                      <div className="recipe-meta">
                        {entry.abv != null && <span>{formatNumber(entry.abv, 1)}% ABV</span>}
                        {entry.ibu != null && <span>{formatNumber(entry.ibu, 1)} IBU</span>}
                        {archive && <span>{(entry.models || []).map(modelLabel).join(' · ')}</span>}
                      </div>
                      <footer>
                        {entry.kind !== 'recipe' && (
                          <small>
                            {entry.can_deploy ? 'Review before transfer' : 'Reference & planning'}
                          </small>
                        )}
                        <button
                          onClick={() =>
                            hub.navigate(
                              `/catalog/${encodeURIComponent(entry.id)}${catalogQuery()}`
                            )
                          }
                        >
                          View details
                          <ArrowRight size={15} />
                        </button>
                      </footer>
                    </div>
                  </article>
                ))}
              </div>
            ) : (
              <div className="panel">
                <Empty
                  icon={<Search size={26} />}
                  title="No entries match these filters."
                  description="Try a different name, source or style."
                  action={
                    <Button
                      variant="secondary"
                      onClick={() => {
                        setQuery('')
                        setSource('')
                        setKind(archive ? 'archive' : kind)
                        setModel('')
                        setStyle('')
                        setOffset(0)
                      }}
                    >
                      Clear filters
                    </Button>
                  }
                />
              </div>
            )}
            <div className="catalog-pagination">
              <Button
                variant="secondary"
                disabled={offset === 0}
                onClick={() => setOffset(Math.max(0, offset - page.limit))}
              >
                <ArrowLeft size={16} />
                Previous
              </Button>
              <span>
                {page.total
                  ? `${offset + 1}–${Math.min(offset + page.items.length, page.total)} of ${page.total.toLocaleString()}`
                  : 'No results'}
              </span>
              <Button
                variant="secondary"
                disabled={offset + page.items.length >= page.total}
                onClick={() => setOffset(offset + page.limit)}
              >
                Next
                <ArrowRight size={16} />
              </Button>
            </div>
          </>
        )
      )}
      {sourcesOpen && (
        <Sources
          sources={page?.sources || []}
          onClose={() => setSourcesOpen(false)}
          browseArchive={() => {
            setSourcesOpen(false)
            change(setKind, 'archive')
          }}
        />
      )}
      {referencesOpen && (
        <ReferenceLibrary model={model} onClose={() => setReferencesOpen(false)} />
      )}
    </>
  )
}
function CatalogEntry({ id }: { id: string }) {
  const hub = useHub()
  const resource = useResource<CatalogDetail>(`/catalog/${encodeURIComponent(id)}`)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState('')
  const entry = resource.data
  async function copy(stockProgram = false) {
    setBusy(true)
    setError('')
    try {
      const recipe = await api<Recipe>(`/catalog/${encodeURIComponent(id)}/copy`, {
        method: 'POST',
        body: stockProgram ? { stock_program: true } : {},
      })
      await hub.refresh()
      hub.notify('Editable copy added to your library.')
      hub.navigate(`/recipes/${recipe.id}`)
    } catch (error) {
      setError(errorMessage(error))
    } finally {
      setBusy(false)
    }
  }
  if (resource.loading) return <Loading label="Opening the source record…" />
  if (!entry) return <ErrorBanner message={resource.error} retry={resource.reload} />
  const sourceID =
    entry.source_id === 'public-recipes' && /^public-[0-9a-f]{32}$/.test(entry.id)
      ? entry.id
      : undefined
  const review = entry.source_review
  const warnings = (entry.warnings || []).filter(
    (warning) => warning !== review?.evidence && warning !== review?.blocker
  )
  const program = entry.native_program || entry.recipe?.native_program
  return (
    <>
      <PageTitle
        back={() => hub.navigate(`/catalog${catalogQuery()}`)}
        eyebrow={entryLabel(entry).toUpperCase()}
        title={entry.name}
        description={
          entry.description ||
          'An original community archive record. Your editable library copy keeps its source attribution.'
        }
        actions={
          <>
            <Button
              disabled={busy || !entry.can_copy}
              variant={entry.brew_ready ? 'secondary' : 'primary'}
              onClick={() => void copy()}
            >
              <Copy size={16} />
              {busy
                ? 'Copying…'
                : entry.brew_ready
                  ? 'Copy ingredient recipe'
                  : 'Copy to my library'}
            </Button>
            {entry.brew_ready && (
              <Button disabled={busy} onClick={() => void copy(true)}>
                Save Zymatic program
              </Button>
            )}
          </>
        }
      />
      <ErrorBanner message={error || resource.error} />
      <div className="editor-layout catalog-detail">
        <div className="editor-main">
          {review && (
            <section className="panel" aria-label="Source review">
              <SectionHeading
                title={entry.edition ? `Hub adaptation · ${entry.edition}` : 'Source review'}
              />
              <SourceEvidence
                text={review.evidence}
                scaledBill={
                  !!entry.edition &&
                  !!entry.machine_versions?.length &&
                  !!entry.recipe?.ingredients?.length
                }
              />
              {review.blocker && (
                <p className="warning-note">
                  <strong>Still unresolved.</strong> {review.blocker}
                </p>
              )}
            </section>
          )}
          {entry.machine_versions?.length ? (
            <MachineVersions
              versions={entry.machine_versions}
              sourceID={sourceID}
              initialModel={new URLSearchParams(catalogQuery()).get('model') || undefined}
            />
          ) : (
            <section className="panel">
              <SectionHeading
                title={program ? 'The original machine schedule' : 'The original recipe'}
                description={
                  entry.kind === 'recipe'
                    ? 'Review this source recipe before adapting it to your equipment.'
                    : entry.kind === 'maintenance'
                      ? 'Follow this cleaning program’s preparation instructions for its source machine. Cleaning routines are kept separate from beer recipes.'
                      : undefined
                }
              />
              {entry.kind === 'recipe' && entry.marketplace && (
                <p>
                  {entry.marketplace.creator} also offered this beer as a{' '}
                  <a href={entry.marketplace.url} target="_blank" rel="noreferrer">
                    PicoPak
                  </a>
                  . The quantities below are for the published{' '}
                  {formatVolume(entry.recipe?.batch_liters ?? 0)} recipe.
                </p>
              )}
              {entry.kind === 'native-program' && (
                <ArchivedIngredients product={entry.marketplace} />
              )}
              {sourceID && <OriginalRecipeLink id={sourceID} />}
              {program ? (
                <ProgramTable program={program} model={entry.source_model} archived />
              ) : (
                entry.recipe && (
                  <>
                    <p className="catalog-recipe-meta">
                      {formatVolume(entry.recipe.batch_liters)} ·{' '}
                      {formatNumber(entry.recipe.efficiency, 1)}% efficiency ·{' '}
                      {entry.recipe.ingredients?.length || 0} ingredients
                    </p>
                    <div className="catalog-ingredients-scroll">
                      <table className="catalog-ingredients" aria-label="Recipe ingredients">
                        <thead>
                          <tr>
                            <th>Ingredient</th>
                            <th>Amount</th>
                            <th>Use</th>
                          </tr>
                        </thead>
                        <tbody>
                          {entry.recipe.ingredients?.map((ingredient, index) => (
                            <tr key={index}>
                              <td data-label="Ingredient">{ingredient.name}</td>
                              <td data-label="Amount" className="catalog-quantity">
                                {formatQuantity(ingredient.amount, ingredient.unit)}
                              </td>
                              <td data-label="Use">{ingredient.use}</td>
                            </tr>
                          ))}
                        </tbody>
                      </table>
                    </div>
                    <ol className="catalog-schedule">
                      {entry.recipe.steps?.map((step, index) => (
                        <li key={index}>
                          <strong>{step.name}</strong>
                          <span>
                            {formatTemperature(step.target_c)} · {formatNumber(step.minutes)} min
                            {step.drain_minutes
                              ? ` · drain ${formatNumber(step.drain_minutes)} min`
                              : ''}
                          </span>
                        </li>
                      ))}
                    </ol>
                  </>
                )
              )}
              {!entry.can_copy && (
                <p className="warning-note">
                  This source record needs further conversion before it can become an editable
                  recipe. The original remains available below.
                </p>
              )}
            </section>
          )}
          {entry.brew_sheet && entry.stock_program && (
            <section className="panel">
              <SectionHeading
                title="Brew on a stock Zymatic"
                description="Save the Zymatic program to send this schedule from your library. The loading sheet goes with it."
              />
              <ol>
                {entry.brew_sheet.instructions.map((line, i) => (
                  <li key={i}>{line}</li>
                ))}
              </ol>
              <a href={entry.brew_sheet.yeast_guide} target="_blank" rel="noreferrer">
                Manufacturer’s pitching guide
              </a>
              <ProgramTable program={entry.stock_program} model="zymatic" archived />
            </section>
          )}
          {(entry.notes || warnings.length > 0) && (
            <section className="panel">
              <SectionHeading title="Source notes" />
              {entry.notes && <p className="preserve-lines">{entry.notes}</p>}
              {warnings.length > 0 && (
                <ul className="warning-list">
                  {warnings.map((warning, index) => (
                    <li key={index}>{warning}</li>
                  ))}
                </ul>
              )}
            </section>
          )}
          <CatalogCredits entry={entry} onError={setError} />
        </div>
        <aside className="editor-aside">
          <div className="recipe-summary">
            {catalogAssetURL(entry.image_url) ? (
              <img
                className="catalog-detail-image"
                src={entry.image_url}
                alt={`${entry.style || 'Beer'} illustration`}
              />
            ) : (
              <Wheat size={50} />
            )}
            <Badge tone="copper">{entryLabel(entry)}</Badge>
            <h3>{entry.style || entry.name}</h3>
            <dl>
              <div>
                <dt>Source machine</dt>
                <dd>{modelLabel(entry.source_model)}</dd>
              </div>
              {entry.abv != null && (
                <div>
                  <dt>ABV</dt>
                  <dd>{formatNumber(entry.abv, 1)}%</dd>
                </div>
              )}
              {entry.brew_sheet?.original_gravity && (
                <div>
                  <dt>Original gravity</dt>
                  <dd>{entry.brew_sheet.original_gravity.toFixed(3)}</dd>
                </div>
              )}
              {entry.brew_sheet?.final_gravity && (
                <div>
                  <dt>Final gravity</dt>
                  <dd>{entry.brew_sheet.final_gravity.toFixed(3)}</dd>
                </div>
              )}
              {entry.srm != null && (
                <div>
                  <dt>Color</dt>
                  <dd>{formatNumber(entry.srm, 1)} SRM</dd>
                </div>
              )}
              {entry.ibu != null && (
                <div>
                  <dt>IBU</dt>
                  <dd>{formatNumber(entry.ibu, 1)}</dd>
                </div>
              )}
            </dl>
            <p>
              {entry.brew_ready
                ? 'Matched to the original stock Zymatic schedule.'
                : entry.kind === 'recipe'
                  ? null
                  : entry.can_deploy
                    ? `Transfer support: ${entry.deploy_models.map(modelLabel).join(', ')}. The preview checks the connected machine and its available slots.`
                    : 'Available for reference and planning. Transfer compatibility is checked when you prepare a preview.'}
            </p>
          </div>
        </aside>
      </div>
    </>
  )
}
function Sources({
  sources,
  onClose,
  browseArchive,
}: {
  sources: CatalogSource[]
  onClose: () => void
  browseArchive: () => void
}) {
  return (
    <Modal title="Catalog sources & credits" onClose={onClose} wide>
      <p>
        Source records are bundled at the revisions shown here. Copying a recipe creates an
        independent library entry; it keeps the source attribution.
      </p>
      <Button variant="secondary" onClick={browseArchive}>
        Browse source archive
      </Button>
      {sources.map((source) => (
        <section className="catalog-source" key={source.id}>
          <h3>{source.name}</h3>
          <p>{source.notes}</p>
          <dl>
            <div>
              <dt>Catalog entries</dt>
              <dd>{source.entry_count.toLocaleString()}</dd>
            </div>
            <div>
              <dt>Original records</dt>
              <dd>{source.record_count.toLocaleString()}</dd>
            </div>
            <div>
              <dt>Excluded records</dt>
              <dd>{source.excluded_count.toLocaleString()}</dd>
            </div>
            <div>
              <dt>Attribution</dt>
              <dd>{source.attribution}</dd>
            </div>
            <div>
              <dt>License</dt>
              <dd>{source.license}</dd>
            </div>
          </dl>
          <small>
            Revision <code>{source.commit}</code>
          </small>
          {externalURL(source.repository) && (
            <p>
              <a href={externalURL(source.repository)} target="_blank" rel="noreferrer">
                Open source repository
              </a>
            </p>
          )}
        </section>
      ))}
    </Modal>
  )
}
function CatalogCredits({
  entry,
  onError,
}: {
  entry: CatalogDetail
  onError: (message: string) => void
}) {
  const [open, setOpen] = useState(false)
  const references = useResource<CatalogReferences>(
    open && entry.image_url ? '/catalog/references' : null
  )
  const asset = references.data?.assets.find((asset) => asset.url === entry.image_url)
  const imageSource =
    asset && references.data
      ? referenceSourceURL(referenceDocument(asset, references.data))
      : undefined
  const source = externalURL(entry.provenance.source_url || entry.source_url)
  return (
    <details
      className="panel catalog-credits"
      onToggle={(event) => setOpen(event.currentTarget.open)}
    >
      <summary>Sources & credits</summary>
      <dl>
        {entry.author && (
          <div>
            <dt>Recipe creator</dt>
            <dd>{entry.author}</dd>
          </div>
        )}
        <div>
          <dt>Recipe archive</dt>
          <dd>{entry.provenance.source_id}</dd>
        </div>
        <div>
          <dt>Recipe license</dt>
          <dd>{entry.provenance.license}</dd>
        </div>
        <div>
          <dt>Source revision</dt>
          <dd>
            <code>{entry.provenance.commit}</code>
          </dd>
        </div>
        <div>
          <dt>Original file</dt>
          <dd>
            {source ? (
              <a href={source} target="_blank" rel="noopener noreferrer">
                {entry.provenance.path}
              </a>
            ) : (
              entry.provenance.path
            )}
          </dd>
        </div>
        <div>
          <dt>Source SHA-256</dt>
          <dd>
            <code>{entry.provenance.sha256}</code>
          </dd>
        </div>
        {asset && (
          <>
            <div>
              <dt>Illustration credit</dt>
              <dd>{asset.attribution}</dd>
            </div>
            <div>
              <dt>Illustration license</dt>
              <dd>{asset.license}</dd>
            </div>
            {imageSource && (
              <div>
                <dt>Illustration source</dt>
                <dd>
                  <a href={imageSource} target="_blank" rel="noopener noreferrer">
                    View original artwork
                  </a>
                </dd>
              </div>
            )}
          </>
        )}
      </dl>
      <ErrorBanner message={references.error} retry={references.reload} />
      {entry.provenance.warnings?.length > 0 && (
        <ul className="warning-list">
          {entry.provenance.warnings.map((warning, index) => (
            <li key={index}>{warning}</li>
          ))}
        </ul>
      )}
      <h3>Original records</h3>
      <p>
        {entry.source_record_count.toLocaleString()} source record
        {entry.source_record_count === 1 ? '' : 's'} retained. Downloads preserve the original
        precision and source data.
      </p>
      <div className="catalog-originals">
        {entry.source_records.map((record) => (
          <Button
            key={record.id}
            variant="secondary"
            onClick={async () => {
              try {
                await download(
                  `/catalog/${encodeURIComponent(entry.id)}/source?record=${encodeURIComponent(record.id)}`,
                  `${entry.id}-${record.id}.json`
                )
              } catch (error) {
                onError(errorMessage(error))
              }
            }}
          >
            <ArrowDownToLine size={16} />
            Record {record.id}
          </Button>
        ))}
      </div>
    </details>
  )
}
