import { useState } from 'react'
import { api, errorMessage } from '../lib/api'
import { useResource } from '../context'
import type { Recipe, MachineRecipe } from '../types'
import { Button, ErrorBanner, Field, Modal } from './ui'
import { ProgramTable } from './Program'

type StockItem = {
  recipe_id: string
  revision: number
  program: MachineRecipe
  maintenance: boolean
}
type Menu = { machine: string; items: StockItem[]; last_requested: string }
export const stockTarget = 'stock-zymatic-menu'

export function StockZymaticTransfer({
  recipes,
  initialSelection,
  back,
  close,
}: {
  recipes: Recipe[]
  initialSelection: string[]
  back: () => void
  close: () => void
}) {
  const [machine, setMachine] = useState(() => localStorage.getItem('stock-zymatic-serial') || '')
  const [chosen, setChosen] = useState(initialSelection)
  const [routing, setRouting] = useState<Record<string, number[]>>({})
  function paths(r: Recipe) {
    return (
      routing[r.id] || (r.steps || []).map((s) => (r.source_model === 'zymatic' ? s.location : 0))
    )
  }
  const selected = recipes.filter((r) => chosen.includes(r.id))
  const needsPaths = selected.filter(
    (r) => !r.native_program && (r.source_model !== 'zymatic' || r.steps.some((s) => !s.location))
  )
  const ready = needsPaths.every((r) => paths(r).every(Boolean))
  const [preview, setPreview] = useState<StockItem[] | null>(null),
    [sent, setSent] = useState(false)
  const [busy, setBusy] = useState(false),
    [error, setError] = useState('')
  const id = machine.trim().toUpperCase(),
    valid = /^[A-Z0-9]{1,32}$/.test(id)
  const path = '/stock-zymatics/' + encodeURIComponent(id) + '/recipes'
  const menu = useResource<Menu>(valid ? path : null)
  async function submit(publish: boolean) {
    setBusy(true)
    setError('')
    try {
      const references = selected.map((r) => ({
        recipe_id: r.id,
        revision: r.revision,
        ...(needsPaths.includes(r) ? { locations: paths(r) } : {}),
      }))
      const response = await api<{ items: StockItem[] }>(path, {
        method: 'POST',
        body: { recipes: references, publish },
      })
      setPreview(response.items)
      if (publish) {
        setSent(true)
        localStorage.setItem('stock-zymatic-serial', id)
        menu.reload()
      }
    } catch (e) {
      setError(errorMessage(e))
      setPreview(null)
    } finally {
      setBusy(false)
    }
  }
  async function remove(item: StockItem) {
    setBusy(true)
    setError('')
    try {
      await api(path + '/' + encodeURIComponent(item.recipe_id), { method: 'DELETE' })
      menu.reload()
      setPreview(null)
      setSent(false)
    } catch (e) {
      setError(errorMessage(e))
    } finally {
      setBusy(false)
    }
  }
  return (
    <Modal
      title="Send to stock Zymatic"
      wide
      onClose={close}
      actions={
        <>
          <Button variant="secondary" disabled={busy} onClick={back}>
            Change target
          </Button>
          {sent ? (
            <Button onClick={close}>Done</Button>
          ) : (
            <Button
              disabled={!valid || !chosen.length || !ready || busy}
              onClick={() => void submit(!!preview)}
            >
              {busy ? 'Working…' : preview ? 'Publish to brewer menu' : 'Preview stock menu'}
            </Button>
          )}
        </>
      }
    >
      <p>
        The stock brewer fetches its menu over HTTP. Point its PicoBrew hostname to this Hub on LAN
        port 80, then open the recipe menu on the machine.
      </p>
      <Field label="Zymatic serial number" hint="Shown under About Zymatic">
        <input
          maxLength={32}
          value={machine}
          disabled={busy}
          onChange={(e) => {
            setMachine(e.target.value)
            setPreview(null)
            setSent(false)
            setError('')
          }}
        />
      </Field>
      <ErrorBanner message={error || (valid ? menu.error : '')} />
      {!sent && (
        <fieldset disabled={busy}>
          <legend>Saved recipes</legend>
          {recipes.map((r) => (
            <label className="checkbox-row" key={r.id}>
              <input
                type="checkbox"
                checked={chosen.includes(r.id)}
                onChange={() => {
                  setChosen((v) =>
                    v.includes(r.id) ? v.filter((id) => id !== r.id) : [...v, r.id]
                  )
                  setPreview(null)
                }}
              />
              {r.name}
            </label>
          ))}
        </fieldset>
      )}
      {!sent &&
        needsPaths.map((r) => (
          <fieldset key={r.id} disabled={busy}>
            <legend>{r.name} · Zymatic flow paths</legend>
            {r.steps.map((s, i) => (
              <Field key={i} label={String(i + 1) + '. ' + s.name}>
                <select
                  value={paths(r)[i]}
                  onChange={(e) => {
                    setRouting({
                      ...routing,
                      [r.id]: paths(r).map((n, j) => (j === i ? Number(e.target.value) : n)),
                    })
                    setPreview(null)
                  }}
                >
                  {[
                    'Choose flow path',
                    'Keg circulation',
                    'Grain compartment',
                    'Hop addition 1',
                    'Hop addition 2',
                    'Hop addition 3',
                    'Hop addition 4',
                  ].map((name, n) => (
                    <option key={n} value={n}>
                      {name}
                    </option>
                  ))}
                </select>
              </Field>
            ))}
          </fieldset>
        ))}
      {sent && (
        <p role="status">Published for {id}. The brewer can now fetch the selected programs.</p>
      )}
      {preview?.map((item) => (
        <section key={item.recipe_id}>
          <h4>{item.program.name}</h4>
          <ProgramTable program={item.program} model="zymatic" archived />
        </section>
      ))}
      {valid && menu.data?.machine === id && menu.data.items.length > 0 && (
        <section>
          <h4>Published on this Hub</h4>
          {menu.data.items.map((item) => (
            <p key={item.recipe_id}>
              {item.program.name} · Revision {item.revision}{' '}
              <Button variant="ghost" disabled={busy} onClick={() => void remove(item)}>
                Remove from menu
              </Button>
            </p>
          ))}
        </section>
      )}
    </Modal>
  )
}
