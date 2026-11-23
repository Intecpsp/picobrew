export function SourceEvidence({ text }: { text: string }) {
  return (
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
  )
}
