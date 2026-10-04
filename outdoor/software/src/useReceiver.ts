import { useEffect, useState, useSyncExternalStore } from 'react';
import { BluetoothReceiver } from './bluetooth';

export function useReceiver() {
  const [receiver] = useState(() => new BluetoothReceiver());
  const state = useSyncExternalStore(receiver.subscribe, receiver.getSnapshot);
  useEffect(() => () => receiver.disconnect(), [receiver]);
  return { receiver, state };
}

export function useNow(): number {
  const [now, setNow] = useState(Date.now);
  useEffect(() => {
    const timer = window.setInterval(() => setNow(Date.now()), 1000);
    return () => window.clearInterval(timer);
  }, []);
  return now;
}
