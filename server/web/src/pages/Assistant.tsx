import { useState } from 'react'
import { Sparkles, ArrowRight, ArrowUpRight, Loader2, Lightbulb } from 'lucide-react'
import { useHub, useResource } from '../context'
import { api, errorMessage } from '../lib/api'
import { dateLabel } from '../lib/utils'
import type { AIAdvice, AISettings, AIResult } from '../types'
import { Badge, Button, ErrorBanner, Field, PageTitle, SectionHeading } from '../components/ui'
const prompts: Record<string, string[]> = {
  recipe: [
    'A balanced citrus pale ale for a 5-liter batch.',
    'Build a rich porter with chocolate notes and a dry finish.',
    'Help me adapt a low-alcohol recipe without losing body.',
  ],
  live: [
    'Review the current temperatures and phase progression.',
    'Are there signs of unstable heating or an unusual temperature trend?',
    'Explain what is happening in this stage of the brew.',
  ],
  analysis: [
    'Analyze this brew and suggest improvements for the next batch.',
    'Compare target and measured temperatures across the session.',
    'What should I note about phase transitions and heating behavior?',
  ],
}
export default function Assistant() {
  const recipeID = new URLSearchParams(location.hash.split('?')[1] || '').get('recipe') || ''
  return <AIWorkbench mode="recipe" recipeID={recipeID} />
}

