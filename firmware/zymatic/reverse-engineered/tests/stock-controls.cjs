'use strict';

// Run the unmodified Zymatic 1.1.14 AVR instructions. Peripheral calls can
// return scripted values; no serial port, network or physical I/O is used.
const assert = require('node:assert/strict');
const { createHash } = require('node:crypto');
const { readFileSync } = require('node:fs');
const { CPU, avrInstruction } = require('avr8js');
const stock = require('../stock-controls.cjs');

const HEX_SHA = 'eb04c9dafdae032e7770b142f868246caf82eed4d80a0989552fdfd5dfee7b7c';
const APP_SHA = 'd6bff3e9c49c092a9ed825b91790c0adbc46be7112f3d79f26d9a30d28e0a4cd';
const sha256 = data => createHash('sha256').update(data).digest('hex');

function loadStock(path) {
  const hex = readFileSync(path);
  assert.equal(sha256(hex), HEX_SHA, 'requires the unmodified 1.1.14 HEX');
  const app = Buffer.alloc(113610);
  let segment = 0, cursor = 0, eof = false;
  for (const text of hex.toString('ascii').trimEnd().split(/\r?\n/)) {
    assert(!eof && /^:(?:[0-9a-f]{2})+$/i.test(text));
    const row = Buffer.from(text.slice(1), 'hex');
    assert.equal(row.reduce((sum, b) => sum + b, 0) & 255, 0);
    assert.equal(row.length, row[0] + 5);
    if (row[3] === 2) {
      segment = row.readUInt16BE(4) * 16;
    } else if (row[3] === 0) {
      assert.equal(cursor, segment + row.readUInt16BE(1));
      assert(cursor + row[0] <= app.length);
      row.copy(app, cursor, 4, 4 + row[0]);
      cursor += row[0];
    } else {
      assert.equal(row[3], 1);
      eof = true;
    }
  }
  assert(eof && cursor === app.length);
  assert.equal(sha256(app), APP_SHA);
  const words = new Uint16Array(65536);
  words.fill(0xffff);
  for (let i = 0; i < app.length; i += 2) words[i / 2] = app.readUInt16LE(i);
  return words;
}

const hexFile = process.argv[2] || process.env.ZYMATIC_STOCK_HEX;
if (!hexFile) throw Error('Pass the path to the original Zymatic_1_1_14.hex');
const words = loadStock(hexFile);
const cpu = new CPU(words, 16384);
const fileOffset = 0x1b24a;
for (let i = 0; i < 0x980; i++) {
  const offset = fileOffset + i;
  cpu.data[0x100 + i] = words[offset >> 1] >> ((offset & 1) * 8);
}
const ret = 0x28d2; // OEM RET, used to return from scripted hardware calls.
const endPC = 0xffff;
let checks = 0;

function run(entry, calls = {}) {
  const sp = cpu.SP;
  cpu.data[sp] = endPC & 255;
  cpu.data[sp - 1] = endPC >> 8;
  cpu.SP = sp - 2;
  execute(entry, [endPC * 2], calls);
  assert.equal(cpu.SP, sp);
  return cpu.data[24];
}

function execute(entry, exits, calls = {}) {
  cpu.pc = entry / 2;
  for (let steps = 0; steps < 500000; steps++) {
    if (exits.includes(cpu.pc * 2)) {
      checks++;
      return cpu.pc * 2;
    }
    const handler = calls[cpu.pc * 2];
    if (handler) {
      handler();
      cpu.pc = ret / 2;
    }
    avrInstruction(cpu);
  }
  throw Error('AVR execution exceeded bound at byte address 0x' + (cpu.pc * 2).toString(16));
}

function input(pin, value) {
  assert.equal(cpu.data[24], pin);
  cpu.dataView.setUint16(24, value, true);
}

const word = (reg, value) => cpu.dataView.setUint16(reg, value, true);
const float = (address, value) => cpu.dataView.setFloat32(address, value, true);
const digitalRead = 0x1a066;
const delay = 0x19db0;

// Recipe locations are turned into indexes of the calibrated arm stop table.
// The wire input is a byte; characterize all 256 rather than extrapolating.
for (let location = 0; location < 256; location++) {
  cpu.data[24] = location;
  assert.equal(run(0x28b4), stock.armStopIndex(location));
}

