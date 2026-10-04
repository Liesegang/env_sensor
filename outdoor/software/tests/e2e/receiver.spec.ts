import { expect, test } from '@playwright/test';
import type { Page } from '@playwright/test';
import vectors from '../fixtures/wire.json' with { type: 'json' };
import { CHARACTERISTIC_UUID, SERVICE_UUID } from '../../src/protocol';

declare global {
  interface Window {
    __ble: { notify: (hex: string) => void; disconnect: () => void; resolveSelection: () => void; subscriptions: number; clockWrites: number[] };
  }
}

async function installBle(page: Page, mode: 'normal' | 'cancel' | 'delayed' | 'clock-fail' = 'normal', packetHex: string = vectors.all) {
  await page.addInitScript(({ packet, serviceUuid, characteristicUuid, mode }) => {
    function toView(hex: string) { return new DataView(Uint8Array.from(hex.match(/../g) ?? [], pair => parseInt(pair, 16)).buffer); }
    class Characteristic extends EventTarget {
      value = toView(packet);
      async writeValueWithResponse(bytes: Uint8Array<ArrayBuffer>) {
        if (mode === 'clock-fail') throw new Error('Write not permitted');
        const data = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        if (data.byteLength !== 12 || data.getUint32(0, true) !== 0x0001544f) throw new Error('Wrong time packet');
        window.__ble.clockWrites.push(Number(data.getBigUint64(4, true)));
      }
      async startNotifications() { window.__ble.subscriptions++; return this; }
      async readValue() { this.value = toView(packet); this.dispatchEvent(new Event('characteristicvaluechanged')); return this.value; }
      notify(hex: string) { this.value = toView(hex); this.dispatchEvent(new Event('characteristicvaluechanged')); }
    }
    const characteristic = new Characteristic();
    const device = new EventTarget();
    const gatt = {
      connected: false,
      async connect() { this.connected = true; return this; },
      async getPrimaryService(uuid: string) {
        if (uuid !== serviceUuid) throw new Error('Wrong service UUID');
        return { async getCharacteristic(uuid: string) {
          if (uuid !== characteristicUuid) throw new Error('Wrong characteristic UUID');
          return characteristic;
        } };
      },
      disconnect() { this.connected = false; device.dispatchEvent(new Event('gattserverdisconnected')); },
    };
    Object.assign(device, { name: 'Outdoor Sensor', gatt });
    let resolveSelection = () => {};
    Object.defineProperty(navigator, 'bluetooth', { configurable: true, value: {
      async requestDevice(options: { filters: { services: string[] }[] }) {
        if (options.filters[0]?.services[0] !== serviceUuid) throw new Error('Wrong chooser filter');
        if (mode === 'cancel') throw new DOMException('Cancelled', 'NotFoundError');
        if (mode === 'delayed') return new Promise(resolve => { resolveSelection = () => resolve(device); });
        return device;
      },
    } });
    window.__ble = { notify: hex => characteristic.notify(hex), disconnect: () => gatt.disconnect(), resolveSelection: () => resolveSelection(), subscriptions: 0, clockWrites: [] };
  }, { packet: packetHex, serviceUuid: SERVICE_UUID, characteristicUuid: CHARACTERISTIC_UUID, mode });
}

function updatedFragments(): string[] {
  const bytes = Uint8Array.from(vectors.all.match(/../g) ?? [], pair => parseInt(pair, 16));
  const packet = new DataView(bytes.buffer);
  packet.setUint32(12, 43, true);
  packet.setBigUint64(16, packet.getBigUint64(16, true) + 1000n, true);
  packet.setUint32(30, 123400, true); // OPT4001's first field after block header + errno.
  const result: string[] = [];
  for (let offset = 0; offset < bytes.length; offset += 8) {
    const fragment = new Uint8Array(12 + Math.min(8, bytes.length - offset));
    const view = new DataView(fragment.buffer);
    fragment.set([0x4f, 0x46, 1, 0]); view.setUint32(4, 43, true);
    view.setUint16(8, bytes.length, true); view.setUint16(10, offset, true);
    fragment.set(bytes.slice(offset, offset + 8), 12);
    result.push(Array.from(fragment, value => value.toString(16).padStart(2, '0')).join(''));
  }
  return result;
}

