import { displaySensors, sensorStatus } from '../measurements';
import type { Sample } from '../protocol';

export function SensorList({ sample, stale }: { sample: Sample | undefined; stale: boolean }) {
  return <aside className="sensor-list" aria-labelledby="sensors-title">
    <h2 id="sensors-title">センサー</h2>
    <ul>{displaySensors.map(sensor => {
      const status = sensorStatus(sample, sensor.id, sensor.wireKey);
      const active = status === '取得中';
      return <li key={sensor.key} className={sensor.key === 'bmv080' ? 'future-start' : undefined}>
        <span>{sensor.name}</span>
        <span className={`sensor-status ${active && !stale ? 'active' : ''} ${status.startsWith('エラー') ? 'failed' : ''}`}>
          {active && stale ? '更新停止' : status}
        </span>
      </li>;
    })}</ul>
  </aside>;
}
