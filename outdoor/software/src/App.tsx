import { supportMessage } from './bluetooth';
import { downloadCsv, metrics, metricValue } from './measurements';
import { useNow, useReceiver } from './useReceiver';
import { ReadingTable } from './components/ReadingTable';
import { SensorList } from './components/SensorList';
import { TrendChart } from './components/TrendChart';

const phases = { idle: '未接続', requesting: 'デバイスを選択中', connecting: '接続中', connected: '接続済み', error: '未接続' };

export function App() {
  const { receiver, state } = useReceiver();
  const now = useNow();
  const unsupported = supportMessage();
  const busy = state.phase === 'requesting' || state.phase === 'connecting';
  const stale = state.phase !== 'connected' || (state.latest !== undefined && now - state.latest.receivedAt > 3500);
  const alert = state.error ?? unsupported;
  return <main className="app-shell">
    <header className="topbar">
      <div className="brand-area"><span className="wordmark">outdoor</span><span className={`connection-label ${state.phase === 'connected' ? 'connected' : ''}`} role="status">{phases[state.phase]}</span></div>
      <div className="actions">
        <button className="button primary" type="button" disabled={Boolean(unsupported) && state.phase !== 'connected'} onClick={() => {
          if (state.phase === 'connected' || busy) receiver.disconnect();
          else void receiver.connect();
        }}>{busy ? '接続を中止' : state.phase === 'connected' ? '切断' : 'BLE接続'}</button>
        <button className="button secondary" type="button" disabled={!state.history.length} onClick={() => downloadCsv(state.history)}>CSV保存</button>
      </div>
    </header>
    <section className="intro">
      <h1>屋外センサーモニター</h1>
      <p>センサーを接続して、1秒ごとの測定値を受信します。</p>
    </section>
    {alert ? <p className="notice" role="alert">{alert}</p> : null}
    <section className={`metric-strip ${stale && state.latest ? 'stale' : ''}`} aria-label="最新の測定値">
      {metrics.map(item => {
        const value = metricValue(state.latest, item.key);
        const warming = (item.key === 'voc' || item.key === 'nox') && state.latest?.sensors.get('sgp41')?.error === 0 && value === undefined;
        return <div className="metric" key={item.key}><h2>{item.label}</h2><p><span className="metric-value" data-testid={`metric-${item.key}`}>{value === undefined ? '—' : value.toLocaleString('ja-JP', { minimumFractionDigits: item.decimals, maximumFractionDigits: item.decimals })}</span><span className="metric-unit">{item.unit}</span></p>{warming ? <span className="metric-note">暖機中</span> : null}</div>;
      })}
    </section>
    {state.clockError ? <p className="notice" role="status">{state.clockError}</p> : null}
    {state.latest ? <p className="received-caption" role="status">{state.deviceName} · {new Date(state.latest.receivedAt).toLocaleTimeString('ja-JP')}受信 · {state.history.length}件{state.clockSynced ? ' · 時刻同期済み（SDログ用）' : ''}{stale ? ' · 更新が停止しています' : ''}</p> : null}
    <div className="monitor-layout">
      <TrendChart history={state.history} />
      <SensorList sample={state.latest} stale={stale} />
    </div>
    <ReadingTable sample={state.latest} rejected={state.rejected} />
  </main>;
}