test('initial screen, responsive layout and detail disclosure', async ({ page }) => {
  await installBle(page);
  await page.setViewportSize({ width: 1536, height: 1024 });
  const errors: string[] = [];
  page.on('pageerror', error => errors.push(error.message));
  page.on('console', message => {
    if (message.type() === 'error' || message.type() === 'warning') errors.push(message.text());
  });
  await page.goto('/');
  expect(new URL(page.url()).origin).toBe('http://127.0.0.1:5178');
  await expect(page).toHaveTitle('Outdoor · 屋外センサーモニター');
  await expect(page.locator('vite-error-overlay')).toHaveCount(0);
  await expect(page.getByRole('heading', { name: '屋外センサーモニター' })).toBeVisible();
  await expect(page.getByTestId('metric-temperature')).toHaveText('—');
  await expect(page.getByRole('button', { name: 'CSV保存' })).toBeDisabled();
  await page.screenshot({ path: '/tmp/outdoor-desktop.png', fullPage: true });
  await page.getByRole('button', { name: '詳細を表示' }).click();
  await expect(page.getByText(SERVICE_UUID, { exact: true })).toBeVisible();
  await page.getByRole('button', { name: '詳細を閉じる' }).click();
  await page.getByRole('button', { name: 'SHT45 湿度', exact: true }).click();
  await expect(page.getByRole('button', { name: 'SHT45 湿度', exact: true })).toHaveAttribute('aria-pressed', 'false');
  await page.setViewportSize({ width: 390, height: 844 });
  await expect(page.getByRole('heading', { name: '屋外センサーモニター' })).toBeVisible();
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
  await page.screenshot({ path: '/tmp/outdoor-mobile.png', fullPage: true });
  expect(errors).toEqual([]);
});

test('BLE selection, notify subscription, initial read, fragmented updates, CSV and disconnect cleanup', async ({ page }) => {
  await installBle(page);
  await page.setViewportSize({ width: 1536, height: 1024 });
  await page.goto('/');
  await page.getByRole('button', { name: 'BLE接続' }).click();
  await expect(page.getByRole('button', { name: '切断', exact: true })).toBeVisible();
  await expect(page.getByTestId('metric-temperature')).toHaveText('23.27');
  await expect(page.getByText(/時刻同期済み（SDログ用）/)).toBeVisible();
  expect(await page.evaluate(() => window.__ble.clockWrites.length)).toBe(1);
  await expect(page.getByTestId('metric-co2')).toHaveText('391');
  await expect(page.getByTestId('metric-voc')).toHaveText('100');
  await expect(page.getByTestId('metric-nox')).toHaveText('1');
  await expect(page.getByRole('complementary').getByText('STCC4', { exact: true })).toBeVisible();
  await expect(page.getByRole('complementary').getByText('SHT45', { exact: true })).toBeVisible();
  await expect(page.getByText('STCC4 / SHT45', { exact: true })).toHaveCount(0);
  const sht45Row = page.getByRole('row').filter({ hasText: 'SHT45 温度' });
  await expect(sht45Row.getByRole('cell', { name: 'SHT45', exact: true })).toBeVisible();
  const chart = page.getByRole('img', { name: '測定推移グラフ' });
  await expect(chart.locator('[data-series="bme690.pressure_pa"]')).toHaveCount(1);
  await expect(chart.locator('[data-series="sgp41.voc_index"]')).toHaveCount(1);
  await expect(chart.locator('[data-series="sgp41.nox_index"]')).toHaveCount(1);
  await expect(chart.locator('[data-series="sht45.temperature_mc"]')).toHaveCount(1);
  await expect(chart.locator('[data-series="sfa40.hcho_millippb"]')).toHaveCount(1);
  await page.getByRole('button', { name: 'BME690 気圧', exact: true }).click();
  await expect(chart.locator('[data-series="bme690.pressure_pa"]')).toHaveCount(0);
  await page.getByRole('button', { name: 'すべて非表示' }).click();
  await expect(chart.locator('[data-series]')).toHaveCount(0);
  await page.getByRole('button', { name: 'SGP41 NOx Index', exact: true }).click();
  await expect(chart.locator('[data-series]')).toHaveCount(1);
  const gasAxisLabels = chart.locator('text.chart-label').filter({ hasNotText: ':' });
  expect((await gasAxisLabels.allTextContents()).map(Number).every(value => value >= 1 && value <= 500)).toBe(true);
  await page.getByRole('button', { name: 'すべて表示' }).click();
  await expect(chart.locator('[data-series="bme690.pressure_pa"]')).toHaveCount(1);
  await expect(page.getByRole('cell', { name: 'VOC raw', exact: true })).toHaveCount(0);
  await page.getByRole('button', { name: '詳細を表示' }).click();
  await expect(page.getByRole('cell', { name: 'VOC raw', exact: true })).toBeVisible();
  await page.getByRole('button', { name: '詳細を閉じる' }).click();
  await expect(page.getByText(/Outdoor Sensor.*1件/)).toBeVisible();
  expect(await page.evaluate(() => window.__ble.subscriptions)).toBe(1);
  await page.evaluate(fragments => fragments.forEach(hex => window.__ble.notify(hex)), updatedFragments());
  await expect(page.getByTestId('metric-light')).toHaveText('123.40');
  await expect(page.getByText(/Outdoor Sensor.*2件/)).toBeVisible();
  const download = page.waitForEvent('download');
  await page.getByRole('button', { name: 'CSV保存' }).click();
  expect((await download).suggestedFilename()).toMatch(/^outdoor-.*\.csv$/);
  await page.screenshot({ path: '/tmp/outdoor-connected.png', fullPage: true });
  await page.setViewportSize({ width: 390, height: 844 });
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
  await expect(page.getByTestId('metric-voc')).toHaveText('100');
  expect(await page.locator('.metric-value').evaluateAll(elements => elements.every(element => {
    const value = element.getBoundingClientRect();
    const container = element.closest('.metric')!.getBoundingClientRect();
    return value.left >= container.left && value.right <= container.right;
  }))).toBe(true);
  await page.screenshot({ path: '/tmp/outdoor-mobile-connected.png', fullPage: true });
  await page.getByRole('button', { name: '切断', exact: true }).click();
  await expect(page.getByText('未接続', { exact: true })).toBeVisible();
  await page.evaluate(() => window.__ble.notify('00'));
  await expect(page.getByRole('alert')).toHaveCount(0);
  await page.getByRole('button', { name: 'BLE接続' }).click();
  await expect(page.getByText(/Outdoor Sensor.*1件/)).toBeVisible();
});

