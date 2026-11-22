import { useEffect, useId, useMemo, useRef, useState } from 'react'
import { ChevronDown } from 'lucide-react'
import { useHub } from '../context'
import { brewingReference } from '../lib/beerCrafter'

export function RecipeStyleField({
  value,
  change,
  label = 'Style',
}: {
  value: string
  change: (name: string) => void
  label?: string
}) {
  const { recipes } = useHub()
  const list = useId()
  const input = useRef<HTMLInputElement>(null)
  const [open, setOpen] = useState(false)
  const [filter, setFilter] = useState('')
  const [active, setActive] = useState(-1)
  const styles = useMemo(() => {
    const names = new Map<string, string>()
    for (const style of [
      ...brewingReference.styles.map((s) => s.name),
      ...recipes.map((r) => r.style),
    ]) {
      const name = style?.trim()
      if (name && !names.has(name.toLowerCase())) names.set(name.toLowerCase(), name)
    }
    return [...names.values()].sort((a, b) => a.localeCompare(b))
  }, [recipes])
  const query = filter.trim().toLowerCase()
  const matches = styles.filter((name) => name.toLowerCase().includes(query))
  const custom = !!query && !matches.some((name) => name.toLowerCase() === query)
  const choices = custom ? [...matches, filter.trim()] : matches
  useEffect(() => {
    if (open && active >= 0)
      document.getElementById(list + '-' + active)?.scrollIntoView({ block: 'nearest' })
  }, [open, active, list])
  function show() {
    setFilter('')
    setActive(-1)
    setOpen(true)
  }
  function select(name: string) {
    change(name)
    setOpen(false)
    setActive(-1)
  }
  return (
    <div
      className="field recipe-style-field"
      onBlur={(e) => {
        if (!e.currentTarget.contains(e.relatedTarget)) setOpen(false)
      }}
    >
      <label className="field-label" htmlFor={list + '-input'}>
        {label}
      </label>
      <div className="style-control">
        <input
          ref={input}
          id={list + '-input'}
          role="combobox"
          aria-autocomplete="list"
          aria-expanded={open}
          aria-controls={list}
          aria-activedescendant={open && choices[active] ? list + '-' + active : undefined}
          autoComplete="off"
          placeholder="Search or enter a style"
          value={value}
          onFocus={show}
          onClick={() => {
            if (!open) show()
          }}
          onChange={(e) => {
            setFilter(e.target.value)
            change(e.target.value)
            setActive(-1)
            setOpen(true)
          }}
          onKeyDown={(e) => {
            if (e.nativeEvent.isComposing) return
            if (e.key === 'Escape' && open) {
              e.preventDefault()
              setOpen(false)
            }
            if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
              e.preventDefault()
              if (!open) {
                show()
                setActive(e.key === 'ArrowDown' ? 0 : styles.length - 1)
              } else
                setActive(
                  e.key === 'ArrowDown'
                    ? Math.min(active + 1, choices.length - 1)
                    : active <= 0
                      ? choices.length - 1
                      : active - 1
                )
            }
            if (e.key === 'Enter' && open) {
              e.preventDefault()
              select(
                choices[active] ?? matches.find((n) => n.toLowerCase() === query) ?? value.trim()
              )
            }
          }}
        />
        <button
          type="button"
          className="style-toggle"
          aria-label="Show beer styles"
          tabIndex={-1}
          onMouseDown={(e) => e.preventDefault()}
          onClick={() => {
            if (open) setOpen(false)
            else {
              input.current?.focus()
              show()
            }
          }}
        >
          <ChevronDown size={16} />
        </button>
        <ul role="listbox" id={list} aria-label="Beer styles" hidden={!open}>
          {open &&
            choices.map((name, i) => (
              <li
                key={name}
                role="option"
                id={list + '-' + i}
                aria-selected={i === active}
                onMouseDown={(e) => e.preventDefault()}
                onClick={() => select(name)}
              >
                {custom && i === matches.length ? 'Use “' + name + '”' : name}
              </li>
            ))}
        </ul>
      </div>
    </div>
  )
}
