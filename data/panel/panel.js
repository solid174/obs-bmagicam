// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174
//
// The Remote Control panel (docs/ui.md, "Web panel"): drawn from /api/v1/controls, kept live over /api/v1/events.
'use strict';

const API = '/api/v1';
// Simple mode's rows, as in the dock (docs/ui.md, "Simple mode")
const SIMPLE_ROWS = ['exposure.iso', 'wb.temperature', 'lens.camera', 'focus.auto', 'lens.stabilization'];
// Advanced mode's tiles, after Blackmagic Camera's strip, and their short names
const TILES = [
  ['lens.zoom', 'Dock.Tile.Lens'],
  ['stream.preset', 'Dock.Tile.FPS'],
  ['exposure.shutter', 'Dock.Tile.Shutter'],
  ['exposure.iris', 'Dock.Tile.Iris'],
  ['exposure.iso', 'Dock.Tile.ISO'],
  ['wb.temperature', 'Dock.Tile.WB'],
  ['wb.tint', 'Dock.Tile.Tint'],
];
const BEAUTY_VALUES = ['smoothing', 'texture', 'evening', 'sharpen', 'glow', 'maskSoftness', 'detailSize'];
const BEAUTY_NAMES = ['Beautify.Smoothing', 'Beautify.Texture', 'Beautify.Evening', 'Beautify.Sharpen',
  'Beautify.Glow', 'Beautify.MaskSoftness', 'Beautify.DetailSize'];
// After the user changes a control, the phone's echoes of older values do not move it for this long
const ECHO_GUARD_MS = 700;

const state = {
  password: '',
  strings: {},
  tabs: [],
  descriptors: [],
  byId: new Map(),
  presets: [],
  cameras: new Map(),
  cameraOrder: [],
  looks: [],
  beautify: [],
  styles: [],
  camera: null,
  mode: 'simple',
  tab: 'camera',
  tile: 'lens.zoom',
  socket: null,
  signedOut: false,
  updaters: new Map(),
  editing: new Map(),
};

// ---------------------------------------------------------------- helpers

const store = {
  get(key) { try { return localStorage.getItem(key); } catch { return null; } },
  set(key, value) { try { localStorage.setItem(key, value); } catch { /* private mode */ } },
  remove(key) { try { localStorage.removeItem(key); } catch { /* private mode */ } },
};

function t(key, ...args) {
  let text = state.strings[key] ?? key;
  args.forEach((arg, index) => { text = text.split('%' + (index + 1)).join(String(arg)); });
  return text;
}

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

function button(text, className, onClick) {
  const node = el('button', className, text);
  node.type = 'button';
  if (onClick) node.addEventListener('click', onClick);
  return node;
}

