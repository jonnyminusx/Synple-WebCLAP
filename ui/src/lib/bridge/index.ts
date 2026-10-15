// API-compatible stand-in for the JUCE WebView JS bridge, speaking the CLAP webview
// extension protocol: UI -> plugin is text commands, plugin -> UI is JSON.

type Listener = () => void

class ListenerList {
  private nextId = 0
  private listeners = new Map<number, Listener>()

  addListener(fn: Listener): number {
    const id = this.nextId++
    this.listeners.set(id, fn)
    return id
  }

  removeListener(id: number) {
    this.listeners.delete(id)
  }

  callListeners() {
    this.listeners.forEach((fn) => fn())
  }
}

export interface SliderProperties {
  start: number
  end: number
  skew: number
  name: string
  label: string
  numSteps: number
  interval: number
  parameterIndex: number
}

export interface ComboBoxProperties {
  name: string
  parameterIndex: number
  choices: string[]
}

interface ParamInfo {
  id: string
  name: string
  label: string
  start: number
  end: number
  skew: number
  interval: number
  choices: string[] | null
  value: number
}

export interface PresetInfo {
  names: string[]
  current: number
}

export interface LearnState {
  learningParam: string
  assignments: Record<string, number>
}

const encoder = new TextEncoder()
const decoder = new TextDecoder()

function send(command: string) {
  const bytes = encoder.encode(command)
  window.parent.postMessage(bytes.buffer, '*')
}

class SliderState {
  readonly valueChangedEvent = new ListenerList()
  readonly propertiesChangedEvent = new ListenerList()
  properties: SliderProperties
  private scaledValue = 0

  constructor(readonly name: string) {
    this.properties = {
      start: 0, end: 1, skew: 1, name, label: '', numSteps: 100, interval: 0, parameterIndex: -1,
    }
  }

  applyInfo(info: ParamInfo, index: number) {
    const range = info.end - info.start
    this.properties = {
      start: info.start,
      end: info.end,
      skew: info.skew,
      name: info.name,
      label: info.label,
      numSteps: info.interval > 0 ? Math.floor(range / info.interval) + 1 : 100,
      interval: info.interval,
      parameterIndex: index,
    }
    this.scaledValue = info.value
    this.propertiesChangedEvent.callListeners()
    this.valueChangedEvent.callListeners()
  }

  setScaledValueFromPlugin(value: number) {
    this.scaledValue = value
    this.valueChangedEvent.callListeners()
  }

  getScaledValue() {
    return this.scaledValue
  }

  getNormalisedValue() {
    const { start, end, skew } = this.properties
    if (end === start) return 0
    const proportion = Math.max(0, Math.min(1, (this.scaledValue - start) / (end - start)))
    return Math.pow(proportion, skew)
  }

  setNormalisedValue(normalised: number) {
    const { start, end, skew, interval, parameterIndex } = this.properties
    let value = start + (end - start) * Math.pow(Math.max(0, Math.min(1, normalised)), 1 / skew)
    if (interval > 0) value = start + Math.round((value - start) / interval) * interval
    value = Math.max(start, Math.min(end, value))
    this.scaledValue = value
    this.valueChangedEvent.callListeners()
    if (parameterIndex >= 0) send(`set ${parameterIndex} ${value}`)
  }

  sliderDragStarted() {}
  sliderDragEnded() {}
}

class ComboBoxState {
  readonly valueChangedEvent = new ListenerList()
  readonly propertiesChangedEvent = new ListenerList()
  properties: ComboBoxProperties
  private index = 0

  constructor(readonly name: string) {
    this.properties = { name, parameterIndex: -1, choices: [] }
  }

  applyInfo(info: ParamInfo, parameterIndex: number) {
    this.properties = { name: info.name, parameterIndex, choices: info.choices ?? [] }
    this.index = Math.round(info.value)
    this.propertiesChangedEvent.callListeners()
    this.valueChangedEvent.callListeners()
  }

  setIndexFromPlugin(index: number) {
    this.index = index
    this.valueChangedEvent.callListeners()
  }

  getChoiceIndex() {
    return this.index
  }

  setChoiceIndex(index: number) {
    this.index = index
    this.valueChangedEvent.callListeners()
    if (this.properties.parameterIndex >= 0) send(`set ${this.properties.parameterIndex} ${index}`)
  }
}

const sliders = new Map<string, SliderState>()
const comboBoxes = new Map<string, ComboBoxState>()
let learnState: LearnState = { learningParam: '', assignments: {} }
let presetInfo: PresetInfo = { names: [], current: 0 }

export const presetChangedEvent = new ListenerList()

export function getSliderState(name: string): SliderState {
  let state = sliders.get(name)
  if (!state) {
    state = new SliderState(name)
    sliders.set(name, state)
  }
  return state
}

export function getComboBoxState(name: string): ComboBoxState {
  let state = comboBoxes.get(name)
  if (!state) {
    state = new ComboBoxState(name)
    comboBoxes.set(name, state)
  }
  return state
}

export function getPresetInfo(): PresetInfo {
  return presetInfo
}

export function selectPreset(index: number) {
  presetInfo = { ...presetInfo, current: index }
  send(`preset ${index}`)
}

type NativeFunction = (...args: unknown[]) => Promise<unknown>

export function getNativeFunction(name: string): NativeFunction {
  return async (...args: unknown[]) => {
    switch (name) {
      case 'midiLearnStart':
        send(`learnStart ${args[0]}`)
        return undefined
      case 'midiLearnCancel':
        send('learnCancel')
        return undefined
      case 'midiLearnClear':
        send(`learnClear ${args[0]}`)
        return undefined
      case 'midiLearnGetState':
        return learnState
      default:
        return undefined
    }
  }
}

type PluginMessage =
  | { t: 'init'; params: ParamInfo[]; presets: string[]; preset: number; learn: LearnState }
  | { t: 'p'; i: number; v: number }
  | { t: 'learn'; learn: LearnState }
  | { t: 'preset'; preset: number }

const idByIndex: string[] = []
const choiceIds = new Set<string>()

function handleMessage(message: PluginMessage) {
  switch (message.t) {
    case 'init':
      message.params.forEach((info, index) => {
        idByIndex[index] = info.id
        if (info.choices) {
          choiceIds.add(info.id)
          getComboBoxState(info.id).applyInfo(info, index)
        } else {
          getSliderState(info.id).applyInfo(info, index)
        }
      })
      presetInfo = { names: message.presets, current: message.preset }
      learnState = message.learn
      presetChangedEvent.callListeners()
      break
    case 'p': {
      const id = idByIndex[message.i]
      if (id === undefined) break
      if (choiceIds.has(id)) getComboBoxState(id).setIndexFromPlugin(Math.round(message.v))
      else getSliderState(id).setScaledValueFromPlugin(message.v)
      break
    }
    case 'learn':
      learnState = message.learn
      break
    case 'preset':
      presetInfo = { ...presetInfo, current: message.preset }
      presetChangedEvent.callListeners()
      break
  }
}

window.addEventListener('message', (event: MessageEvent) => {
  if (!(event.data instanceof ArrayBuffer) && !ArrayBuffer.isView(event.data)) return
  try {
    handleMessage(JSON.parse(decoder.decode(event.data)) as PluginMessage)
  } catch {
    // ignore malformed messages
  }
})

send('ready')
