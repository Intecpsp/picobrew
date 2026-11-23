import { StockSetupGuide } from './StockSetupGuide'

export function StockRestoreGuide({
  model,
  display,
  deviceID,
  onClose,
}: {
  model: string
  display?: string
  deviceID: string
  onClose: () => void
}) {
  return <StockSetupGuide model={model} display={display} deviceID={deviceID} onClose={onClose} />
}
