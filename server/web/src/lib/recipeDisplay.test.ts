import { describe, expect, it } from 'vitest'
import { formatNumber, formatQuantity, formatTemperature, formatVolume } from './recipeDisplay'

describe('recipe display values', () => {
  it('removes conversion noise while leaving source values intact', () => {
    const source = {
      liters: 9.46352946,
      water: 13.96816948259999,
      grain: 0.136077711,
      c: 65.555555555,
    }
    const original = { ...source }
    expect(formatVolume(source.liters)).toBe('9.46 L')
    expect(formatQuantity(source.water, 'L')).toBe('13.97 L')
    expect(formatQuantity(source.grain, 'kg')).toBe('136.1 g')
    expect(formatTemperature(source.c)).toBe('65.6 °C')
    expect(source).toEqual(original)
  })

  it('keeps useful small quantities and trims unnecessary decimals', () => {
    expect(formatQuantity(2.812272694, 'kg')).toBe('2.81 kg')
    expect(formatQuantity(14.1747615625, 'g')).toBe('14.2 g')
    expect(formatQuantity(0.025, 'g')).toBe('25 mg')
    expect(formatQuantity(0.005, 'L')).toBe('5 mL')
    expect(formatQuantity(1, 'pack')).toBe('1 pack')
    expect(formatNumber(0.000125)).toBe('0.00013')
    expect(formatNumber(90)).toBe('90')
    expect(formatNumber(Number.NaN)).toBe('—')
    expect(formatNumber(-0)).toBe('0')
  })
})