// An initial high returns immediately. A low must persist across a 5 ms
// debounce. Exercise every combination and check the requests and their order.
for (const first of [0, 1]) for (const second of [0, 1]) {
  const reads = first ? [first] : [first, second];
  let delayed = false;
  assert.equal(run(0x37c4, {
    [digitalRead]: () => {
      if (!first && reads.length === 1) assert(delayed);
      input(23, reads.shift());
    },
    [delay]: () => {
      assert.equal(cpu.dataView.getUint32(22, true), 5);
      assert(!delayed && !first && reads.length === 1);
      delayed = true;
    },
  }), Number(stock.filterRemoved(first, second)));
  assert.equal(reads.length, 0);
  assert.equal(delayed, !first);
}

// OEM assert codes printed in the brew loop. Give these inputs their original
// logical numbers; the harness does not assert the wiring of a physical board.
for (const pump of [0, 1]) for (const heater of [0, 1]) for (const backup of [0, 1]) {
  const values = {0: pump, 4: heater, 13: backup};
  assert.equal(run(0x3ab6, {
    [digitalRead]: () => input(cpu.data[24], values[cpu.data[24]]),
  }), stock.outputFault(pump, heater, backup));
}

// Three differential comparisons on either heat channel. The original
// floating-point subtraction/comparison library executes in the emulator.
const wort = [0x230e, 0x2312, 0x23dc];
const heat1 = [0x231a, 0x231e, 0x23e0];
const heat2 = [0x2326, 0x232a, 0x23e8];
const histories = [
  [49.75, 50, 50.25], [50, 50, 50], [50, 49.75, 51], [55, 55, 49.75], [-55, -55, -55],
];
for (const enabled of [0, 1]) for (const a of histories) for (const b of histories) {
  for (let i = 0; i < 3; i++) {
    float(wort[i], 100);
    float(heat1[i], 100 + a[i]);
    float(heat2[i], 100 + b[i]);
  }
  const hit = stock.heatDifferenceFault(enabled, [100, 100, 100], a.map(d => 100 + d), b.map(d => 100 + d));
  assert.equal(run(0x3954, {[digitalRead]: () => input(13, enabled)}), Number(hit));
}

// Three consecutive rises of at least 28 F. In the backup branch the final
// comparison uses the current PRIMARY heat value minus the backup's last
// history sample. Retain the observed asymmetry when modeling OEM behavior.
const history1 = [0x231a, 0x231e, 0x2322, 0x23e0];
const history2 = [0x2326, 0x232a, 0x232e, 0x23e8];
const rises = [
  [100, 128, 156, 184], [100, 128, 156, 183.75], [100, 127.75, 156, 190],
  [100, 128, 155.75, 200], [100, 100, 100, 100], [90, 120, 151, 182],
];
for (const enabled of [0, 1]) for (const a of rises) for (const b of rises) {
  a.forEach((value, i) => float(history1[i], value));
  b.forEach((value, i) => float(history2[i], value));
  const hit = stock.heatRiseFault(enabled, a, b);
  assert.equal(run(0x37fc, {[digitalRead]: () => input(13, enabled)}), Number(hit));
}

// Shutdown shadow: the physical digital-write and EEPROM transports are
// replaced with event recorders, but the shutdown/latch/delay code is OEM.
for (const code of [1, 3, 9, 17, 64]) {
  const events = [];
  cpu.data[24] = code;
  cpu.data[0x2334] = 0x80;
  run(0x4c74, {
    0x1a000: () => events.push(['digital', cpu.data[24], cpu.data[22]]),
    [delay]: () => events.push(['delay', cpu.dataView.getUint32(22, true)]),
    0x495c: () => events.push(['persist', cpu.dataView.getUint16(24, true), cpu.data[22]]),
  });
  assert.deepEqual(events, [['digital', 4, 0], ['digital', 24, 0], ['delay', 500],
    ['digital', 13, 0], ['persist', 121, 128 | code]]);
  assert.equal(cpu.data[0x2334], 128 | code);
}

// Writing the four corners and every position within a byte exercises the OEM
// pixel operation and its integer division library, independent of OLED SPI.
for (const [x, y] of [[0, 0], [127, 0], [0, 63], [127, 63], ...Array.from({length: 8}, (_, k) => [40 + k, 12])]) {
  const index = y * 16 + Math.floor(x / 8);
  const mask = 0x80 >> (x % 8);
  for (const color of [1, 0]) {
    cpu.data.fill(color ? 0 : 255, 0x266a, 0x2a6a);
    word(22, x); word(20, y); word(18, color);
    run(0x15c9c);
    const expected = Buffer.alloc(1024, color ? 0 : 255);
    expected[index] ^= mask;
    assert.deepEqual(Buffer.from(cpu.data.subarray(0x266a, 0x2a6a)), expected);
  }
}