export function AIWorkbench({
  mode,
  deviceID,
  brewID,
  recipeID: initialRecipeID = '',
  contextName,
  embedded = false,
  liveVerified = true,
  initialAdvice,
}: {
  mode: 'recipe' | 'live' | 'analysis'
  deviceID?: string
  brewID?: string
  recipeID?: string
  contextName?: string
  embedded?: boolean
  liveVerified?: boolean
  initialAdvice?: AIAdvice[]
}) {
  const hub = useHub()
  const [prompt, setPrompt] = useState('')
  const [recipeID, setRecipeID] = useState(initialRecipeID)
  const [busy, setBusy] = useState(false)
  const [result, setResult] = useState<AIResult | null>(null)
  const [error, setError] = useState('')
  const [adviceLimit, setAdviceLimit] = useState(8)
  const settings = useResource<AISettings>('/ai/settings')
  const adviceQuery = new URLSearchParams()
  if (mode === 'recipe') adviceQuery.set('mode', 'recipe')
  if (brewID) adviceQuery.set('brew_id', brewID)
  if (deviceID) adviceQuery.set('device_id', deviceID)
  if (mode === 'live') adviceQuery.set('mode', 'live')
  const advice = useResource<AIAdvice[]>(
    mode === 'recipe' || brewID ? `/ai/advice?${adviceQuery}` : null,
    30000
  )
  const contextAdvice = (advice.data ?? initialAdvice ?? []).filter((item) =>
    mode === 'recipe'
      ? item.mode === 'recipe'
      : item.brew_id === brewID &&
        (!deviceID || item.device_id === deviceID) &&
        (item.mode === 'live' || (mode === 'analysis' && item.mode === 'analysis'))
  )
  async function submit() {
    if (mode === 'live' && !liveVerified) return
    setBusy(true)
    setError('')
    try {
      setResult(
        await api<AIResult>('/ai/assist', {
          method: 'POST',
          body: {
            mode,
            prompt,
            ...(mode === 'recipe' && recipeID ? { recipe_id: recipeID } : {}),
            ...(mode === 'live' ? { device_id: deviceID } : {}),
            ...(mode !== 'recipe' && brewID ? { brew_id: brewID } : {}),
          },
        })
      )
      advice.reload()
    } catch (error) {
      setError(errorMessage(error))
    } finally {
      setBusy(false)
    }
  }
  const configuration = settings.data
  const authMode = configuration?.auth_mode || 'api_key'
  const enabled = Boolean(
    configuration?.enabled &&
    configuration.model?.trim() &&
    (authMode === 'subscription' || (authMode === 'api_key' && configuration.key_configured))
  )
  const title =
    mode === 'recipe'
      ? 'Create a recipe with AI.'
      : mode === 'live'
        ? 'AI help during this brew'
        : 'AI brew analysis'
  const description =
    mode === 'recipe'
      ? 'Describe your idea, then review the draft in your recipe library.'
      : mode === 'live'
        ? liveVerified
          ? `Ask about ${contextName || 'this brew'} using its current machine readings and recorded session.`
          : 'Saved observations for the last verified brew remain available. Live questions are paused until the current session can be verified.'
        : `Review ${contextName || 'this brew'} using its recorded temperatures, phase changes and saved notes.`
  const provider = settings.data && (
    <Badge tone={enabled ? 'green' : 'neutral'}>
      {enabled ? `${settings.data.provider} · ${settings.data.model}` : 'Not configured'}
    </Badge>
  )
  return (
    <section className="contextual-ai" aria-label={title}>
      {embedded ? (
        <SectionHeading title={title} description={description} />
      ) : (
        <PageTitle
          eyebrow="A SECOND PERSPECTIVE ON YOUR CRAFT"
          title={title}
          description={description}
        />
      )}
      {!settings.loading && !enabled && (
        <div className="assistant-setup">
          <span className="assistant-symbol">
            <Sparkles size={25} />
          </span>
          <div>
            <h3>Bring your own brewing companion.</h3>
            <p>
              Connect a ChatGPT or Claude subscription, or add a provider API key, in Settings.
              Choose a model and save with the brew assistant enabled. Your Hub works fully without
              it.
            </p>
          </div>
          <Button variant="secondary" onClick={() => hub.navigate('/settings')}>
            Configure assistant
            <ArrowUpRight size={16} />
          </Button>
        </div>
      )}
      <div className="assistant-layout">
        <div>
          <section className="panel assistant-compose">
            <SectionHeading
              title={mode === 'recipe' ? 'Shape your recipe' : 'Ask about this session'}
            />
            {mode === 'recipe' && (
              <Field label="Existing recipe to build on (optional)">
                <select value={recipeID} onChange={(event) => setRecipeID(event.target.value)}>
                  <option value="">No recipe selected</option>
                  {hub.recipes.map((recipe) => (
                    <option key={recipe.id} value={recipe.id}>
                      {recipe.name}
                    </option>
                  ))}
                </select>
              </Field>
            )}
            <Field label="What would you like to explore?">
              <textarea
                rows={6}
                value={prompt}
                onChange={(event) => setPrompt(event.target.value)}
                placeholder={
                  mode === 'recipe'
                    ? 'Describe the style, batch size, ingredients or character you have in mind…'
                    : mode === 'live'
                      ? 'Ask about the current brew. Recent readings are included as context.'
                      : 'What would you like to understand about this brew?'
                }
              />
            </Field>
            <ErrorBanner message={error || settings.error} />
            <div className="assistant-compose-footer">
              <div className="assistant-compose-context">
                {provider}
                <small>Responses may take a moment.</small>
              </div>
              <Button
                disabled={
                  !enabled ||
                  !prompt.trim() ||
                  busy ||
                  (mode === 'live' && !liveVerified) ||
                  (mode === 'live' && (!deviceID || !brewID)) ||
                  (mode === 'analysis' && !brewID)
                }
                onClick={() => void submit()}
              >
                {busy ? <Loader2 size={16} className="spin" /> : <Sparkles size={16} />}{' '}
                {busy ? 'Thinking through your brew…' : 'Ask the assistant'}
              </Button>
            </div>
          </section>
          {result && (
            <section className="panel assistant-response">
              <SectionHeading
                title="A perspective to brew on."
                actions={<Badge tone="copper">AI SUGGESTION</Badge>}
              />
              <div className="assistant-response-body">
                {result.text.split(/\n\n+/).map((paragraph, index) => (
                  <p key={index}>{paragraph.replace(/^#{1,6}\s+/, '')}</p>
                ))}
              </div>
              {mode === 'recipe' && result.recipe && (
                <div className="ai-recipe-draft">
                  <div>
                    <p className="eyebrow">RECIPE DRAFT</p>
                    <h3>{result.recipe.name}</h3>
                    <p>
                      {result.recipe.style} · {result.recipe.batch_liters} L ·{' '}
                      {result.recipe.ingredients?.length || 0} ingredients ·{' '}
                      {result.recipe.steps?.length || 0} steps
                    </p>
                  </div>
                  <Button
                    onClick={() => {
                      hub.setDraft({ ...result.recipe!, id: '', revision: 0 })
                      hub.navigate('/recipes/new')
                    }}
                  >
                    Review in recipe editor
                    <ArrowRight size={16} />
                  </Button>
                </div>
              )}
              {result.limitations?.length > 0 && (
                <div className="ai-limitations">
                  <h4>Keep in mind</h4>
                  <ul>
                    {result.limitations.map((limitation, index) => (
                      <li key={index}>{limitation}</li>
                    ))}
                  </ul>
                </div>
              )}
            </section>
          )}
          <ErrorBanner message={advice.error} retry={advice.reload} />
          {!!contextAdvice.length && (
            <section className="panel advice-feed">
              <SectionHeading
                title={mode === 'recipe' ? 'Previous recipe ideas' : 'Advice saved with this brew'}
                description={
                  mode === 'recipe'
                    ? 'Recent recipe ideas retained by the Hub.'
                    : 'Observations and analysis for this session only.'
                }
              />
              {contextAdvice.slice(0, adviceLimit).map((item, index) => (
                <details key={item.id} open={mode !== 'recipe' && index === 0}>
                  <summary>
                    <span>
                      <strong>
                        {item.mode === 'live'
                          ? 'Live brew observation'
                          : item.mode === 'recipe'
                            ? 'Recipe exploration'
                            : 'Brew analysis'}
                      </strong>
                      <small>{dateLabel(item.created_at)}</small>
                    </span>
                    <Badge tone={item.status === 'error' ? 'amber' : 'green'}>
                      {item.status === 'error' ? 'Unavailable' : 'Advice'}
                    </Badge>
                  </summary>
                  <p>{item.status === 'error' ? item.error : item.text}</p>
                  {!!item.limitations?.length && (
                    <div className="ai-limitations">
                      <h4>Saved qualifications</h4>
                      <ul>
                        {item.limitations.map((limitation, index) => (
                          <li key={index}>{limitation}</li>
                        ))}
                      </ul>
                    </div>
                  )}
                </details>
              ))}
              {contextAdvice.length > adviceLimit && (
                <Button variant="secondary" onClick={() => setAdviceLimit(contextAdvice.length)}>
                  Show all {contextAdvice.length} saved{' '}
                  {mode === 'recipe' ? 'ideas' : 'observations and analyses'}
                </Button>
              )}
            </section>
          )}
        </div>
        <aside className="assistant-aside">
          <div className="assistant-note">
            <Lightbulb size={27} strokeWidth={1.3} />
            <h3>A useful place to start.</h3>
            <div className="prompt-suggestions">
              {prompts[mode].map((suggestion) => (
                <button key={suggestion} onClick={() => setPrompt(suggestion)}>
                  {suggestion}
                  <ArrowUpRight size={14} />
                </button>
              ))}
            </div>
          </div>
          <div className="assistant-boundaries">
            <h3>You stay at the controls.</h3>
            <p>
              The assistant can suggest recipes and observations. It cannot start programs, move the
              dial or change heating and pumping.
            </p>
            {mode === 'recipe' && (
              <p>
                Review the draft in the recipe editor, save it, and preview its machine adaptation
                before sending it.
              </p>
            )}
          </div>
        </aside>
      </div>
    </section>
  )
}
