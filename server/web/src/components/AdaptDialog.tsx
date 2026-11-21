import { StockZymaticTransfer, stockTarget } from './StockZymaticTransfer'
import { useEffect, useRef, useState } from 'react'
import { ArrowUpRight, Send } from 'lucide-react'
import { useHub } from '../context'
import { api, APIError, errorMessage } from '../lib/api'
import { isNative, routeLabel } from '../lib/programs'
import { formatNumber, formatQuantity, formatTemperature, formatVolume } from '../lib/recipeDisplay'
import type { Adapted, Profile, Recipe } from '../types'
import { Badge, Button, ErrorBanner, Field, Modal } from './ui'

type BatchSelection = { value: string; explicit: boolean }

function validBatch(value: string, profile?: Profile) {
  const amount = Number(value)
  return !!profile && Number.isFinite(amount) && amount > 0 && amount <= profile.batch_liters
}

function profileBatch(current: BatchSelection | undefined, profile?: Profile): BatchSelection {
  if (current?.explicit && validBatch(current.value, profile)) return current
  const limit = profile?.batch_liters || 0
  // A readable default must never round above the profile's actual capacity.
  const rounded = Number(limit.toFixed(3))
  const amount = rounded > limit ? Math.floor(limit * 1000) / 1000 : rounded
  return { value: limit > 0 ? String(amount || limit) : '', explicit: false }
}