function cssVar(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

let statusTimer = 0;
function showStatus(text, ms) {
  const status = document.getElementById('status');
  status.textContent = text;
  status.hidden = false;
  clearTimeout(statusTimer);
  if (ms) statusTimer = setTimeout(() => { status.hidden = true; }, ms);
}

function hideStatus() {
  document.getElementById('status').hidden = true;
}

function duration(seconds) {
  const minutes = Math.floor(seconds / 60);
  return minutes >= 60 ? t('Dock.Duration.Hours', Math.floor(minutes / 60), minutes % 60)
    : t('Dock.Duration.Minutes', minutes);
}

function format(descriptor, value) {
  if (value === null || value === undefined || value === '') return '—';
  const decimals = descriptor.decimals || 0;
  switch (descriptor.unit) {
  case 'K': return `${Math.round(value)} K`;
  case 'mm': return `${Math.round(value)} mm`;
  case '%': return `${decimals ? value.toFixed(decimals) : Math.round(value)} %`;
  case 'dB': return `${value.toFixed(decimals || 1)} dB`;
  case '1/': return `1/${Math.round(value)}`;
  case 'f/': return `f/${value.toFixed(1)}`;
  case 's': return duration(value);
  default:
    if (descriptor.unit && descriptor.unit.endsWith('.')) return t(descriptor.unit + value);
    if (typeof value === 'number') return decimals ? value.toFixed(decimals) : String(Math.round(value));
    return String(value);
  }
}

function camera() {
  return state.cameras.get(state.camera) || null;
}

function controlState(id) {
  const current = camera();
  return current && current.controls ? current.controls[id] : undefined;
}

function send(message) {
  if (state.socket && state.socket.readyState === WebSocket.OPEN) state.socket.send(JSON.stringify(message));
}

function setControl(id, value) {
  state.editing.set(id, Date.now() + ECHO_GUARD_MS);
  send({ op: 'set', camera: state.camera, control: id, value });
}

function editing(id) {
  return (state.editing.get(id) || 0) > Date.now();
}

// ---------------------------------------------------------------- the ruler

// A horizontal scale under a fixed center needle, dragged by touch (docs/ui.md, "Controls"); drawn from the theme
class Ruler {
  constructor({ format: formatValue, plain, onChange }) {
    this.format = formatValue;
    this.plain = plain;
    this.onChange = onChange;
    this.values = [0];
    this.marks = new Set();
    this.position = 0;
    this.enabled = true;
    this.dragging = false;
    this.canvas = el('canvas', 'ruler');
    this.canvas.addEventListener('pointerdown', (event) => this.down(event));
    this.canvas.addEventListener('pointermove', (event) => this.move(event));
    this.canvas.addEventListener('pointerup', (event) => this.up(event));
    this.canvas.addEventListener('pointercancel', (event) => this.up(event));
    new ResizeObserver(() => this.draw()).observe(this.canvas);
  }

  setStops(values) {
    const current = this.values[Math.round(this.position)];
    this.values = values.length ? [...values] : [0];
    this.position = this.nearest(current);
    this.draw();
  }

  setRange(minimum, maximum, step) {
    const count = Math.max(1, Math.round((maximum - minimum) / step) + 1);
    const values = [];
    for (let index = 0; index < count; index++) values.push(minimum + index * step);
    this.setStops(values);
  }

  setMarks(values) {
    this.marks = new Set((values || []).map((value) => this.nearest(value)));
    this.draw();
  }

  setValue(value) {
    if (this.dragging) return;
    this.slide(this.nearest(value));
  }

  setEnabled(enabled) {
    this.enabled = enabled;
    this.canvas.classList.toggle('disabled', !enabled);
  }

  nearest(value) {
    if (value === undefined || value === null) return 0;
    let best = 0;
    for (let index = 1; index < this.values.length; index++) {
      if (Math.abs(this.values[index] - value) < Math.abs(this.values[best] - value)) best = index;
    }
    return best;
  }

  spacing() {
    const width = this.canvas.clientWidth || 300;
    return Math.min(26, Math.max(4, (width * 3) / Math.max(1, this.values.length - 1)));
  }

  slide(target) {
    cancelAnimationFrame(this.frame);
    const start = this.position;
    const began = performance.now();
    const step = (now) => {
      const progress = Math.min(1, (now - began) / 180);
      this.position = start + (target - start) * (1 - Math.pow(1 - progress, 3));
      this.draw();
      if (progress < 1) this.frame = requestAnimationFrame(step);
    };
    this.frame = requestAnimationFrame(step);
  }

  down(event) {
    if (!this.enabled) return;
    // Keeps the drag when the finger leaves the scale; a pointer the browser does not track cannot be captured
    try { this.canvas.setPointerCapture(event.pointerId); } catch { /* the drag still works on the scale */ }
    this.dragging = true;
    this.moved = false;
    this.startX = event.clientX;
    this.startPosition = this.position;
    cancelAnimationFrame(this.frame);
  }

  move(event) {
    if (!this.dragging) return;
    const dx = event.clientX - this.startX;
    if (Math.abs(dx) > 3) this.moved = true;
    this.position = Math.min(this.values.length - 1, Math.max(0, this.startPosition - dx / this.spacing()));
    this.draw();
    const index = Math.round(this.position);
    const now = performance.now();
    if (index !== this.sentIndex && now - (this.sentAt || 0) > 50) {
      this.sentIndex = index;
      this.sentAt = now;
      this.onChange(this.values[index], false);
    }
  }

  up(event) {
    if (!this.dragging) return;
    this.dragging = false;
    const index = Math.round(this.position);
    this.slide(index);
    if (this.moved) {
      this.sentIndex = undefined;
      this.onChange(this.values[index], true);
    } else if (!this.plain && event.type === 'pointerup' && event.offsetY < 34) {
      // A tap on the value box types a value
      const typed = window.prompt('', String(this.values[index]));
      const number = typed === null ? NaN : Number(typed.replace(',', '.'));
      if (!Number.isNaN(number)) {
        const nearest = this.nearest(number);
        this.slide(nearest);
        this.onChange(this.values[nearest], true);
      }
    }
  }

  draw() {
    const canvas = this.canvas;
    const width = canvas.clientWidth;
    const height = canvas.clientHeight;
    if (!width || !height) return;
    const ratio = window.devicePixelRatio || 1;
    if (canvas.width !== Math.round(width * ratio)) canvas.width = Math.round(width * ratio);
    if (canvas.height !== Math.round(height * ratio)) canvas.height = Math.round(height * ratio);
    const context = canvas.getContext('2d');
    context.setTransform(ratio, 0, 0, ratio, 0, 0);
    context.clearRect(0, 0, width, height);
    const text = cssVar('--text') || '#fff';
    const highlight = cssVar('--highlight') || '#284cb8';
    const spacing = this.spacing();
    const center = width / 2;
    const count = this.values.length;
    let major = 1;
    for (const step of [1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000]) {
      major = step;
      if (step * spacing >= 64) break;
    }
    const first = Math.max(0, Math.floor(this.position - center / spacing) - 1);
    const last = Math.min(count - 1, Math.ceil(this.position + center / spacing) + 1);
    context.font = '12px system-ui, sans-serif';
    context.textAlign = 'center';
    for (let index = first; index <= last; index++) {
      const x = center + (index - this.position) * spacing;
      const isMajor = index % major === 0 || index === 0 || index === count - 1;
      context.globalAlpha = isMajor ? 0.8 : 0.4;
      context.fillStyle = text;
      const tick = isMajor ? 16 : 9;
      context.fillRect(Math.round(x), 46 - tick, 1, tick);
      if (this.marks.has(index)) {
        context.globalAlpha = 0.9;
        context.beginPath();
        context.arc(x, 51, 2.5, 0, Math.PI * 2);
        context.fill();
      }
      if (isMajor && !this.plain && index % major === 0) {
        context.globalAlpha = 0.65;
        context.fillText(this.format(this.values[index]), x, 68);
      }
    }
    context.globalAlpha = 1;
    context.fillStyle = highlight;
    context.fillRect(center - 1.5, 24, 3, 30);
    if (!this.plain) {
      const label = this.format(this.values[Math.round(this.position)]);
      context.font = '600 14px system-ui, sans-serif';
      const boxWidth = Math.max(48, context.measureText(label).width + 18);
      context.globalAlpha = 0.9;
      context.strokeStyle = text;
      context.lineWidth = 1;
      context.beginPath();
      context.roundRect(center - boxWidth / 2, 1, boxWidth, 22, 6);
      context.stroke();
      context.fillStyle = text;
      context.fillText(label, center, 17);
    }
  }
}

// ---------------------------------------------------------------- widgets

function segmented(options, selected, onSelect, compact) {
  const node = el('div', compact ? 'segmented compact' : 'segmented');
  const update = (items, value) => {
    node.replaceChildren(...items.map((item) => {
      const choice = button(item.label, item.id === value ? 'selected' : '', () => onSelect(item.id));
      choice.setAttribute('aria-pressed', String(item.id === value));
      return choice;
    }));
  };
  update(options, selected);
  return { element: node, update };
}

function chip(label, on, onToggle) {
  const node = button(label, on ? 'chip on' : 'chip', () => onToggle(!node.classList.contains('on')));
  return {
    element: node,
    update(value) { node.classList.toggle('on', Boolean(value)); node.setAttribute('aria-pressed', String(Boolean(value))); },
  };
}

// A row: its name with an ⓘ that opens its help, extras beside the name, the control below
function row(label, help, extras = []) {
  const node = el('div', 'row');
  const head = el('div', 'row-head');
  head.append(el('span', 'label', label), ...extras);
  const text = el('p', 'help', help);
  text.hidden = true;
  if (help) {
    const info = button('ⓘ', 'info', () => { text.hidden = !text.hidden; });
    info.setAttribute('aria-label', t('Panel.Info'));
    head.append(info);
  }
  const lock = el('p', 'lock');
  lock.hidden = true;
  node.append(head, text);
  return { node, head, lock };
}

// The widget for a control of the phone, by its kind. A companion shows as a chip beside its row's control: Auto
// beside Brightness and Warmth, Refocus beside Focus.
function controlWidget(descriptor, simple, companion = false) {
  const id = descriptor.id;
  switch (descriptor.kind) {
  case 'number':
  case 'stops': {
    const plain = simple && Array.isArray(descriptor.simpleEnds);
    const ruler = new Ruler({
      format: (value) => format(descriptor, value),
      plain,
      onChange: (value) => setControl(id, value),
    });
    const element = el('div');
    element.append(ruler.canvas);
    if (plain) {
      const ends = el('div', 'ends');
      ends.append(el('span', '', descriptor.simpleEnds[0]), el('span', '', descriptor.simpleEnds[1]));
      element.append(ends);
    }
    return {
      element,
      update(current) {
        if (descriptor.kind === 'stops') {
          if (JSON.stringify(current.stops) !== JSON.stringify(ruler.values)) ruler.setStops(current.stops || []);
          ruler.setMarks(current.marks);
        } else {
          ruler.setRange(current.min, current.max, current.step || 1);
        }
        ruler.setEnabled(!current.locked);
        if (!editing(id)) ruler.setValue(current.value);
      },
    };
  }
  case 'choice': {
    const choices = segmented([], null, (value) => setControl(id, value));
    return { element: choices.element, update: (current) => choices.update(current.options || [], current.value) };
  }
  case 'switch': {
    if (simple && !descriptor.companion && !companion) {
      const choices = segmented([], null, (value) => setControl(id, value === 'on'));
      const options = [{ id: 'off', label: t('Dock.Off') }, { id: 'on', label: t('Dock.On') }];
      return { element: choices.element, update: (current) => choices.update(options, current.value ? 'on' : 'off') };
    }
    const toggle = chip(simple || companion ? t('Dock.Auto') : t('Dock.On'), false, (on) => setControl(id, on));
    return { element: toggle.element, update: (current) => { toggle.update(current.value); toggle.element.disabled = current.locked; } };
  }
  case 'action': {
    const label = companion && simple && id === 'wb.auto' ? t('Dock.Auto') : descriptor.label;
    const node = button(label, companion ? 'chip' : '', () => setControl(id, true));
    return { element: node, update: (current) => { node.disabled = current.locked; } };
  }
  default: {
    const node = el('span', 'value-text');
    return { element: node, update: (current) => { node.textContent = format(descriptor, current.value); } };
  }
  }
}

// A control's row, kept up to date by its control's events
function controlRow(descriptor, simple) {
  const extras = [];
  const companions = [];
  const companion = descriptor.companion ? state.byId.get(descriptor.companion) : null;
  if (companion) {
    const widget = controlWidget(companion, simple, true);
    (descriptor.kind === 'switch' ? companions : extras).push(widget.element);
    register(companion.id, widget.update, widget.element);
  }
  const shown = row(simple && descriptor.simpleLabel ? descriptor.simpleLabel : descriptor.label,
    descriptor.tooltip, extras);
  const widget = controlWidget(descriptor, simple);
  if (companions.length) {
    const line = el('div', 'buttons');
    line.append(widget.element, ...companions);
    shown.node.append(line);
  } else {
    shown.node.append(widget.element);
  }
  shown.node.append(shown.lock);
  register(descriptor.id, (current) => {
    widget.update(current);
    shown.lock.hidden = !current.locked;
    shown.lock.textContent = current.lockReason || '';
  }, shown.node);
  return shown.node;
}

// Hooks a control's updater to its events; a control the phone does not offer is hidden (CTL-1)
function register(id, update, node) {
  const run = () => {
    const current = controlState(id);
    node.hidden = !current || !current.available;
    if (current && current.available) update(current);
  };
  const list = state.updaters.get(id) || [];
  list.push(run);
  state.updaters.set(id, list);
  run();
}

function updateControl(id) {
  for (const run of state.updaters.get(id) || []) run();
  if (editing(id)) setTimeout(() => updateControl(id), ECHO_GUARD_MS);
}

// ---------------------------------------------------------------- rows that are not the phone's controls

function setUpRow() {
  const node = button(t('Dock.SetUp'), 'primary', () => send({ op: 'action', camera: state.camera, action: 'set-up-for-streaming' }));
  node.title = t('Dock.SetUp.Tooltip');
  const wrap = el('div', 'row');
  wrap.append(node);
  return wrap;
}

function lookRow() {
  const shown = row(t('Dock.Look'), t('Dock.Look.Tooltip'));
  const choices = segmented([], null, (id) => send({ op: 'look', camera: state.camera, look: id }));
  shown.node.append(choices.element);
  const update = () => choices.update(state.looks.map((look) => ({ id: look.id, label: look.name })), camera() ? camera().look : null);
  state.updaters.set('look', [...(state.updaters.get('look') || []), update]);
  update();
  return shown.node;
}

function presetRow() {
  const shown = row(t('Camera.Preset'), t('Camera.Preset.Tooltip'));
  const choices = segmented([], null, (id) => send({ op: 'stream', camera: state.camera, preset: id }));
  shown.node.append(choices.element);
  const update = () => choices.update(state.presets.map((preset) => ({ id: preset.id, label: preset.name })), camera() ? camera().preset : null);
  state.updaters.set('stream.preset', [...(state.updaters.get('stream.preset') || []), update]);
  update();
  return shown.node;
}

function wand() {
  const icon = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  icon.setAttribute('viewBox', '0 0 24 24');
  icon.setAttribute('width', '20');
  icon.setAttribute('height', '20');
  icon.setAttribute('aria-hidden', 'true');
  icon.innerHTML = '<g fill="none" stroke="currentColor" stroke-linecap="round"><path stroke-width="3" d="M4 20L14 10"/>' +
    '<path stroke-width="2" d="M18 2v8M14 6h8M9 2v4M7 4h4M20 13v3M18.5 14.5h3"/></g>';
  return icon;
}

// Beautify on one source (BEA-2): on, style, strength; in Advanced mode every value and Show mask
function beautyGroup(filter, advanced, title) {
  const update = { list: [] };
  const change = (settings) => send({ op: 'beautify', source: filter.source, filter: filter.filter, settings });
  const on = chip(t('Dock.On'), filter.enabled, (value) => change({ enabled: value }));
  const shown = row(title, t('Dock.Beauty.Tooltip'), [on.element]);
  shown.head.prepend(wand());
  const styles = segmented([], null, (id) => change({ style: id }));
  const strength = new Ruler({
    format: (value) => String(Math.round(value)),
    plain: !advanced,
    onChange: (value) => { state.editing.set(`beauty:${filter.source}:strength`, Date.now() + ECHO_GUARD_MS); change({ strength: Math.round(value) }); },
  });
  strength.setRange(0, 100, 1);
  shown.node.append(styles.element, strength.canvas);
  if (!advanced) {
    const ends = el('div', 'ends');
    ends.append(el('span', '', t('Dock.Beauty.Off')), el('span', '', t('Dock.Beauty.Full')));
    shown.node.append(ends);
  }
  const nodes = [shown.node];
  update.list.push((current) => {
    on.update(current.enabled);
    styles.update(state.styles.map((style) => ({ id: style.id, label: style.name })), current.style);
    if (!editing(`beauty:${filter.source}:strength`)) strength.setValue(current.strength);
  });
  if (advanced) {
    BEAUTY_VALUES.forEach((key, index) => {
      const shown = row(t(BEAUTY_NAMES[index]), t(BEAUTY_NAMES[index] + '.Tooltip'));
      const ruler = new Ruler({
        format: (value) => String(Math.round(value)),
        plain: false,
        onChange: (value) => { state.editing.set(`beauty:${filter.source}:${key}`, Date.now() + ECHO_GUARD_MS); change({ [key]: Math.round(value) }); },
      });
      ruler.setRange(0, 100, 1);
      shown.node.append(ruler.canvas);
      nodes.push(shown.node);
      update.list.push((current) => { if (!editing(`beauty:${filter.source}:${key}`)) ruler.setValue(current[key]); });
    });
    const mask = chip(t('Beautify.ShowMask'), filter.showMask, (value) => change({ showMask: value }));
    mask.element.title = t('Beautify.ShowMask.Tooltip');
    const line = el('div', 'row buttons');
    line.append(mask.element);
    nodes.push(line);
    update.list.push((current) => mask.update(current.showMask));
  }
  const run = () => {
    const current = state.beautify.find((item) => item.source === filter.source && item.filter === filter.filter);
    if (current) update.list.forEach((apply) => apply(current));
  };
  const list = state.updaters.get('beautify') || [];
  list.push(run);
  state.updaters.set('beautify', list);
  run();
  return nodes;
}

function beautySection(advanced) {
  const section = el('div');
  const own = state.beautify.filter((filter) => filter.source === state.camera);
  if (state.camera && !own.length) {
    const shown = row(t('Dock.Beauty'), t('Dock.Beauty.Tooltip'));
    shown.head.prepend(wand());
    shown.node.append(button(t('Dock.Beauty.Add'), '', () => send({ op: 'addBeauty', source: state.camera })));
    section.append(shown.node);
  }
  own.forEach((filter) => section.append(...beautyGroup(filter, advanced, t('Dock.Beauty'))));
  // Beautify works on any source (BEA-1): Advanced mode also shows the others
  if (advanced) {
    state.beautify.filter((filter) => filter.source !== state.camera)
      .forEach((filter) => section.append(...beautyGroup(filter, true, `${filter.sourceName} · ${filter.filter}`)));
  }
  return section;
}

// ---------------------------------------------------------------- pages

function renderHeader() {
  const picker = document.getElementById('camera-picker');
  picker.hidden = state.cameraOrder.length < 2;
  picker.replaceChildren(...state.cameraOrder.map((id) => {
    const option = el('option', '', `${state.cameras.get(id).phone.name || state.cameras.get(id).name} · ${state.cameras.get(id).name}`);
    option.value = id;
    option.selected = id === state.camera;
    return option;
  }));
  const current = camera();
  const status = document.getElementById('camera-state');
  status.textContent = current ? current.text : '';
  status.className = current && current.state === 'live' ? 'state live' : 'state';
  const mode = document.getElementById('mode');
  const choices = segmented([{ id: 'simple', label: t('Panel.Simple') }, { id: 'advanced', label: t('Panel.Advanced') }],
    state.mode, (id) => { state.mode = id; store.set('mode', id); render(); }, true);
  mode.replaceWith(choices.element);
  choices.element.id = 'mode';
}

function renderSimple(content) {
  const current = camera();
  if (current.connected) {
    content.append(setUpRow(), lookRow());
    SIMPLE_ROWS.map((id) => state.byId.get(id)).filter(Boolean).forEach((descriptor) => content.append(controlRow(descriptor, true)));
  }
  content.append(beautySection(false));
}

function tileButton(id, nameKey) {
  const descriptor = state.byId.get(id);
  const node = button('', id === state.tile ? 'tile selected' : 'tile', () => { state.tile = id; render(); });
  const value = el('strong');
  node.append(el('small', '', t(nameKey)), value);
  if (id === 'stream.preset') {
    const update = () => {
      const preset = state.presets.find((item) => item.id === (camera() ? camera().preset : null));
      value.textContent = preset ? String(preset.fps) : '—';
    };
    const list = state.updaters.get('stream.preset') || [];
    list.push(update);
    state.updaters.set('stream.preset', list);
    update();
  } else if (descriptor) {
    register(id, (current) => { value.textContent = format(descriptor, current.value); }, node);
  }
  return node;
}

function renderAdvanced(content) {
  const current = camera();
  if (current.connected) {
    const tiles = el('div', 'tiles');
    TILES.forEach(([id, key]) => tiles.append(tileButton(id, key)));
    content.append(tiles);
    if (state.tile === 'stream.preset') content.append(presetRow());
    else if (state.byId.get(state.tile)) content.append(controlRow(state.byId.get(state.tile), false));
  }
  const tabs = el('div', 'tabs');
  for (const tab of state.tabs) {
    if (!current.connected && tab.id !== 'beauty') continue;
    tabs.append(button(tab.label, tab.id === state.tab ? 'selected' : '', () => { state.tab = tab.id; render(); }));
  }
  content.append(tabs);
  if (!current.connected && state.tab !== 'beauty') state.tab = 'beauty';
  if (state.tab === 'beauty') {
    content.append(beautySection(true));
    return;
  }
  if (state.tab === 'color') content.append(lookRow());
  if (state.tab === 'camera') content.append(setUpRow());
  const inTiles = new Set(TILES.map(([id]) => id));
  state.descriptors.filter((descriptor) => descriptor.tab === state.tab && !inTiles.has(descriptor.id) &&
    !(descriptor.kind === 'action' && descriptor.id === 'wb.auto'))
    .forEach((descriptor) => content.append(controlRow(descriptor, false)));
  if (state.tab === 'phone') {
    const actions = el('div', 'row buttons');
    actions.append(
      button(t('Dock.Reset'), '', () => { if (window.confirm(t('Dock.Reset.Question'))) send({ op: 'action', camera: state.camera, action: 'reset-defaults' }); }),
      button(t('Dock.Restore'), '', () => { if (window.confirm(t('Dock.Restore.Question'))) send({ op: 'action', camera: state.camera, action: 'restore-settings' }); }));
    content.append(actions);
  }
}

function render() {
  state.updaters.clear();
  const content = document.getElementById('content');
  content.replaceChildren();
  renderHeader();
  const current = camera();
  if (!current) {
    content.append(el('p', 'empty', t('Panel.NoCameras')));
    if (state.beautify.length) content.append(beautySection(true));
    return;
  }
  if (state.mode === 'advanced') renderAdvanced(content);
  else renderSimple(content);
}

// ---------------------------------------------------------------- the connection

function receive(message) {
  switch (message.op) {
  case 'ready':
    state.cameras = new Map(message.cameras.map((item) => [item.id, item]));
    state.cameraOrder = message.cameras.map((item) => item.id);
    state.looks = message.looks;
    state.beautify = message.beautify;
    state.styles = message.styles;
    document.documentElement.lang = (message.info.language || 'en-US').split('-')[0];
    if (!state.cameras.has(state.camera)) state.camera = store.get('camera') || state.cameraOrder[0] || null;
    if (!state.cameras.has(state.camera)) state.camera = state.cameraOrder[0] || null;
    hideStatus();
    document.getElementById('signin').hidden = true;
    document.getElementById('panel').hidden = false;
    render();
    break;
  case 'control': {
    const target = state.cameras.get(message.camera);
    if (!target) break;
    const { op, camera: cameraId, control, ...current } = message;
    target.controls = target.controls || {};
    target.controls[control] = current;
    if (cameraId === state.camera) updateControl(control);
    break;
  }
  case 'camera': {
    const incoming = message.camera;
    const old = state.cameras.get(incoming.id);
    const merged = { ...(old || {}), ...incoming };
    if (!incoming.controls && old) merged.controls = old.controls;
    state.cameras.set(incoming.id, merged);
    if (!state.cameraOrder.includes(incoming.id)) state.cameraOrder.push(incoming.id);
    if (!state.camera) state.camera = incoming.id;
    const reshape = !old || old.connected !== merged.connected || incoming.controls;
    if (incoming.id === state.camera && reshape) render();
    else {
      renderHeader();
      for (const run of state.updaters.get('look') || []) run();
      for (const run of state.updaters.get('stream.preset') || []) run();
    }
    break;
  }
  case 'cameraRemoved':
    state.cameras.delete(message.camera);
    state.cameraOrder = state.cameraOrder.filter((id) => id !== message.camera);
    if (state.camera === message.camera) state.camera = state.cameraOrder[0] || null;
    render();
    break;
  case 'beautify': {
    const before = state.beautify.map((filter) => filter.source + filter.filter).join();
    state.beautify = message.beautify;
    if (before !== state.beautify.map((filter) => filter.source + filter.filter).join()) render();
    else for (const run of state.updaters.get('beautify') || []) run();
    break;
  }
  case 'looks':
    state.looks = message.looks;
    for (const run of state.updaters.get('look') || []) run();
    break;
  case 'action': {
    const keys = { 'set-up-for-streaming': 'Dock.SetUp', 'reset-defaults': 'Dock.Reset', 'restore-settings': 'Dock.Restore' };
    if (keys[message.action]) showStatus(t(keys[message.action] + (message.done ? '.Done' : '.Failed')), 4000);
    break;
  }
  case 'error':
    if (message.request === 'hello') {
      state.signedOut = true;
      store.remove('password');
      showSignIn(t(message.status === 429 ? 'Panel.Blocked' : 'Panel.WrongPassword'));
    } else {
      showStatus(message.message, 4000);
    }
    break;
  default:
    break;
  }
}

function connect() {
  state.signedOut = false;
  const socket = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}${API}/events`);
  state.socket = socket;
  socket.addEventListener('open', () => socket.send(JSON.stringify({ op: 'hello', password: state.password })));
  socket.addEventListener('message', (event) => receive(JSON.parse(event.data)));
  socket.addEventListener('close', () => {
    if (state.socket !== socket) return;
    state.socket = null;
    if (state.signedOut) return;
    showStatus(t('Panel.Reconnecting'));
    setTimeout(connect, 2000);
  });
}

async function request(path) {
  const response = await fetch(API + path, { headers: { Authorization: `Bearer ${state.password}` } });
  if (!response.ok) {
    const failure = new Error(String(response.status));
    failure.status = response.status;
    throw failure;
  }
  return response.json();
}

function applyTheme(theme) {
  const names = { window: '--window', text: '--text', base: '--base', button: '--button', buttonText: '--button-text',
    highlight: '--highlight', highlightedText: '--highlighted-text', mid: '--mid' };
  for (const [key, name] of Object.entries(names)) {
    if (theme[key]) document.documentElement.style.setProperty(name, theme[key]);
  }
  document.documentElement.dataset.scheme = theme.dark === false ? 'light' : 'dark';
}

function translate() {
  document.title = t('Panel.Title');
  for (const node of document.querySelectorAll('[data-text]')) node.textContent = t(node.dataset.text);
}

async function load() {
  try {
    const [strings, controls, theme, presets] = await Promise.all(
      [request('/locale'), request('/controls'), request('/theme'), request('/stream-presets')]);
    state.strings = strings;
    state.tabs = controls.tabs;
    state.descriptors = controls.controls;
    state.byId = new Map(controls.controls.map((descriptor) => [descriptor.id, descriptor]));
    state.presets = presets;
    applyTheme(theme);
    translate();
    connect();
  } catch (failure) {
    if (failure.status === 401) {
      store.remove('password');
      showSignIn(t('Panel.WrongPassword'));
    } else if (failure.status === 429) {
      showSignIn(t('Panel.Blocked'));
    } else {
      showStatus(t('Panel.Reconnecting'));
      setTimeout(load, 3000);
    }
  }
}

function showSignIn(message) {
  document.getElementById('panel').hidden = true;
  document.getElementById('signin').hidden = false;
  const error = document.getElementById('signin-error');
  error.textContent = message || '';
  error.hidden = !message;
  document.getElementById('password').focus();
}

document.getElementById('signin-form').addEventListener('submit', (event) => {
  event.preventDefault();
  state.password = document.getElementById('password').value.trim();
  store.set('password', state.password);
  load();
});

document.getElementById('camera-picker').addEventListener('change', (event) => {
  state.camera = event.target.value;
  store.set('camera', state.camera);
  render();
});

// The QR code from OBS carries the password in the fragment, which never reaches the server
const fragment = new URLSearchParams(location.hash.slice(1));
if (fragment.get('password')) {
  state.password = fragment.get('password');
  store.set('password', state.password);
  history.replaceState(null, '', location.pathname);
} else {
  state.password = store.get('password') || '';
}
state.mode = store.get('mode') || 'simple';
if (state.password) load();
else showSignIn();