// Absolute-temperature cutout inside TIMER3_COMPA. Stop before its subsequent
// history/PID housekeeping. The OEM float comparisons include the boundaries.
for (const wortF of [213.75, 214, 214.25]) for (const heatF of [224.75, 225, 225.25]) {
  float(0x23dc, wortF); float(0x23e0, heatF);
  cpu.data[0x230d] = 0;
  cpu.data[0x233b] = 1;
  const actions = [];
  execute(0x10b22, [0x10b8c], {
    0x1a000: () => actions.push([cpu.data[24], cpu.data[22]]),
    [delay]: () => actions.push(['wait', cpu.dataView.getUint32(22, true)]),
  });
  const trip = stock.absoluteHeatCutout(wortF, heatF);
  assert.deepEqual(actions, trip ? [[4, 0], ['wait', 500], [13, 0]] : []);
  assert.equal(cpu.data[0x230d], Number(trip));
  assert.equal(cpu.data[0x233b], Number(!trip));
}

// Replay the exact brew-loop block that maps each selected routing byte and
// dispatches a step. Don't energize a motor or run a fictitious physical step.
for (const recipe of [0, 1, 9]) for (const current of [0, 1, 24]) for (const location of [0, 1, 2, 3, 4, 5]) {
  const base = 0x16be + recipe * 172;
  word(28, 0x3500);
  cpu.data[0x3501] = recipe; word(0x3502, recipe * 172);
  cpu.data[base + 21] = 25; word(6, base + 21);
  word(0x23c0, current); cpu.data[base + 26 + current * 6] = location;
  const actions = [];
  const exit = execute(0xfbea, [0xfc58, 0xf83c], {
    0x754c: () => actions.push(['move', cpu.data[24]]),
    0x5386: () => {}, 0x3e10: () => {},
    0xe648: () => { actions.push(['step', cpu.data[24], cpu.data[22]]); cpu.data[24] = current + 1; },
    0x1a000: () => actions.push(['pin', cpu.data[24], cpu.data[22]]),
  });
  assert.deepEqual(actions, [['move', [0, 1, 3, 4, 5, 6][location]], ['step', recipe, current], ['pin', 24, 0]]);
  assert.equal(cpu.dataView.getUint16(0x23c0, true), current + 1);
  assert.equal(exit, current + 1 === 25 ? 0xfc58 : 0xf83c);
}

// A pause uses the separate user-interaction branch and does not dispatch an
// arm motor command just because 6 maps to 0 in the convenience function.
for (const location of [0, 1, 2, 3, 4, 5, 6]) {
  word(28, 0x3500); word(0x3502, 0); word(0x23c0, 1);
  cpu.data[0x16be + 26 + 6] = location;
  const exit = execute(0xf92a, [0xf954, 0xfb78], {});
  assert.equal(exit, location === 6 ? 0xf954 : 0xfb78);
}

function checkBrewButton() {
  // The physical press is still the first decision. Replay all four menu
  // selections through the unmodified OEM confirmation menu and its labels.
  const exits = [0xf660, 0xf644, 0xef12, 0xf67e, 0xf4a8];
  for (const [choice, destination] of [[null, 0xf660], [0, 0xf644], [1, 0xef12], [2, 0xf67e], [3, 0xf4a8]]) {
    cpu.data[0x2334] = 1;   // a pending fault must also take precedence over network polling
    let called = false;
    const actual = execute(0xf484, exits, {
      0x1122c: () => { assert.equal(cpu.dataView.getUint16(24, true), 0x2494); cpu.data[24] = Number(choice !== null); },
      0x1604e: () => {}, 0x4f50: () => {},
      0x7f9a: () => {
        assert(choice !== null);
        const labels = Array.from({length: 4}, (_, j) => {
          const a = 0x2010 + j * 21;
          return Buffer.from(cpu.data.subarray(a, cpu.data.indexOf(0, a))).toString('ascii').trim();
        });
        assert.deepEqual(labels, ['Continue', 'Exit Brew', 'Next Step', 'Drain/Pause']);
        called = true; cpu.data[24] = choice;
      },
    });
    assert.equal(called, choice !== null);
    assert.equal(actual, destination);
  }
}
checkBrewButton();

console.log('Stock Zymatic controls: %d AVR executions passed.', checks);

if (process.argv[3]) {
  const image = readFileSync(process.argv[3]);
  assert(image.length > 0x1bc00 && image.length <= 0x1e000);
  for (let i = 0; i < image.length; i += 2) words[i / 2] = image[i] | (image[i + 1] << 8);
  checkBrewButton();
  require('../../modified/tests/offline-recipe.cjs')({cpu, words, run, execute, word, image});
}
