import { useEffect, useState } from 'react'
import { getPresetInfo, presetChangedEvent, selectPreset } from '../lib/bridge/index.js'

export function PresetSelect() {
  const [info, setInfo] = useState(getPresetInfo())

  useEffect(() => {
    setInfo(getPresetInfo())
    const id = presetChangedEvent.addListener(() => setInfo({ ...getPresetInfo() }))
    return () => presetChangedEvent.removeListener(id)
  }, [])

  return (
    <select
      value={info.current}
      onChange={(e) => selectPreset(Number(e.target.value))}
      className="bg-zinc-700 text-zinc-200 text-xs font-mono rounded px-1 py-0.5 border border-zinc-500/60"
    >
      {info.names.map((name, i) => (
        <option key={name + i} value={i}>
          {name}
        </option>
      ))}
    </select>
  )
}
