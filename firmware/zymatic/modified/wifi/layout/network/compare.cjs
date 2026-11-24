'use strict';

const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { createRequire } = require('node:module');
const path = require('node:path');
const stockRequire = createRequire(path.resolve(__dirname, '../../../../reverse-engineered/tests/package.json'));
const { CPU, avrInstruction } = stockRequire('avr8js');
const [stock, candidate] = process.argv.slice(2).map(p => readFileSync(p));
const returnPC = 0xffff;

function start(image, entry) {
    const words = new Uint16Array(65536).fill(0xffff);
    for (let i = 0; i < image.length; i += 2) words[i / 2] = image.readUInt16LE(i);
    const cpu = new CPU(words, 16384);
    cpu.pc = entry / 2;
    cpu.data.set(stock.subarray(0x1b24a), 0x100);
    for (let i = 2; i < 32; i++) cpu.data[i] = (i * 51 + 7) & 255;
    cpu.data[cpu.SP] = 0xff;
    cpu.data[cpu.SP - 1] = 0xff;
    cpu.SP -= 2;
    return cpu;
}

function execute(cpu, networkRead) {
    const stack = cpu.SP;
    for (let i = 0; i < 1000000; i++) {
        if (cpu.pc === returnPC) {
            assert.equal(cpu.SP, stack + 2);
            assert.equal(cpu.data[1], 0);
            return;
        }
        if (networkRead && (cpu.pc === 0x3280 / 2 || cpu.pc === 0xcbf2 / 2)) {
            networkRead();
            cpu.pc = 0x28d2 / 2; // stock RET after injecting the simulated HTTP body
        }
        avrInstruction(cpu);
    }
    throw new Error('Unexpected firmware execution time at ' + (2 * cpu.pc).toString(16));
}

function percentEncode(image, input) {
    const cpu = start(image, 0x2ed0);
    const source = 0x1900, dest = 0x2300, end = dest + input.length * 3 + 12;
    cpu.data.fill(0xbe, dest - 2, end);
    cpu.data.set(input, source);
    cpu.data[source + input.length] = 0;
    cpu.dataView.setUint16(24, source, true);
    cpu.dataView.setUint16(22, dest, true);
    execute(cpu);
    assert.equal(cpu.dataView.getUint16(24, true), dest);
    return [cpu.data.slice(dest - 2, end), cpu.data.slice(2, 18), cpu.data.slice(28, 30)];
}

function parseFields(image, entry, body, ok) {
    const cpu = start(image, entry);
    cpu.data.fill(0xbe, 0x2460, 0x2490);
    cpu.data.fill(0xd6, 0x3000, 0x3060);
    for (let reg = 10; reg <= 24; reg += 2) {
        cpu.dataView.setUint16(reg, 0x3000 + (24 - reg) * 4, true);
    }
    let requests = 0;
    execute(cpu, () => {
        assert.equal(cpu.pc * 2, entry === 0x33a0 ? 0x3280 : 0xcbf2);
        const dest = cpu.dataView.getUint16(24, true);
        assert.equal(cpu.dataView.getUint16(22, true), 512);
        cpu.data.set(Buffer.from(body + '\0'), dest);
        cpu.data[24] = ok ? 1 : 0;
        requests++;
    });
    assert.equal(requests, 1);
    return [cpu.data[24], cpu.data.slice(0x2460, 0x2490), cpu.data.slice(0x3000, 0x3060),
            cpu.data.slice(2, 18), cpu.data.slice(28, 30)];
}

const inputs = Array.from({length: 256}, (_, i) => [i]);
for (let i = 0; i < 48; ++i) inputs.push(Array.from({length: 40}, (_, j) => (i * 37 + j * 43) % 256));
for (const input of inputs) assert.deepEqual(percentEncode(candidate, input), percentEncode(stock, input));

const responses = [['', false], ['', true], ['!', true], ['1/2/3/4/5/6/', true],
                   ['15/-2/0/425/-15/65535/', true]];
for (let i = 0; i < 50; i++) {
    responses.push([Array.from({length: 6}, (_, j) => (i + 3) * (j - 2)).join('/') + '/', true]);
}
for (const entry of [0x33a0, 0xccba]) {
    for (const [body, ok] of responses) {
        assert.deepEqual(parseFields(candidate, entry, body, ok), parseFields(stock, entry, body, ok));
    }
}
console.log(inputs.length + ' encoding and ' + (responses.length * 2) + ' transport parser comparisons passed');
