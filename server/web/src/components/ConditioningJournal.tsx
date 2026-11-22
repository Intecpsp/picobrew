import { useState } from 'react'
import { useHub } from '../context'
import { api, errorMessage } from '../lib/api'
import { dateLabel, temperature } from '../lib/utils'
import type { BrewDetail, Conditioning } from '../types'
import { Button, Field, SectionHeading, ErrorBanner } from './ui'

export function ConditioningJournal({
  brew,
  update,
  reload,
}: {
  brew: BrewDetail
  update: (value: Conditioning) => void
  reload: () => void
}) {
  const { recipes, unit } = useHub()
  const plan = brew.conditioning
  const stages = plan?.stages ?? []
  const choices = recipes.filter(
    (r) =>
      r.fermentation?.length ||
      r.carbonation?.method ||
      r.carbonation?.volumes ||
      r.carbonation?.temperature_c !== undefined
  )
  const [recipeId, choose] = useState('')
  const [name, setName] = useState('')
  const [when, setWhen] = useState('')
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState('')
  async function record(action: string, fields: Record<string, unknown> = {}) {
    setBusy(true)
    setError('')
    try {
      const result = await api<Conditioning>(
        '/brews/' + encodeURIComponent(brew.id) + '/conditioning',
        {
          method: 'POST',
          body: {
            revision: plan?.revision ?? 0,
            action,
            ...fields,
            ...(when && (action === 'start' || action === 'finish')
              ? { at: new Date(when).toISOString() }
              : {}),
          },
        }
      )
      update(result)
      setName('')
      setWhen('')
    } catch (e) {
      setError(errorMessage(e))
      reload()
    } finally {
      setBusy(false)
    }
  }
  return (
    <section className="panel">
      <SectionHeading
        title="Fermentation and carbonation"
        description={
          plan?.recipe_name
            ? plan.recipe_name + ' · Recipe revision ' + plan.recipe_revision
            : 'Track the stages you carry out after brewing.'
        }
      />
      <ErrorBanner message={error} />
      {!stages.length && choices.length > 0 && (
        <div className="form-grid">
          <Field label="Use a recipe plan">
            <select value={recipeId} onChange={(e) => choose(e.target.value)}>
              <option value="">Choose a recipe</option>
              {choices.map((r) => (
                <option key={r.id} value={r.id}>
                  {r.name}
                </option>
              ))}
            </select>
          </Field>
          <Button
            variant="secondary"
            disabled={busy || !recipeId}
            onClick={() =>
              void record('use_recipe', {
                recipe_id: recipeId,
                recipe_revision: choices.find((r) => r.id === recipeId)?.revision,
              })
            }
          >
            Use plan
          </Button>
        </div>
      )}
      {!!stages.length && (
        <>
          <div className="table-scroll">
            <table className="data-table">
              <thead>
                <tr>
                  <th>Stage</th>
                  <th>Target</th>
                  <th>Started</th>
                  <th>Completed</th>
                  <th />
                </tr>
              </thead>
              <tbody>
                {stages.map((s, i) => (
                  <tr key={i}>
                    <td>{s.name}</td>
                    <td>
                      {[
                        s.target_c !== undefined && temperature(s.target_c * 1.8 + 32, unit),
                        s.days && s.days + ' days',
                        s.volumes && s.volumes + ' volumes CO₂',
                      ]
                        .filter(Boolean)
                        .join(' · ') || '—'}
                    </td>
                    <td>{s.started_at ? dateLabel(s.started_at) : '—'}</td>
                    <td>{s.completed_at ? dateLabel(s.completed_at) : '—'}</td>
                    <td>
                      {!s.completed_at && (
                        <Button
                          variant="secondary"
                          aria-label={(s.started_at ? 'Complete ' : 'Start ') + s.name}
                          disabled={busy}
                          onClick={() =>
                            void record(s.started_at ? 'finish' : 'start', { stage: i })
                          }
                        >
                          {s.started_at ? 'Complete' : 'Start'}
                        </Button>
                      )}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
          <Field label="Recorded at" hint="Leave blank to record the current time.">
            <input type="datetime-local" value={when} onChange={(e) => setWhen(e.target.value)} />
          </Field>
        </>
      )}
      <div className="form-grid">
        <Field label="Additional stage">
          <input
            placeholder="e.g. Dry hop"
            maxLength={512}
            value={name}
            onChange={(e) => setName(e.target.value)}
          />
        </Field>
        <Button
          variant="secondary"
          disabled={busy || !name.trim()}
          onClick={() => void record('add', { name })}
        >
          Add stage to brew
        </Button>
      </div>
    </section>
  )
}
