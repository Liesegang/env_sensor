import { describe, expect, it } from 'vitest';
import { createCsv, displaySensors, formatField, metricValue, sensorStatus, trendMetrics, trendValue } from '../src/measurements';
import { decodeSample, schema } from '../src/protocol';
import vectors from './fixtures/wire.json' with { type: 'json' };

function sample(hex: string) {
  const bytes = Uint8Array.from(hex.match(/../g) ?? [], value => parseInt(value, 16));
  return decodeSample(new DataView(bytes.buffer), 1700000000000);
}

describe('receiver measurements', () => {
  it('uses SHT45 first and correct physical units', () => {
    const frame = sample(vectors.all);
    expect(metricValue(frame, 'temperature')).toBeCloseTo(23.274);
    expect(metricValue(frame, 'humidity')).toBeCloseTo(54.908);
    expect(metricValue(frame, 'light')).toBeCloseTo(65.944);
    expect(metricValue(frame, 'pressure')).toBeCloseTo(1018.03);
    expect(metricValue(frame, 'co2')).toBe(391);
  });
  it('distinguishes absent, failed and warming sensors from valid zero', () => {
    const failed = sample(vectors.failed);
    expect(metricValue(failed, 'pressure')).toBeUndefined();
    expect(metricValue(undefined, 'temperature')).toBeUndefined();
    expect(sensorStatus(failed, 2, 'bme690')).toBe('エラー -5');
    expect(sensorStatus(failed, 3, 'sgp41')).toBe('準備中');
    expect(sensorStatus(sample(vectors.current), 5, 'bmv080')).toBe('無効');
    expect(sensorStatus(sample(vectors.all), 7, 'sfa40')).toBe('安定化中');
  });
  it('falls back to BME690 if the SHT45 read fails', () => {
    const frame = sample(vectors.all);
    const sensors = new Map(frame.sensors);
    sensors.set('stcc4', { id: 4, error: -5, values: {} });
    expect(metricValue({ ...frame, sensors }, 'temperature')).toBeCloseTo(-1.234);
    expect(metricValue({ ...frame, sensors }, 'co2')).toBeUndefined();
  });
  it('shows computed gas indices and hides values during algorithm blackout', () => {
    const frame = sample(vectors.all);
    expect(metricValue(frame, 'voc')).toBe(100);
    expect(metricValue(frame, 'nox')).toBe(1);
    const sensors = new Map(frame.sensors);
    const sgp41 = sensors.get('sgp41')!;
    sensors.set('sgp41', { ...sgp41, values: { ...sgp41.values, nox_valid: true, nox_index_valid: false, nox_index: 0 } });
    expect(metricValue({ ...frame, sensors }, 'voc')).toBe(100);
    expect(metricValue({ ...frame, sensors }, 'nox')).toBeUndefined();
    expect(sensorStatus({ ...frame, sensors }, 3, 'sgp41')).toBe('暖機中');
    expect(metricValue(sample(vectors.failed), 'voc')).toBeUndefined();
  });
  it('separates STCC4 and SHT45 display ownership without changing the wire layout', () => {
    const stcc4 = displaySensors.find(sensor => sensor.key === 'stcc4')!;
    const sht45 = displaySensors.find(sensor => sensor.key === 'sht45')!;
    expect(stcc4.name).toBe('STCC4');
    expect(stcc4.fields.map(field => field.key)).toContain('co2_ppm');
    expect(stcc4.fields.map(field => field.key)).not.toContain('temperature_mc');
    expect(sht45.name).toBe('SHT45');
    expect(sht45.fields.map(field => field.key)).toEqual(['temperature_mc', 'humidity_mpercent', 'raw_temperature', 'raw_humidity']);
    expect(sht45.wireKey).toBe('stcc4');
  });
  it('graphs physical readings per sensor and excludes invalid warmup values', () => {
    const frame = sample(vectors.all);
    const get = (key: string, input = frame) => trendValue(input, trendMetrics.find(metric => metric.key === key)!);
    expect(get('bme690.pressure_pa')).toBeCloseTo(1018.03);
    expect(get('sht45.temperature_mc')).toBeCloseTo(23.274);
    expect(get('bme690.temperature_mc')).toBeCloseTo(-1.234);
    expect(trendMetrics.some(metric => metric.field.key.startsWith('raw_') || metric.field.unit === 'ms')).toBe(false);
    const sensors = new Map(frame.sensors);
    const hcho = sensors.get('sfa40')!;
    sensors.set('sfa40', { ...hcho, values: { ...hcho.values, ready: false } });
    expect(get('sfa40.hcho_millippb', { ...frame, sensors })).toBeUndefined();
    expect(get('sfa40.temperature_mc', { ...frame, sensors })).toBeDefined();
    expect(get('bme690.pressure_pa', sample(vectors.failed))).toBeUndefined();
  });
  it('exports all raw fields, exact serial numbers and error columns in UTF-8 CSV', () => {
    const csv = createCsv([sample(vectors.all), sample(vectors.failed)]);
    expect(csv.startsWith('\uFEFF')).toBe(true);
    expect(csv).toContain('"stcc4.serial_number"');
    expect(csv).toContain('"0xfedcba9876543210"');
    expect(csv).toContain('"12345678901234"');
    expect(csv).toContain('"bmv080.pm2_5_mass_milliug_m3"');
    expect(csv).toContain('"sgp41.voc_index"');
    expect(csv).toContain('"-5","","","",""');
    expect(csv.split('\r\n')).toHaveLength(4);
  });
  it('formats flags, raw hexadecimal values and scaled readings without inventing missing data', () => {
    const lux = schema.sensors[0]!.fields[0]!;
    expect(formatField(undefined, lux)).toBe('—');
    expect(formatField(65944, lux)).toBe('65.944');
    expect(formatField(true, lux)).toBe('Yes');
  });
});
