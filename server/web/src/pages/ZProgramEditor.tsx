import { useEffect, useState } from 'react'
import { Download, Save, Send, Trash2, Upload } from 'lucide-react'
import { useHub } from '../context'
import { api, download, errorMessage } from '../lib/api'
import { slug } from '../lib/utils'
import { recipeSavePath } from '../lib/programs'
import { ZProgramFields } from '../components/ZProgramFields'
import type { Recipe, ZProgram } from '../types'
import { Button, Confirm, ErrorBanner, PageTitle } from '../components/ui'
import { TransferDialog } from '../components/TransferDialog'

export default function ZProgramEditor({
  initial,
  onImport,
}: {
  initial: Recipe
  onImport: () => void
}) {
  const hub = useHub()
  const [saved, setSaved] = useState(initial)
  const [program, setProgram] = useState(initial.native_program!.z_program!)
  const [dirty, setDirty] = useState(false)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState('')
  const [send, setSend] = useState(false)
  const [remove, setRemove] = useState(false)
  function update(next: ZProgram) {
    setProgram(next)
    setDirty(true)
    setError('')
  }
  useEffect(() => {
    function warn(e: BeforeUnloadEvent) {
      if (dirty) {
        e.preventDefault()
        e.returnValue = ''
      }
    }
    window.addEventListener('beforeunload', warn)
    return () => window.removeEventListener('beforeunload', warn)
  }, [dirty])
  async function save() {
    setBusy(true)
    setError('')
    try {
      const body: Recipe = {
        ...saved,
        name: program.Name,
        native_program: {
          id: saved.native_program?.id || slug(program.Name).slice(0, 20),
          name: program.Name,
          steps: [],
          z_program: program,
        },
      }
      const result = await api<Recipe>(recipeSavePath(saved.id, 'z'), {
        method: saved.id ? 'PUT' : 'POST',
        body,
      })
      setSaved(result)
      setDirty(false)
      await hub.refresh()
      hub.notify('Z program saved.')
      if (!saved.id) hub.navigate('/recipes/' + result.id)
    } catch (e) {
      setError(errorMessage(e))
    } finally {
      setBusy(false)
    }
  }
  return (
    <>
      <PageTitle
        title={saved.id ? saved.name : 'Create a Z program'}
        eyebrow="Z Program Maker"
        back={() => {
          if (!dirty || window.confirm('Leave this program without saving your changes?'))
            hub.navigate('/recipes')
        }}
        actions={
          <>
            <Button
              variant="secondary"
              disabled={busy || dirty || !saved.id}
              onClick={() => setSend(true)}
            >
              <Send size={16} />
              Send
            </Button>
            <Button type="submit" form="z-program" disabled={busy}>
              <Save size={16} />
              Save program
            </Button>
          </>
        }
      />
      <ErrorBanner message={error} />
      <form
        id="z-program"
        className="editor-main"
        onSubmit={(e) => {
          e.preventDefault()
          void save()
        }}
      >
        <fieldset className="program-fieldset" disabled={busy}>
          <ZProgramFields program={program} update={update} confirmSample={dirty || !!saved.id} />
        </fieldset>
      </form>
      <div className="inline-actions">
        <Button
          variant="secondary"
          disabled={busy}
          onClick={() => {
            if (!dirty || window.confirm('Import another program and leave your unsaved changes?'))
              onImport()
          }}
        >
          <Upload size={16} />
          Import program
        </Button>
        <Button
          variant="secondary"
          disabled={busy || dirty || !saved.id}
          onClick={() =>
            download(
              '/recipes/' + saved.id + '/export?format=z-program',
              slug(saved.name) + '.json'
            ).catch((e) => setError(errorMessage(e)))
          }
        >
          <Download size={16} />
          Export Z program
        </Button>
        {saved.id && (
          <Button
            variant="secondary"
            disabled={dirty || busy}
            onClick={() =>
              download(
                '/recipes/' + saved.id + '/export?format=json',
                slug(saved.name) + '-hub.json'
              ).catch((e) => setError(errorMessage(e)))
            }
          >
            <Download size={16} />
            Export Hub JSON
          </Button>
        )}
        {saved.id && (
          <Button variant="ghost" disabled={busy} onClick={() => setRemove(true)}>
            <Trash2 size={16} />
            Delete program
          </Button>
        )}
      </div>
      {send && (
        <TransferDialog
          recipes={[saved]}
          initialSelection={[saved.id]}
          onClose={() => setSend(false)}
        />
      )}
      {remove && (
        <Confirm
          title="Delete this library program?"
          description={saved.name}
          label="Delete program"
          danger
          busy={busy}
          onClose={() => setRemove(false)}
          onConfirm={async () => {
            setBusy(true)
            try {
              await api('/recipes/' + saved.id, { method: 'DELETE' })
              await hub.refresh()
              hub.navigate('/recipes')
            } catch (e) {
              setError(errorMessage(e))
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
