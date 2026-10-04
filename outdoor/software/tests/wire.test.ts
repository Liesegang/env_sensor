import { describe, expect, it } from 'vitest';
import vectors from './fixtures/wire.json' with { type: 'json' };
import { decodeSample, FragmentAssembler, encodeClockSync } from '../src/protocol';

export function view(hex: string): DataView {
  const bytes = Uint8Array.from(hex.match(/../g) ?? [], pair => parseInt(pair, 16));
  return new DataView(bytes.buffer);
}

describe('C firmware → TypeScript wire compatibility', () => {
  it('decodes all 7 sensors, signed values, units and 64-bit identities', () => {
    const sample = decodeSample(view(vectors.all), 1700000000000);
    expect(sample.sequence).toBe(42);
    expect(sample.uptimeMs).toBe(12345678901234n);
    expect(sample.sensors.size).toBe(7);
    expect(sample.sensors.get('opt4001')?.values.millilux).toBe(65944);
    expect(sample.sensors.get('bme690')?.values.temperature_mc).toBe(-1234);
    expect(sample.sensors.get('stcc4')?.values.serial_number).toBe('0xfedcba9876543210');
    expect(sample.sensors.get('stcc4')?.values.co2_ppm).toBe(391);
    expect(sample.sensors.get('sgp41')?.values.voc_index).toBe(100);
    expect(sample.sensors.get('sgp41')?.values.nox_index).toBe(1);
    expect(sample.sensors.get('sgp41')?.values.voc_index_valid).toBe(true);
    expect(sample.sensors.get('sgp41')?.values.nox_index_valid).toBe(true);
    expect(sample.sensors.get('bmv080')?.values.pm2_5_mass_milliug_m3).toBe(2500);
    expect(sample.sensors.get('as3935')?.values.lightning).toBe(true);
    expect(sample.sensors.get('sfa40')?.values.within_specification).toBe(false);
  });

  it('represents read errors without returning previous or zero measurements', () => {
    const sample = decodeSample(view(vectors.failed));
    expect(sample.validMask).toBe(0x79);
    expect(sample.sensors.get('bme690')).toEqual({ id: 2, error: -5, values: {} });
    expect(sample.sensors.get('sgp41')?.error).toBe(-11);
    expect(decodeSample(view(vectors.current)).sensors.has('bmv080')).toBe(false);
    expect(decodeSample(view(vectors.empty)).sensors.size).toBe(0);
  });

  it('rejects short, wrong-version, wrong-length and contradictory state packets', () => {
    expect(() => decodeSample(view('4f45'))).toThrow();
    for (const [offset, value] of [[0, 0], [2, 2], [3, 0], [4, 24], [10, 1], [6, 0], [8, 0]]) {
      const packet = view(vectors.all);
      packet.setUint8(offset!, value!);
      expect(() => decodeSample(packet)).toThrow();
    }
  });

  it('requests a firmware update for the older raw-only SGP41 block', () => {
    const packet = view(vectors.current);
    let offset = 24;
    while (packet.getUint8(offset) !== 3) offset += 4 + packet.getUint16(offset + 2, true);
    packet.setUint8(offset + 1, 1);
    expect(() => decodeSample(packet)).toThrow('SGP41のデータ形式が未対応です。ファームウェアを更新してください。');
  });

  it('rejects invalid booleans and sensor block sizes', () => {
    const packet = view(vectors.all);
    // Find BME690's block; its last two values are boolean flags.
    const block = 24 + 4 + packet.getUint16(26, true);
    const length = packet.getUint16(block + 2, true);
    packet.setUint8(block + 4 + length - 1, 2);
    expect(() => decodeSample(packet)).toThrow('boolean');
    packet.setUint8(block + 4 + length - 1, 1);
    packet.setUint16(block + 2, length - 1, true);
    expect(() => decodeSample(packet)).toThrow();
  });

  it('preserves uptime beyond JavaScript safe integer precision', () => {
    const packet = view(vectors.current);
    packet.setBigUint64(16, 9007199254740993n, true);
    expect(decodeSample(packet).uptimeMs).toBe(9007199254740993n);
  });

  it('honors DataView byte offsets rather than decoding unrelated buffer bytes', () => {
    const packet = view(vectors.all);
    const buffer = new Uint8Array(packet.byteLength + 11);
    buffer.set(new Uint8Array(packet.buffer), 7);
    expect(decodeSample(new DataView(buffer.buffer, 7, packet.byteLength)).sequence).toBe(42);
  });

  for (const key of ['fragments20', 'fragments244'] as const) {
    it(`reassembles C notifications at ${key === 'fragments20' ? 'ATT MTU 23' : 'ATT MTU 247'}`, () => {
      const assembler = new FragmentAssembler();
      const results = vectors[key].map((hex, index) => assembler.push(view(hex), index * 10));
      expect(results.slice(0, -1).every(result => result === undefined)).toBe(true);
      expect(results.at(-1)?.sensors.get('stcc4')?.values.co2_ppm).toBe(391);
    });
  }

  it('drops missing, out-of-order or timed-out fragments and recovers next frame', () => {
    const assembler = new FragmentAssembler();
    assembler.push(view(vectors.fragments20[0]!), 0);
    expect(() => assembler.push(view(vectors.fragments20[2]!), 1)).toThrow();
    assembler.push(view(vectors.fragments20[0]!), 0);
    expect(() => assembler.push(view(vectors.fragments20[1]!), 3001)).toThrow();
    let last;
    for (const hex of vectors.fragments244) last = assembler.push(view(hex), 4000);
    expect(last?.sequence).toBe(42);
  });

  it('rejects mixed sample sequences and mismatched inner sequence', () => {
    const assembler = new FragmentAssembler();
    assembler.push(view(vectors.fragments20[0]!));
    const second = view(vectors.fragments20[1]!);
    second.setUint32(4, 43, true);
    expect(() => assembler.push(second)).toThrow();
    const pieces = vectors.fragments244.map(view);
    pieces.forEach(piece => piece.setUint32(4, 43, true));
    expect(() => pieces.forEach(piece => assembler.push(piece))).toThrow('連番');
  });
});

describe('browser clock synchronization', () => {
  it('encodes UTC milliseconds exactly and rejects malformed times', () => {
    const bytes = encodeClockSync(1791114000123);
    expect(Array.from(bytes.slice(0, 4))).toEqual([0x4f, 0x54, 1, 0]);
    expect(new DataView(bytes.buffer).getBigUint64(4, true)).toBe(1791114000123n);
    for (const time of [NaN, Infinity, 0, 4102444800000, 1791114000123.5]) expect(() => encodeClockSync(time)).toThrow();
  });
});
