import wireSchema from '../../protocol/outdoor-v1.json' with { type: 'json' };

export interface Field {
  readonly key: string;
  readonly type: string;
  readonly label: string;
  readonly scale?: number;
  readonly unit?: string;
  readonly format?: string;
  readonly source?: string;
}
interface SensorDefinition { readonly id: number; readonly key: string; readonly name: string; readonly version?: number; readonly fields: readonly Field[] }
export const schema: Omit<typeof wireSchema, 'sensors'> & { readonly sensors: readonly SensorDefinition[] } = wireSchema;
export const SERVICE_UUID = schema.serviceUuid;
export const CHARACTERISTIC_UUID = schema.characteristicUuid;
export type Scalar = number | string | boolean;
export interface SensorReading {
  readonly id: number;
  readonly error: number;
  readonly values: Readonly<Record<string, Scalar>>;
}
export interface Sample {
  readonly sequence: number;
  readonly uptimeMs: bigint;
  readonly enabledMask: number;
  readonly validMask: number;
  readonly sensors: ReadonlyMap<string, SensorReading>;
  readonly receivedAt: number;
}

const widths: Readonly<Record<string, number>> = { u8: 1, bool: 1, u16: 2, i16: 2, u32: 4, i32: 4, u64: 8 };
const byId = new Map(schema.sensors.map(sensor => [sensor.id, sensor]));

function readScalar(view: DataView, offset: number, field: Field): Scalar {
  switch (field.type) {
    case 'u8': return view.getUint8(offset);
    case 'bool': {
      const value = view.getUint8(offset);
      if (value > 1) throw new Error('不正なboolean値です');
      return value === 1;
    }
    case 'u16': return view.getUint16(offset, true);
    case 'i16': return view.getInt16(offset, true);
    case 'u32': return view.getUint32(offset, true);
    case 'i32': return view.getInt32(offset, true);
    case 'u64': {
      const value = view.getBigUint64(offset, true);
      return field.format === 'hex' ? `0x${value.toString(16).padStart(16, '0')}` : value.toString();
    }
    default: throw new Error(`未対応のデータ型: ${field.type}`);
  }
}

export function decodeSample(view: DataView, receivedAt = Date.now()): Sample {
  if (view.byteLength < schema.headerSize || view.byteLength > schema.maxSize) throw new Error('測定パケットの長さが不正です');
  if (view.getUint8(0) !== 0x4f || view.getUint8(1) !== 0x45) throw new Error('Outdoorのパケットではありません');
  if (view.getUint8(2) !== schema.version || view.getUint8(3) !== schema.headerSize) throw new Error('未対応のプロトコルバージョンです');
  if (view.getUint16(4, true) !== view.byteLength || view.getUint16(10, true) !== 0) throw new Error('測定ヘッダーが不正です');
  const enabledMask = view.getUint16(6, true);
  const validMask = view.getUint16(8, true);
  if (validMask & ~enabledMask) throw new Error('センサーの状態フラグが不正です');
  const sensors = new Map<string, SensorReading>();
  const seen = new Set<number>();
  let offset = schema.headerSize;
  while (offset < view.byteLength) {
    if (offset + 4 > view.byteLength) throw new Error('センサーヘッダーが欠けています');
    const id = view.getUint8(offset);
    const version = view.getUint8(offset + 1);
    const size = view.getUint16(offset + 2, true);
    offset += 4;
    if (seen.has(id) || size < 2 || offset + size > view.byteLength) throw new Error('センサーブロックが不正です');
    seen.add(id);
    const sensor = byId.get(id);
    if (sensor) {
      if (version !== (sensor.version ?? 1)) throw new Error(`${sensor.name}のデータ形式が未対応です。ファームウェアを更新してください。`);
      const bit = 1 << (id - 1);
      const error = view.getInt16(offset, true);
      if (!(enabledMask & bit) || error > 0 || Boolean(validMask & bit) !== (error === 0)) throw new Error('センサー状態とデータが一致しません');
      const expectedSize = error === 0 ? 2 + sensor.fields.reduce((sum, field) => sum + (widths[field.type] ?? 0), 0) : 2;
      if (size !== expectedSize) throw new Error(`${sensor.name}のデータ長が不正です`);
      const values: Record<string, Scalar> = {};
      if (error === 0) {
        let cursor = offset + 2;
        for (const field of sensor.fields) {
          values[field.key] = readScalar(view, cursor, field);
          cursor += widths[field.type] ?? 0;
        }
      }
      sensors.set(sensor.key, { id, error, values });
    }
    offset += size;
  }
  for (const sensor of schema.sensors) {
    if ((enabledMask & (1 << (sensor.id - 1))) && !sensors.has(sensor.key)) throw new Error(`${sensor.name}のブロックがありません`);
  }
  return { sequence: view.getUint32(12, true), uptimeMs: view.getBigUint64(16, true), enabledMask, validMask, sensors, receivedAt };
}

interface PartialFrame { sequence: number; bytes: Uint8Array; offset: number; startedAt: number }

export class FragmentAssembler {
  private pending: PartialFrame | undefined;
  reset(): void { this.pending = undefined; }

  push(view: DataView, now = performance.now()): Sample | undefined {
    try {
      if (view.byteLength <= schema.fragmentHeaderSize || view.getUint8(0) !== 0x4f || view.getUint8(1) !== 0x46 || view.getUint8(2) !== 1 || view.getUint8(3) !== 0) throw new Error('BLE通知のヘッダーが不正です');
      const sequence = view.getUint32(4, true);
      const length = view.getUint16(8, true);
      const offset = view.getUint16(10, true);
      const payloadLength = view.byteLength - schema.fragmentHeaderSize;
      if (length < schema.headerSize || length > schema.maxSize || offset + payloadLength > length) throw new Error('BLE通知の長さが不正です');
      if (offset === 0) this.pending = { sequence, bytes: new Uint8Array(length), offset: 0, startedAt: now };
      const frame = this.pending;
      if (!frame || frame.sequence !== sequence || frame.bytes.length !== length || frame.offset !== offset || now - frame.startedAt > 3000) throw new Error('BLE通知が欠落しました。次の測定を待ちます');
      frame.bytes.set(new Uint8Array(view.buffer, view.byteOffset + schema.fragmentHeaderSize, payloadLength), offset);
      frame.offset += payloadLength;
      if (frame.offset !== length) return undefined;
      this.reset();
      const sample = decodeSample(new DataView(frame.bytes.buffer));
      if (sample.sequence !== sequence) throw new Error('BLE通知の連番が一致しません');
      return sample;
    } catch (error) {
      this.reset();
      throw error;
    }
  }
}

/** OT v1: UTC Unix milliseconds; sent with an acknowledged GATT write. */
export function encodeClockSync(unixMs: number): Uint8Array<ArrayBuffer> {
  if (!Number.isSafeInteger(unixMs) || unixMs < 1577836800000 || unixMs >= 4102444800000) throw new Error('時刻が同期可能な範囲外です');
  const bytes = new Uint8Array(12);
  bytes.set([0x4f, 0x54, 1, 0]);
  new DataView(bytes.buffer).setBigUint64(4, BigInt(unixMs), true);
  return bytes;
}
