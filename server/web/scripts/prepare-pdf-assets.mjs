import { cp, mkdir, readFile, readdir, rm } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

// Keep the reader's worker, fonts, CMaps and image decoders on the same pinned
// package version. Vite copies these generated resources into standalone dist.
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const source = path.join(root, 'node_modules/pdfjs-dist')
const { version } = JSON.parse(await readFile(path.join(source, 'package.json'), 'utf8'))
const vendor = path.join(root, 'public/vendor')
await mkdir(vendor, { recursive: true })
for (const entry of await readdir(vendor)) {
  if (/^pdfjs-\d+\.\d+\.\d+$/.test(entry))
    await rm(path.join(vendor, entry), { recursive: true, force: true })
}
const destination = path.join(vendor, `pdfjs-${version}`)
await mkdir(destination, { recursive: true })
await cp(path.join(source, 'LICENSE'), path.join(destination, 'LICENSE'))
for (const folder of ['cmaps', 'standard_fonts'])
  await cp(path.join(source, folder), path.join(destination, folder), { recursive: true })
await mkdir(path.join(destination, 'wasm'), { recursive: true })
for (const file of await readdir(path.join(source, 'wasm'))) {
  // WebAssembly/evaluation is disabled; only the matching JavaScript image
  // decoders and their upstream notices are needed by this canvas reader.
  if (file.endsWith('_nowasm_fallback.js') || file.startsWith('LICENSE'))
    await cp(path.join(source, 'wasm', file), path.join(destination, 'wasm', file))
}
