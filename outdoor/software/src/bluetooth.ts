import { CHARACTERISTIC_UUID, SERVICE_UUID, FragmentAssembler, decodeSample, encodeClockSync } from './protocol';
import type { Sample } from './protocol';

export type ConnectionPhase = 'idle' | 'requesting' | 'connecting' | 'connected' | 'error';
export interface ReceiverState {
  readonly phase: ConnectionPhase;
  readonly deviceName: string;
  readonly latest: Sample | undefined;
  readonly history: readonly Sample[];
  readonly error: string | undefined;
  readonly rejected: number;
  readonly clockSynced: boolean;
  readonly clockError: string | undefined;
}
export const HISTORY_LIMIT = 900;
const initial: ReceiverState = { phase: 'idle', deviceName: '', latest: undefined, history: [], error: undefined, rejected: 0, clockSynced: false, clockError: undefined };

function message(error: unknown): string {
  if (error instanceof DOMException && error.name === 'NotFoundError') return 'デバイスの選択をキャンセルしました。';
  if (error instanceof DOMException && error.name === 'SecurityError') return 'Bluetoothの利用をブラウザーで許可してください。';
  return error instanceof Error ? error.message : 'Bluetooth通信に失敗しました。';
}

export function supportMessage(): string | undefined {
  if (!window.isSecureContext) return 'HTTPSまたはlocalhostで開いてください。';
  if (!navigator.bluetooth) return 'BLE受信にはWeb Bluetooth対応のChromeまたはEdgeを使用してください。';
  return undefined;
}

export class BluetoothReceiver {
  private state: ReceiverState = initial;
  private listeners = new Set<() => void>();
  private assembler = new FragmentAssembler();
  private device: BluetoothDevice | undefined;
  private characteristic: BluetoothRemoteGATTCharacteristic | undefined;
  private epoch = 0;
  private busy = false;

  getSnapshot = (): ReceiverState => this.state;
  subscribe = (listener: () => void): (() => void) => {
    this.listeners.add(listener);
    return () => { this.listeners.delete(listener); };
  };

  private update(patch: Partial<ReceiverState>): void {
    this.state = { ...this.state, ...patch };
    for (const listener of this.listeners) listener();
  }

  private release(): void {
    this.characteristic?.removeEventListener('characteristicvaluechanged', this.onValue);
    this.device?.removeEventListener('gattserverdisconnected', this.onDisconnect);
    this.characteristic = undefined;
    this.device = undefined;
    this.assembler.reset();
  }

  private onDisconnect = (): void => {
    this.epoch++;
    this.busy = false;
    this.release();
    this.update({ phase: 'idle', error: '接続が切れました。BLE接続から再接続できます。' });
  };

  private accept(sample: Sample): void {
    // readValue may deliver the same snapshot as a notification, or an older one.
    if (this.state.latest && sample.uptimeMs <= this.state.latest.uptimeMs) return;
    this.update({ latest: sample, history: [...this.state.history.slice(-(HISTORY_LIMIT - 1)), sample], error: undefined });
  }

  private onValue = (): void => {
    const value = this.characteristic?.value;
    if (!value) return;
    try {
      const sample = value.byteLength >= 2 && value.getUint8(1) === 0x45 ? decodeSample(value) : this.assembler.push(value);
      if (sample) this.accept(sample);
    } catch (error) {
      this.update({ error: message(error), rejected: this.state.rejected + 1 });
    }
  };

  async connect(): Promise<void> {
    if (this.busy || this.state.phase === 'connected') return;
    const unsupported = supportMessage();
    if (unsupported) { this.update({ phase: 'error', error: unsupported }); return; }
    const epoch = ++this.epoch;
    this.busy = true;
    this.update({ phase: 'requesting', error: undefined, clockSynced: false, clockError: undefined });
    let selected: BluetoothDevice | undefined;
    const guard = (): void => {
      if (epoch !== this.epoch) {
        if (selected !== this.device) selected?.gatt?.disconnect();
        throw new DOMException('接続を中止しました', 'AbortError');
      }
    };
    try {
      // Keep the chooser directly in this user-gesture call, before other awaits.
      selected = await navigator.bluetooth.requestDevice({ filters: [{ services: [SERVICE_UUID] }] });
      guard();
      if (!selected.gatt) throw new Error('このデバイスはGATT接続をサポートしていません。');
      this.device = selected;
      selected.addEventListener('gattserverdisconnected', this.onDisconnect);
      this.update({ phase: 'connecting', deviceName: selected.name ?? 'Outdoor Sensor' });
      const server = await selected.gatt.connect();
      guard();
      const service = await server.getPrimaryService(SERVICE_UUID);
      guard();
      const characteristic = await service.getCharacteristic(CHARACTERISTIC_UUID);
      guard();
      // Same characteristic as telemetry: no second GATT endpoint is needed.
      try {
        await characteristic.writeValueWithResponse(encodeClockSync(Date.now()));
        guard();
        this.update({ clockSynced: true });
      } catch (error) {
        guard();
        this.update({ clockSynced: false, clockError: `SDログの時刻同期: ${message(error)} 再接続すると再試行します。` });
      }
      this.characteristic = characteristic;
      this.characteristic.addEventListener('characteristicvaluechanged', this.onValue);
      this.update({ latest: undefined, history: [], rejected: 0 });
      await this.characteristic.startNotifications();
      guard();
      this.update({ phase: 'connected' });
      // Notifications are already subscribed: the initial read cannot create a gap.
      try {
        const snapshot = await this.characteristic.readValue();
        guard();
        this.accept(decodeSample(snapshot));
      } catch (error) {
        guard();
        this.update({ error: `初回読み取り: ${message(error)} 通知を待っています。` });
      }
    } catch (error) {
      if (epoch === this.epoch) {
        this.release();
        selected?.gatt?.disconnect();
        this.update({ phase: 'error', error: message(error) });
      }
    } finally {
      if (epoch === this.epoch) this.busy = false;
    }
  }

  disconnect = (): void => {
    this.epoch++;
    this.busy = false;
    const device = this.device;
    this.release();
    device?.gatt?.disconnect();
    this.update({ phase: 'idle', error: undefined });
  };
}
