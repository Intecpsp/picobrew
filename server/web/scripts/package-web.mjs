import { createHash } from 'node:crypto'
import { lstat, mkdir, readFile, readdir, writeFile } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { zipSync } from 'fflate'

const webRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const bundleRoot = path.resolve(webRoot, '../internal/webui')
const sha256 = (data) => createHash('sha256').update(data).digest('hex')

async function filesIn(root, skip = () => false, prefix = '') {
  const files = []
  for (const entry of (await readdir(path.join(root, prefix), { withFileTypes: true })).sort((a, b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0)) {
    const relative = prefix ? `${prefix}/${entry.name}` : entry.name
    if (skip(relative)) continue
    if (entry.isSymbolicLink()) throw new Error(`Bundle inputs must not be symbolic links: ${relative}`)
    if (entry.isDirectory()) files.push(...await filesIn(root, skip, relative))
    else if (entry.isFile()) files.push({ path: relative, data: await readFile(path.join(root, relative)) })
    else throw new Error(`Bundle inputs must be regular files: ${relative}`)
  }
  return files.sort((a, b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0)
}

function sourceInputSkipped(relative) {
  const top = relative.split('/')[0]
  if (top === 'src') return /\.test\.[cm]?[jt]sx?$/.test(relative)
  if (top === 'public') {
    // Generated from the hash-locked pdfjs-dist dependency, not source inputs.
    return /^public\/vendor\/pdfjs-\d+\.\d+\.\d+(?:\/|$)/.test(relative) || relative.endsWith('/.DS_Store')
  }
  if (top === 'scripts') return relative !== 'scripts' && !['scripts/prepare-pdf-assets.mjs', 'scripts/package-web.mjs'].includes(relative)
  return !['index.html', 'package.json', 'package-lock.json', 'tsconfig.json', 'vite.config.ts'].includes(relative)
}

export function createArchive(files) {
  const entries = Object.create(null)
  for (const { path: name, data } of [...files].sort((a, b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0)) {
    if (!name || name.startsWith('/') || name.includes('\\') || name.includes(':') || name.split('/').some((part) => !part || part === '.' || part === '..') || Object.hasOwn(entries, name)) {
      throw new Error(`Invalid or duplicate bundle path: ${name}`)
    }
    entries[name] = [data, {
      level: 9,
      // Construct local midnight, not a UTC instant: ZIP stores local calendar
      // fields, so every time zone emits the same fixed DOS timestamp.
      mtime: new Date(1980, 0, 1, 0, 0, 0),
      os: 3,
      attrs: (0o100644 << 16) >>> 0,
    }]
  }
  // fflate is pure JavaScript and exactly pinned; host zlib/ZIP implementations
  // cannot change compression bytes, file order, metadata or permissions.
  return Buffer.from(zipSync(entries, { level: 9 }))
}

export async function packageWeb({ source = webRoot, destination = bundleRoot, check = false } = {}) {
  const pkg = JSON.parse(await readFile(path.join(source, 'package.json'), 'utf8'))
  const files = await filesIn(path.join(source, 'dist'))
  const byName = new Map(files.map((file) => [file.path, file.data]))
  const index = byName.get('index.html')?.toString('utf8') || ''
  if (!index.includes('<!doctype html>') || !index.includes('<script') || !/\/assets\/[^"']+\.js/.test(index)) {
    throw new Error('Build the real browser client first: npm --prefix web run build')
  }
  const notices = byName.get('third-party-licenses.md')?.toString('utf8') || ''
  if (!notices.trim()) throw new Error('The browser build is missing third-party-licenses.md')
  for (const name of ['react', 'react-dom', 'lucide-react']) {
    if (pkg.dependencies?.[name] && !notices.includes(`## ${name} - ${pkg.dependencies[name]}`)) {
      throw new Error(`The browser build is missing the complete ${name} license notice`)
    }
  }
  if (pkg.dependencies?.['pdfjs-dist']) {
    const vendor = `vendor/pdfjs-${pkg.dependencies['pdfjs-dist']}`
    for (const notice of ['LICENSE', 'cmaps/LICENSE', 'standard_fonts/LICENSE_FOXIT', 'standard_fonts/LICENSE_LIBERATION']) {
      if (!byName.has(`${vendor}/${notice}`)) throw new Error(`Missing PDF/font license: ${vendor}/${notice}`)
    }
  }
  const uncompressed = files.reduce((total, file) => total + file.data.length, 0)
  if (uncompressed > 64 * 1024 * 1024) throw new Error('Browser bundle exceeds the embedded reader limit of 64 MiB')
  const archive = createArchive(files)
  const inputs = await filesIn(source, sourceInputSkipped)
  const identity = ({ path: name, data }) => ({ path: name, bytes: data.length, sha256: sha256(data) })
  const manifest = {
    schema_version: 1,
    source_root: 'web',
    build: {
      command: 'npm --prefix web run build:bundle',
      node: pkg.engines.node,
      tools: Object.fromEntries(['vite', 'typescript', 'fflate'].map((name) => [name, pkg.devDependencies[name]])),
      archive_format: 'ZIP, pinned JavaScript DEFLATE, sorted paths, 1980-01-01 00:00:00, Unix 0644',
    },
    source_inputs: inputs.map(identity),
    archive: { path: 'bundle.zip', bytes: archive.length, sha256: sha256(archive) },
    file_count: files.length,
    uncompressed_bytes: uncompressed,
    files: files.map(identity),
    license_files: files.filter((file) => /(?:^|\/)LICENSE[^/]*$/.test(file.path) || file.path === 'third-party-licenses.md').map((file) => file.path),
  }
  const manifestBytes = Buffer.from(JSON.stringify(manifest, null, 2) + '\n')
  const outputs = [['bundle.zip', archive], ['manifest.json', manifestBytes]]
  if (check) {
    const stale = []
    for (const [name, expected] of outputs) {
      let actual
      try {
        actual = await readFile(path.join(destination, name))
      } catch (error) {
        if (error.code !== 'ENOENT') throw error
      }
      if (!actual?.equals(expected)) stale.push(name)
    }
    if (stale.length) throw new Error(`Committed browser bundle is stale (${stale.join(', ')}). Run npm --prefix web run build:bundle and commit bundle.zip + manifest.json with the source change.`)
  } else {
    await mkdir(destination, { recursive: true })
    for (const [name, data] of outputs) {
      const target = path.join(destination, name)
      try {
        if ((await lstat(target)).isSymbolicLink()) throw new Error(`Refusing to write a symbolic-link output: ${target}`)
      } catch (error) {
        if (error.code !== 'ENOENT') throw error
      }
      await writeFile(target, data)
    }
  }
  return manifest
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const arguments_ = process.argv.slice(2)
  if (arguments_.some((arg) => arg !== '--check') || arguments_.length > 1) {
    console.error('Usage: node scripts/package-web.mjs [--check]')
    process.exitCode = 1
  } else {
    try {
      const manifest = await packageWeb({ check: arguments_.includes('--check') })
      console.log(`${arguments_.includes('--check') ? 'Verified' : 'Packaged'} browser bundle: ${manifest.file_count} files, ${manifest.uncompressed_bytes} bytes -> ${manifest.archive.bytes} ZIP bytes; SHA-256 ${manifest.archive.sha256}`)
    } catch (error) {
      console.error(error.message)
      process.exitCode = 1
    }
  }
}
