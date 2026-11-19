// Display rounding never changes the recipe or its original source quantities.
export function formatNumber(value: number, maximumFractionDigits = 2): string {
  if (!Number.isFinite(value)) return '—'
  const small = value !== 0 && Math.abs(value) < 10 ** -maximumFractionDigits
  return new Intl.NumberFormat('en', {
    ...(small ? { maximumSignificantDigits: 2 } : { maximumFractionDigits }),
    useGrouping: true,
  }).format(Object.is(value, -0) ? 0 : value)
}

export function formatVolume(liters: number): string {
  return `${formatNumber(liters, 2)} L`
}

export function formatTemperature(celsius: number): string {
  return `${formatNumber(celsius, 1)} °C`
}

export function formatQuantity(amount: number, unit: string): string {
  const absolute = Math.abs(amount)
  switch (unit.toLowerCase()) {
    case 'kg':
      if (absolute > 0 && absolute < 0.001) return `${formatNumber(amount * 1_000_000, 1)} mg`
      if (absolute > 0 && absolute < 1) return `${formatNumber(amount * 1_000, 1)} g`
      return `${formatNumber(amount, 2)} kg`
    case 'g':
      if (absolute > 0 && absolute < 1) return `${formatNumber(amount * 1_000, 1)} mg`
      return `${formatNumber(amount, 1)} g`
    case 'l':
      if (absolute > 0 && absolute < 1) return `${formatNumber(amount * 1_000, 1)} mL`
      return formatVolume(amount)
    case 'ml':
      return `${formatNumber(amount, 1)} mL`
    default:
      return `${formatNumber(amount, 2)}${unit ? ` ${unit}` : ''}`
  }
}
