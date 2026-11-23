export function SourceEvidence({
  text,
  scaledBill = false,
}: {
  text: string
  scaledBill?: boolean
}) {
  return (
    <>
      {scaledBill && (
        <p>
          <strong>These notes give quantities for the original batch.</strong> Use the scaled
          ingredient table below for your brewer’s water and ingredients.
        </p>
      )}
      <p>
        {text.split(/(https:\/\/[\w.-]+\.[a-z]+(?:\/[^\s]*)?)/i).map((part, i) =>
          i % 2 === 1 ? (
            <a key={i} href={part} target="_blank" rel="noreferrer">
              {part.slice('https://'.length).split('/')[0]}
            </a>
          ) : (
            part
          )
        )}
      </p>
    </>
  )
}