export function AdaptDialog({ recipe, onClose }: { recipe: Recipe; onClose: () => void }) {
  const hub = useHub()
  const native = isNative(recipe)
  const modelProfile = (model: string) => (model === 'z-series' ? 'z' : model)
  const source = !native && hub.profiles.find((p) => p.id === recipe.source_model)
  const first = hub.devices.find(
    (device) =>
      device.online &&
      (source
        ? modelProfile(device.model) === source.id
        : hub.profiles.some((p) => p.id === modelProfile(device.model)))
  )
  const [deviceID, setDeviceID] = useState(first?.id || '')
  const [profileID, setProfileID] = useState(
    (source && source.id) || (first && modelProfile(first.model)) || hub.profiles[0]?.id || ''
  )
  const profile = hub.profiles.find((item) => item.id === profileID)
  const [batch, setBatch] = useState<BatchSelection>(() =>
    profileBatch(
      source ? { value: String(recipe.batch_liters), explicit: true } : undefined,
      profile
    )
  )
  const [previewResult, setPreviewResult] = useState<{ key: string; value: Adapted } | null>(null)
  const [sent, setSent] = useState<{ machine: string; path: string; program: string } | null>(null)
  const [busy, setBusy] = useState('')
  const [error, setError] = useState('')
  const requestID = useRef(0)
  const previewController = useRef<AbortController | null>(null)
  const target = hub.devices.find((device) => device.id === deviceID)
  const needsReady = target?.status?.busy || target?.status?.stopped || !target?.status?.ready
  const batchLiters = native ? 0 : Number(batch.value)
  const batchValid = native || validBatch(batch.value, profile)
  const inputKey = JSON.stringify({
    recipe: [recipe.id, recipe.revision, recipe.kind],
    deviceID,
    profile,
    batch: native ? 0 : batch.value,
  })
  const latestKey = useRef(inputKey)
  latestKey.current = inputKey
  const adapted = previewResult?.key === inputKey ? previewResult.value : null
  const canSend = !!(
    adapted?.can_deploy &&
    adapted.machine_recipe &&
    !adapted.deployment_blockers?.length &&
    target?.online &&
    !needsReady &&
    modelProfile(target.model) === profileID &&
    batchValid &&
    !busy
  )

  function invalidatePreview() {
    requestID.current++
    previewController.current?.abort()
    previewController.current = null
    setPreviewResult(null)
    setError('')
    setBusy((current) => (current === 'preview' ? '' : current))
  }

  useEffect(() => {
    setBatch((current) => profileBatch(current, profile))
  }, [profileID, profile?.batch_liters])

  useEffect(() => {
    invalidatePreview()
    return () => {
      requestID.current++
      previewController.current?.abort()
    }
  }, [inputKey])

  async function preview() {
    if (!profile || !batchValid || busy) return
    const key = inputKey
    const id = ++requestID.current
    const controller = new AbortController()
    previewController.current?.abort()
    previewController.current = controller
    setBusy('preview')
    setError('')
    setPreviewResult(null)
    try {
      const result = await api<Adapted>(`/recipes/${recipe.id}/adapt`, {
        method: 'POST',
        body: { profile_id: profileID, batch_liters: batchLiters },
        signal: controller.signal,
      })
      if (id === requestID.current && key === latestKey.current && !controller.signal.aborted)
        setPreviewResult({ key, value: result })
    } catch (error) {
      if (id === requestID.current && key === latestKey.current && !controller.signal.aborted)
        setError(errorMessage(error))
    } finally {
      if (id === requestID.current) {
        previewController.current = null
        setBusy('')
      }
    }
  }

  async function deploy() {
    if (!adapted || !canSend || previewResult?.key !== latestKey.current) return
    setBusy('deploy')
    setError('')
    try {
      const receipt = await api<{ device_id: string; recipe: { id: string; name: string } }>(
        `/devices/${deviceID}/deploy`,
        {
          method: 'POST',
          body: {
            recipe_id: recipe.id,
            profile_id: adapted.profile.id,
            batch_liters: native ? 0 : adapted.recipe.batch_liters,
            source_revision: adapted.source_revision,
            profile_fingerprint: adapted.profile_fingerprint,
          },
        }
      )
      setSent({
        machine: target!.name,
        program: receipt.recipe.name,
        path: `/devices/${receipt.device_id}?recipe=${encodeURIComponent(receipt.recipe.id)}`,
      })
    } catch (error) {
      setError(errorMessage(error))
      if (
        error instanceof APIError &&
        ['profile_changed', 'revision_conflict'].includes(error.code)
      ) {
        setPreviewResult(null)
        void hub.refresh()
      }
    } finally {
      setBusy('')
    }
  }

  if (deviceID === stockTarget)
    return (
      <StockZymaticTransfer
        recipes={[recipe]}
        initialSelection={[recipe.id]}
        back={() => setDeviceID('')}
        close={onClose}
      />
    )
  if (sent)
    return (
      <Modal
        title="Recipe sent"
        onClose={onClose}
        actions={
          <>
            <Button variant="secondary" onClick={onClose}>
              Done
            </Button>
            <Button
              onClick={() => {
                onClose()
                hub.navigate(sent.path)
              }}
            >
              Open {sent.machine}
              <ArrowUpRight size={16} />
            </Button>
          </>
        }
      >
        <p>
          <strong>{sent.program}</strong> is saved on {sent.machine}. Open the brewer to review the
          selection and start when ready.
        </p>
      </Modal>
    )
  return (
    <Modal
      title="Adapt the recipe. Choose its brewer."
      onClose={onClose}
      wide
      actions={
        <>
          <Button variant="secondary" onClick={onClose} disabled={busy === 'deploy'}>
            Cancel
          </Button>
          {adapted ? (
            <Button disabled={!canSend} onClick={() => void deploy()}>
              <Send size={16} />
              {busy === 'deploy' ? 'Sending…' : 'Send to machine'}
            </Button>
          ) : (
            <Button disabled={!profile || !batchValid || !!busy} onClick={() => void preview()}>
              {busy === 'preview' ? 'Adapting…' : 'Preview adaptation'}
              <ArrowUpRight size={16} />
            </Button>
          )}
        </>
      }
    >
      <p>
        {native
          ? 'This native program retains its exact temperatures, timing and routing. No batch or ingredient scaling is performed.'
          : 'Ingredient amounts are scaled for the selected batch and equipment profile. Recipes without verified chamber mappings remain available for planning.'}{' '}
        Sending a recipe never starts the brewer.
      </p>
      <div className="form-grid">
        <Field label="Target machine">
          <select
            value={deviceID}
            disabled={busy === 'deploy'}
            onChange={(event) => {
              invalidatePreview()
              const machine = hub.devices.find((device) => device.id === event.target.value)
              setDeviceID(event.target.value)
              if (machine) {
                setProfileID(modelProfile(machine.model))
                setBatch((current) =>
                  profileBatch(
                    current,
                    hub.profiles.find((p) => p.id === modelProfile(machine.model))
                  )
                )
              }
            }}
          >
            <option value="">Planning only</option>
            <option value={stockTarget}>Stock Zymatic · sync menu</option>
            {hub.devices.map((device) => (
              <option key={device.id} value={device.id} disabled={!device.model}>
                {device.name}
                {!device.model ? ' (model not identified)' : device.online ? '' : ' (offline)'}
              </option>
            ))}
          </select>
        </Field>
        <Field label="Equipment profile">
          <select
            value={profileID}
            disabled={busy === 'deploy'}
            onChange={(event) => {
              invalidatePreview()
              const nextProfileID = event.target.value
              setProfileID(nextProfileID)
              setBatch((current) =>
                profileBatch(
                  current,
                  hub.profiles.find((p) => p.id === nextProfileID)
                )
              )
              if (target && modelProfile(target.model) !== nextProfileID) setDeviceID('')
            }}
          >
            {hub.profiles.map((item) => (
              <option key={item.id} value={item.id}>
                {item.name}
                {item.control_supported ? '' : ' · planning & export'}
              </option>
            ))}
          </select>
        </Field>
        {!native && (
          <Field
            label="Target batch size (L)"
            hint={`Source batch: ${formatVolume(recipe.batch_liters)}. Profile batch: ${formatVolume(profile?.batch_liters || 0)}.`}
          >
            <input
              type="number"
              min="0"
              max={profile?.batch_liters}
              step="any"
              value={batch.value}
              disabled={busy === 'deploy'}
              onChange={(event) => {
                invalidatePreview()
                setBatch({ value: event.target.value, explicit: true })
              }}
            />
          </Field>
        )}
      </div>
      {!batchValid && profile && (
        <ErrorBanner
          message={`Enter a positive target batch no larger than the profile's ${profile.batch_liters} L limit.`}
        />
      )}
      <ErrorBanner message={error} />
      {adapted && (
        <>
          <div className="adapt-summary">
            <Badge tone={adapted.can_deploy ? 'green' : 'amber'}>
              {adapted.can_deploy ? 'Machine recipe ready' : 'Planning preview'}
            </Badge>
            <strong>
              {native
                ? 'Exact native schedule'
                : `${formatVolume(adapted.recipe.batch_liters)} · ${formatNumber(adapted.recipe.efficiency)}% efficiency`}
            </strong>
          </div>
          {!!adapted.deployment_blockers?.length && (
            <section aria-label="Deployment blockers">
              <h3>Deployment blocked</h3>
              <ul>
                {adapted.deployment_blockers.map((reason, index) => (
                  <li key={index}>{reason}</li>
                ))}
              </ul>
            </section>
          )}
          {(adapted.warnings || []).map((warning, index) => (
            <p className="notice" key={index}>
              {warning}
            </p>
          ))}
          {needsReady && target && (
            <p className="notice">
              {target.name} must be idle and ready at its main menu before deployment.
            </p>
          )}
          {!native && (
            <>
              <h3>Scaled ingredients</h3>
              <table className="data-table">
                <thead>
                  <tr>
                    <th>Ingredient</th>
                    <th>Original</th>
                    <th>Adapted</th>
                  </tr>
                </thead>
                <tbody>
                  {adapted.recipe.ingredients.map((item, index) => (
                    <tr key={index}>
                      <td>{item.name}</td>
                      <td>
                        {formatQuantity(
                          recipe.ingredients[index]?.amount || 0,
                          recipe.ingredients[index]?.unit || item.unit
                        )}
                      </td>
                      <td>
                        <strong>{formatQuantity(item.amount, item.unit)}</strong>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </>
          )}
          {adapted.machine_recipe || (native && adapted.recipe.native_program) ? (
            <>
              <h3>{adapted.machine_recipe ? 'Machine schedule' : 'Source program schedule'}</h3>
              <div className="table-scroll">
                <table className="data-table">
                  <thead>
                    <tr>
                      <th>Step</th>
                      <th>Target</th>
                      <th>Time</th>
                      <th>Drain</th>
                      <th>Flow path</th>
                    </tr>
                  </thead>
                  <tbody>
                    {(adapted.machine_recipe || adapted.recipe.native_program)!.steps.map(
                      (step, index) => (
                        <tr key={index}>
                          <td>{step.name}</td>
                          <td>{step.target_f ? `${step.target_f}°F` : 'Unheated'}</td>
                          <td>{step.minutes} min</td>
                          <td>{step.drain_minutes} min</td>
                          <td>
                            {adapted.machine_recipe
                              ? routeLabel(step.location, adapted.profile.id)
                              : routeLabel(step.location, adapted.recipe.source_model, true)}
                          </td>
                        </tr>
                      )
                    )}
                  </tbody>
                </table>
              </div>
            </>
          ) : (
            <>
              <h3>Recipe schedule</h3>
              <p>
                This schedule remains a recipe plan. Resolve the deployment blockers before sending
                it to a machine.
              </p>
              <div className="table-scroll">
                <table className="data-table">
                  <thead>
                    <tr>
                      <th>Step</th>
                      <th>Target</th>
                      <th>Time</th>
                      <th>Drain</th>
                      <th>Flow path</th>
                      <th>Instructions</th>
                    </tr>
                  </thead>
                  <tbody>
                    {adapted.recipe.steps.map((step, index) => (
                      <tr key={index}>
                        <td>{step.name}</td>
                        <td>{step.target_c ? formatTemperature(step.target_c) : 'Unheated'}</td>
                        <td>{step.minutes} min</td>
                        <td>{step.drain_minutes} min</td>
                        <td>{routeLabel(step.location, adapted.profile.id)}</td>
                        <td>
                          {[
                            step.type || step.kind,
                            step.infuse_liters
                              ? `${formatVolume(step.infuse_liters)} infusion`
                              : '',
                            step.ramp_minutes ? `${formatNumber(step.ramp_minutes)} min ramp` : '',
                          ]
                            .filter(Boolean)
                            .join(' · ')}
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            </>
          )}
          <button
            className="text-button"
            disabled={!!busy}
            onClick={() => {
              hub.setDraft({
                ...adapted.recipe,
                id: '',
                revision: 0,
                name: native ? recipe.name : `${recipe.name} · ${adapted.profile.name}`,
              })
              onClose()
              hub.navigate('/recipes/new')
            }}
          >
            Save this adaptation as a separate recipe <ArrowUpRight size={14} />
          </button>
        </>
      )}
    </Modal>
  )
}
