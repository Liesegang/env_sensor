import { useMemo, useState } from 'react';
import { trendMetrics, trendValue } from '../measurements';
import type { Sample } from '../protocol';

interface Props { history: readonly Sample[] }
const colors = ['#275b49', '#b65833', '#326aa3', '#9a6622', '#8b4a91', '#367e86', '#b23f61'];
const width = 800, height = 344, left = 58, right = 12, top = 18, bottom = 26;
const format = (value: number) => value.toLocaleString('ja-JP', { maximumFractionDigits: 2 });

export function TrendChart({ history }: Props) {
  const [selected, setSelected] = useState<readonly string[]>(() => trendMetrics.map(metric => metric.key));
  const last = history.at(-1);
  const { points, series } = useMemo(() => {
    const cutoff = last ? last.uptimeMs - 900000n : 0n;
    const points = history.filter(sample => sample.uptimeMs >= cutoff);
    const series = trendMetrics.map((metric, index) => {
      const values = points.map(sample => trendValue(sample, metric));
      let low = Infinity, high = -Infinity;
      for (const value of values) {
        if (value !== undefined) { low = Math.min(low, value); high = Math.max(high, value); }
      }
      const available = Number.isFinite(low);
      const padding = available ? Math.max((high - low) * .18, metric.unit === 'Index' || metric.unit === 'ppm' ? 1 : .1) : 0;
      return { metric, values, available, low: available ? (metric.unit === 'Index' ? Math.max(1, low - padding) : low - padding) : 0, high: available ? (metric.unit === 'Index' ? Math.min(500, high + padding) : high + padding) : 1,
        color: colors[index % colors.length]!, dashed: Math.floor(index / colors.length), latest: last ? trendValue(last, metric) : undefined };
    });
    return { points, series };
  }, [history, last]);
  const visible = series.filter(item => selected.includes(item.metric.key));
  const populated = visible.filter(item => item.available);
  const single = visible.length === 1 ? visible[0] : undefined;
  const start = points[0]?.uptimeMs ?? 0n;
  const span = Math.max(Number((last?.uptimeMs ?? start) - start), 1000);
  const x = (sample: Sample) => left + Number(sample.uptimeMs - start) / span * (width - left - right);
  const y = (value: number, low: number, high: number) => height - bottom - (value - low) / (high - low) * (height - top - bottom);
  const toggle = (key: string) => setSelected(current => current.includes(key) ? current.filter(item => item !== key) : [...current, key]);

  return <section className="trend" aria-labelledby="trend-title">
    <div className="section-heading">
      <h2 id="trend-title">測定の推移</h2>
      <div className="segments" role="group" aria-label="グラフの表示切り替え">
        <button type="button" onClick={() => setSelected(trendMetrics.map(metric => metric.key))}>すべて表示</button>
        <button type="button" onClick={() => setSelected([])}>すべて非表示</button>
      </div>
    </div>
    <p className="chart-scale-note">{single ? `${single.metric.label} · ${single.metric.unit}` : '各項目の表示範囲を0〜100%に揃えて重ねています。実際の値・単位・範囲は凡例に表示します。'}</p>
    <div className="chart-frame">
      <svg viewBox={`0 0 ${width} ${height}`} role="img" aria-label="測定推移グラフ" preserveAspectRatio="none">
        {[0, 1, 2, 3, 4, 5].map(index => {
          const py = top + index * (height - top - bottom) / 5;
          return <g key={index}>
            <line className="chart-grid" x1={left} x2={width - right} y1={py} y2={py} />
            <text className="chart-label" x={left - 8} y={py + 4} textAnchor="end">{single?.available ? format(single.high - index * (single.high - single.low) / 5) : `${100 - index * 20}%`}</text>
          </g>;
        })}
        {[0, 1, 2, 3, 4, 5, 6, 7, 8].map(index => <line key={index} className="chart-grid" x1={left + index * (width - left - right) / 8} x2={left + index * (width - left - right) / 8} y1={top} y2={height - bottom} />)}
        <path className="chart-axis" d={`M${left},${top}V${height - bottom}H${width - right}`} />
        {populated.map(item => {
          let path = '', previous: Sample | undefined;
          for (let index = 0; index < points.length; index++) {
            const sample = points[index]!;
            const value = item.values[index];
            if (value === undefined) { previous = undefined; continue; }
            const move = !previous || sample.uptimeMs - previous.uptimeMs > 2500n;
            path += `${move ? 'M' : 'L'}${x(sample).toFixed(2)},${y(value, item.low, item.high).toFixed(2)} `;
            previous = sample;
          }
          return <g key={item.metric.key} data-series={item.metric.key}>
            <title>{item.metric.label} ({item.metric.unit})</title>
            <path className="chart-line" d={path} style={{ stroke: item.color }} strokeDasharray={item.dashed ? `${8 / item.dashed} ${3 * item.dashed}` : undefined} />
            {last && item.latest !== undefined ? <circle cx={x(last)} cy={y(item.latest, item.low, item.high)} r={3} fill={item.color} /> : null}
          </g>;
        })}
        {points.length ? <>
          <text className="chart-label" x={left} y={height - 4}>{new Date(points[0]!.receivedAt).toLocaleTimeString('ja-JP')}</text>
          <text className="chart-label" x={width - right} y={height - 4} textAnchor="end">{new Date(last!.receivedAt).toLocaleTimeString('ja-JP')}</text>
        </> : null}
      </svg>
      {!populated.length ? <p className="chart-empty">{!selected.length ? '表示する項目を凡例から選んでください。' : history.length ? '選択した項目の有効な測定値を待っています。' : '接続すると、測定の推移が表示されます。'}</p> : null}
    </div>
    <div className="chart-legend" role="group" aria-label="グラフの測定項目">
      {series.map(item => <button key={item.metric.key} type="button" aria-pressed={selected.includes(item.metric.key)} aria-label={item.metric.label} onClick={() => toggle(item.metric.key)}>
        <span className="legend-swatch" style={{ borderColor: item.color, borderTopStyle: item.dashed ? 'dashed' : 'solid' }} />
        <span><strong>{item.metric.label}</strong><span className="legend-value">{item.latest === undefined ? '—' : format(item.latest)} {item.metric.unit}</span>
          <span className="legend-range">{item.available ? `範囲 ${format(item.low)}〜${format(item.high)} ${item.metric.unit}` : '有効な測定値なし'}</span></span>
      </button>)}
    </div>
    <p className="caption">最新15分 · 1秒間隔 · 凡例を押すと個別に表示・非表示</p>
  </section>;
}