test('gas algorithm warmup hides zero indices while keeping other readings available', async ({ page }) => {
  const bytes = Uint8Array.from(vectors.current.match(/../g) ?? [], pair => parseInt(pair, 16));
  const packet = new DataView(bytes.buffer);
  let offset = 24;
  while (packet.getUint8(offset) !== 3) offset += 4 + packet.getUint16(offset + 2, true);
  const end = offset + 4 + packet.getUint16(offset + 2, true);
  bytes.fill(0, end - 6, end); // Indices and validity flags; the raw measurement remains valid.
  await installBle(page, 'normal', Array.from(bytes, value => value.toString(16).padStart(2, '0')).join(''));
  await page.goto('/');
  await page.getByRole('button', { name: 'BLE接続' }).click();
  await expect(page.getByTestId('metric-voc')).toHaveText('—');
  await expect(page.getByTestId('metric-nox')).toHaveText('—');
  await expect(page.getByRole('complementary').getByText('暖機中', { exact: true })).toBeVisible();
  await expect(page.getByTestId('metric-co2')).toHaveText('391');
});

test('malformed notifications show an error without overwriting valid readings', async ({ page }) => {
  await installBle(page);
  await page.goto('/');
  await page.getByRole('button', { name: 'BLE接続' }).click();
  await expect(page.getByTestId('metric-co2')).toHaveText('391');
  await page.evaluate(() => window.__ble.notify('00'));
  await expect(page.getByRole('alert')).toContainText('ヘッダーが不正');
  await expect(page.getByTestId('metric-co2')).toHaveText('391');
  await page.evaluate(fragments => fragments.forEach(hex => window.__ble.notify(hex)), updatedFragments());
  await expect(page.getByRole('alert')).toHaveCount(0);
});

test('device chooser cancellation is actionable and can be retried', async ({ page }) => {
  await installBle(page, 'cancel');
  await page.goto('/');
  await page.getByRole('button', { name: 'BLE接続' }).click();
  await expect(page.getByRole('alert')).toContainText('キャンセル');
  await expect(page.getByRole('button', { name: 'BLE接続' })).toBeEnabled();
});

test('cancelling a pending chooser does not connect its late result', async ({ page }) => {
  await installBle(page, 'delayed');
  await page.goto('/');
  await page.getByRole('button', { name: 'BLE接続' }).click();
  await page.getByRole('button', { name: '接続を中止' }).click();
  await page.evaluate(() => window.__ble.resolveSelection());
  await expect(page.getByRole('button', { name: 'BLE接続' })).toBeVisible();
  await expect(page.getByTestId('metric-temperature')).toHaveText('—');
  expect(await page.evaluate(() => window.__ble.subscriptions)).toBe(0);
});

test('unsupported browsers explain the required browser', async ({ page }) => {
  await page.addInitScript(() => Object.defineProperty(navigator, 'bluetooth', { value: undefined }));
  await page.goto('/');
  await expect(page.getByRole('alert')).toContainText('ChromeまたはEdge');
  await expect(page.getByRole('button', { name: 'BLE接続' })).toBeDisabled();
});

test('unexpected disconnection marks readings stale and allows reconnecting', async ({ page }) => {
  await installBle(page);
  await page.goto('/');
  await page.getByRole('button', { name: 'BLE接続' }).click();
  await expect(page.getByTestId('metric-temperature')).toHaveText('23.27');
  await page.evaluate(() => window.__ble.disconnect());
  await expect(page.getByRole('alert')).toContainText('接続が切れました');
  await expect(page.getByText(/更新が停止/)).toBeVisible();
  await expect(page.getByRole('button', { name: 'BLE接続' })).toBeEnabled();
});

test('clock write failure stays visible while telemetry continues', async ({ page }) => {
  await installBle(page, 'clock-fail');
  await page.goto('/');
  await page.getByRole('button', { name: 'BLE接続' }).click();
  await expect(page.getByTestId('metric-co2')).toHaveText('391');
  await expect(page.getByText(/SDログの時刻同期: Write not permitted/)).toBeVisible();
  await page.evaluate(fragments => fragments.forEach(hex => window.__ble.notify(hex)), updatedFragments());
  await expect(page.getByTestId('metric-light')).toHaveText('123.40');
  await expect(page.getByText(/SDログの時刻同期: Write not permitted/)).toBeVisible();
  await expect(page.getByText(/時刻同期済み（SDログ用）/)).toHaveCount(0);
});
