import assert from 'node:assert/strict'
import { mkdtemp, mkdir, readFile, rm, utimes, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import path from 'node:path'
import test from 'node:test'
import { unzipSync } from 'fflate'
import { createArchive, packageWeb } from './package-web.mjs'

test('ZIP entries preserve binary contents with fixed metadata and ordering', () => {
  const files = [
    { path: 'vendor/font.bin', data: Buffer.from([0, 255, 13, 10, 0, 128]) },
    { path: 'index.html', data: Buffer.from('<!doctype html>') },
  ]
  const archive = createArchive(files)
  assert.deepEqual(createArchive([...files].reverse()), archive)
  assert.equal(archive.readUInt16LE(10), 0) // DOS time: midnight.
  assert.equal(archive.readUInt16LE(12), 33) // DOS date: 1980-01-01.
  const unpacked = unzipSync(archive)
  assert.deepEqual(Object.keys(unpacked), ['index.html', 'vendor/font.bin'])
  for (const file of files) assert.deepEqual(Buffer.from(unpacked[file.path]), file.data)
  assert.throws(() => createArchive([...files, files[0]]), /duplicate/)
  for (const name of ['../outside', '/absolute', 'C:/drive', 'a\\b', 'a//b']) {
    assert.throws(() => createArchive([{ path: name, data: Buffer.alloc(0) }]), /Invalid/)
  }
})

test('freshness check detects changed sources, missing assets and changed bundle bytes', async () => {
  const temporary = await mkdtemp(path.join(tmpdir(), 'hub browser bundle '))
  try {
    const source = path.join(temporary, 'web')
    const destination = path.join(temporary, 'embedded')
    await mkdir(path.join(source, 'src'), { recursive: true })
    await mkdir(path.join(source, 'dist/assets'), { recursive: true })
    await writeFile(path.join(source, 'package.json'), JSON.stringify({ engines: { node: '>=22.22.2' }, devDependencies: { vite: 'fixture', typescript: 'fixture', fflate: '0.8.3' } }))
    await writeFile(path.join(source, 'package-lock.json'), '{}\n')
    await writeFile(path.join(source, 'src/main.ts'), 'export const value = 1\n')
    await writeFile(path.join(source, 'dist/index.html'), '<!doctype html><script type="module" src="/assets/main.js"></script>')
    await writeFile(path.join(source, 'dist/assets/main.js'), 'console.log(1)\n')
    await writeFile(path.join(source, 'dist/third-party-licenses.md'), '# Fixture license notice\n')
    await packageWeb({ source, destination })
    await packageWeb({ source, destination, check: true })
    await utimes(path.join(source, 'dist/index.html'), new Date(), new Date())
    await packageWeb({ source, destination, check: true })
    await writeFile(path.join(source, 'src/main.ts'), 'export const value = 2\n')
    await assert.rejects(packageWeb({ source, destination, check: true }), /stale/)
    await writeFile(path.join(source, 'src/main.ts'), 'export const value = 1\n')
    await rm(path.join(source, 'dist/assets/main.js'))
    await assert.rejects(packageWeb({ source, destination, check: true }), /stale/)
    await writeFile(path.join(source, 'dist/assets/main.js'), 'console.log(1)\n')
    const archive = await readFile(path.join(destination, 'bundle.zip'))
    archive[archive.length - 1] ^= 1
    await writeFile(path.join(destination, 'bundle.zip'), archive)
    await assert.rejects(packageWeb({ source, destination, check: true }), /stale/)
  } finally {
    await rm(temporary, { recursive: true, force: true })
  }
})
