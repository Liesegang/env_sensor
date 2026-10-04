import { useState } from 'react';
import { displaySensors, formatField, sensorStatus } from '../measurements';
import { CHARACTERISTIC_UUID, SERVICE_UUID } from '../protocol';
import type { Sample } from '../protocol';

export function ReadingTable({ sample, rejected }: { sample: Sample | undefined; rejected: number }) {
  const [expanded, setExpanded] = useState(false);
  return <section className="readings" aria-labelledby="readings-title">
    <div className="section-heading">
      <h2 id="readings-title">受信データ</h2>
      <button className="button secondary small" type="button" aria-expanded={expanded} aria-controls="reading-details" onClick={() => setExpanded(value => !value)}>
        {expanded ? '詳細を閉じる' : '詳細を表示'}<svg width="14" height="14" viewBox="0 0 16 16" aria-hidden="true" className={expanded ? 'rotated' : ''}><path d="m4 6 4 4 4-4" fill="none" stroke="currentColor" strokeWidth="1.5" /></svg>
      </button>
    </div>
    <div className="table-scroll">
      <table><thead><tr><th>センサー</th><th>項目</th><th>値</th><th>状態</th></tr></thead>
        <tbody>{sample ? displaySensors.flatMap(sensor => {
          const reading = sample.sensors.get(sensor.wireKey);
          const fields = expanded ? sensor.fields : sensor.fields.filter(field => field.unit !== undefined || (sensor.key === 'as3935' && field.key === 'lightning'));
          if (!reading || reading.error !== 0) return [<tr key={sensor.key}><td>{sensor.name}</td><td>—</td><td>—</td><td>{sensorStatus(sample, sensor.id, sensor.wireKey)}</td></tr>];
          return fields.map(field => {
            const invalid = (field.key === 'raw_nox' && reading.values.nox_valid !== true) ||
              (field.key === 'voc_index' && reading.values.voc_index_valid !== true) ||
              (field.key === 'nox_index' && reading.values.nox_index_valid !== true);
            const value = invalid ? undefined : reading.values[field.key];
            return <tr key={`${sensor.key}.${field.key}`}>
              <td>{sensor.name}</td><td>{field.label}</td><td className="data-value">{formatField(value, field)}{field.unit ? <span className="table-unit"> {field.unit}</span> : null}</td><td>{sensorStatus(sample, sensor.id, sensor.wireKey)}</td>
            </tr>;
          });
        }) : <tr><td className="empty-table" colSpan={4}>まだデータを受信していません。</td></tr>}</tbody>
      </table>
    </div>
    {expanded ? <dl className="protocol-details" id="reading-details">
      <div><dt>Service</dt><dd>{SERVICE_UUID}</dd></div>
      <div><dt>Characteristic</dt><dd>{CHARACTERISTIC_UUID}</dd></div>
      <div><dt>受信連番 / 稼働時間</dt><dd>{sample ? `${sample.sequence} / ${sample.uptimeMs.toString()} ms` : '—'}</dd></div>
      <div><dt>不正・欠落パケット</dt><dd>{rejected}</dd></div>
    </dl> : null}
    <p className="caption privacy">データはこのブラウザー内だけに保存されます。</p>
  </section>;
}
