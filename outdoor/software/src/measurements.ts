import { schema } from './protocol';
import type { Field, Sample, Scalar } from './protocol';

export type Metric = 'temperature' | 'humidity' | 'co2' | 'light' | 'pressure' | 'voc' | 'nox';
export const metrics: readonly { key: Metric; label: string; unit: string; decimals: number }[] = [
  { key: 'temperature', label: '温度', unit: '°C', decimals: 2 },
  { key: 'humidity', label: '湿度', unit: '%RH', decimals: 2 },
  { key: 'co2', label: 'CO₂', unit: 'ppm', decimals: 0 },
  { key: 'light', label: '照度', unit: 'lx', decimals: 2 },
  { key: 'pressure', label: '気圧', unit: 'hPa', decimals: 2 },
  { key: 'voc', label: 'VOC Index', unit: 'Index', decimals: 0 },
  { key: 'nox', label: 'NOx Index', unit: 'Index', decimals: 0 },
];

/* SHT45 values travel in the STCC4 block, but belong to a separate sensor. */
export const displaySensors = schema.sensors.flatMap(sensor => {
  const sources = new Set([sensor.key, ...sensor.fields.map(field => field.source ?? sensor.key)]);
  return [...sources].map(key => ({
    id: sensor.id, key, wireKey: sensor.key,
    name: key === 'sht45' ? 'SHT45' : sensor.name,
    fields: sensor.fields.filter(field => (field.source ?? sensor.key) === key),
  }));
});

function numeric(sample: Sample, sensor: string, field: string): number | undefined {
  const reading = sample.sensors.get(sensor);
  const value = reading?.values[field];
  return reading?.error === 0 && typeof value === 'number' ? value : undefined;
}

export function metricValue(sample: Sample | undefined, key: Metric): number | undefined {
  if (!sample) return undefined;
  switch (key) {
    case 'temperature': return scaled(numeric(sample, 'stcc4', 'temperature_mc') ?? numeric(sample, 'bme690', 'temperature_mc'), .001);
    case 'humidity': return scaled(numeric(sample, 'stcc4', 'humidity_mpercent') ?? numeric(sample, 'bme690', 'humidity_mpercent'), .001);
    case 'co2': return numeric(sample, 'stcc4', 'co2_ppm');
    case 'light': return scaled(numeric(sample, 'opt4001', 'millilux'), .001);
    case 'pressure': return scaled(numeric(sample, 'bme690', 'pressure_pa'), .01);
    case 'voc': return sample.sensors.get('sgp41')?.values.voc_index_valid === true ? numeric(sample, 'sgp41', 'voc_index') : undefined;
    case 'nox': return sample.sensors.get('sgp41')?.values.nox_index_valid === true ? numeric(sample, 'sgp41', 'nox_index') : undefined;
  }
}
function scaled(value: number | undefined, factor: number): number | undefined {
  return value === undefined ? undefined : value * factor;
}

export interface TrendMetric { readonly key: string; readonly sensor: string; readonly field: Field; readonly label: string; readonly unit: string }
// Physical readings keep their sensor ownership; uptime, raw values and status flags stay in the detail table.
export const trendMetrics: readonly TrendMetric[] = displaySensors.flatMap(sensor => sensor.fields
  .filter(field => field.unit && field.unit !== 'ms' || sensor.key === 'as3935' && field.key === 'energy')
  .map(field => ({ key: `${sensor.key}.${field.key}`, sensor: sensor.wireKey, field,
    label: `${sensor.name} ${field.label.replace(/^SHT45 /, '')}`, unit: field.unit ?? '' })));

export function trendValue(sample: Sample, metric: TrendMetric): number | undefined {
  const values = sample.sensors.get(metric.sensor)?.values;
  if (metric.sensor === 'sgp41' && values?.[`${metric.field.key}_valid`] !== true) return undefined;
  if (metric.sensor === 'sfa40' && metric.field.key === 'hcho_millippb' && values?.ready !== true) return undefined;
  if (metric.sensor === 'bme690' && metric.field.key === 'gas_resistance_ohm' && (!values?.gas_valid || !values.heater_stable)) return undefined;
  if (metric.sensor === 'as3935' && (values?.lightning !== true || metric.field.key === 'distance_km' && values.distance_valid !== true)) return undefined;
  const value = numeric(sample, metric.sensor, metric.field.key);
  return value === undefined || !Number.isFinite(value) ? undefined : value * (metric.field.scale ?? 1);
}

export function sensorStatus(sample: Sample | undefined, id: number, key: string): string {
  if (!sample) return id <= 4 ? '接続待ち' : '未追加';
  if (!(sample.enabledMask & (1 << (id - 1)))) return '無効';
  const reading = sample.sensors.get(key);
  if (!reading) return '未対応';
  if (reading.error === -11) return '準備中';
  if (reading.error !== 0) return `エラー ${reading.error}`;
  if (key === 'sgp41' && (reading.values.conditioning || !reading.values.voc_index_valid || !reading.values.nox_index_valid)) return '暖機中';
  if (key === 'sfa40' && !reading.values.ready) return '暖機中';
  if (key === 'sfa40' && !reading.values.within_specification) return '安定化中';
  return '取得中';
}

export function formatField(value: Scalar | undefined, field: Field): string {
  if (value === undefined) return '—';
  if (typeof value === 'boolean') return value ? 'Yes' : 'No';
  if (typeof value === 'string') return value;
  if (field.format === 'hex') return `0x${value.toString(16).padStart(2, '0')}`;
  return (value * (field.scale ?? 1)).toLocaleString('ja-JP', { maximumFractionDigits: field.scale ? 3 : 0 });
}

export function createCsv(samples: readonly Sample[]): string {
  const columns = schema.sensors.flatMap(sensor => [`${sensor.key}.error`, ...sensor.fields.map(field => `${sensor.key}.${field.key}`)]);
  const rows: (string | number | boolean | undefined)[][] = samples.map(sample => [
    new Date(sample.receivedAt).toISOString(), sample.sequence, sample.uptimeMs.toString(), sample.enabledMask, sample.validMask,
    ...schema.sensors.flatMap(sensor => {
      const reading = sample.sensors.get(sensor.key);
      return [reading?.error, ...sensor.fields.map(field => reading?.values[field.key])];
    }),
  ]);
  const escape = (value: string | number | boolean | undefined): string => `"${String(value ?? '').replaceAll('"', '""')}"`;
  return '\uFEFF' + [['received_at', 'sequence', 'uptime_ms', 'enabled_mask', 'valid_mask', ...columns], ...rows].map(row => row.map(escape).join(',')).join('\r\n') + '\r\n';
}

export function downloadCsv(samples: readonly Sample[]): void {
  const url = URL.createObjectURL(new Blob([createCsv(samples)], { type: 'text/csv;charset=utf-8' }));
  const link = document.createElement('a');
  link.href = url;
  link.download = `outdoor-${new Date().toISOString().replaceAll(':', '-')}.csv`;
  link.click();
  window.setTimeout(() => URL.revokeObjectURL(url), 1000);
}
