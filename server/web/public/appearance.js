// Runs before the stylesheet and React, including on the sign-in screen.
(() => {
  const key = 'hub-appearance'
  function apply(value) {
    if (value === 'light' || value === 'dark') document.documentElement.dataset.appearance = value
    else delete document.documentElement.dataset.appearance
  }
  try {
    apply(localStorage.getItem(key))
  } catch {
    // The stylesheet follows the system when browser storage is unavailable.
  }
  window.addEventListener('storage', event => {
    if (event.key === key || event.key === null) {
      try { apply(localStorage.getItem(key)) } catch { /* Storage can be disabled. */ }
    }
  })
})()
