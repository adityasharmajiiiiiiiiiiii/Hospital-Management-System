/* MediCare HMS - frontend logic (vanilla JS, talks to the C++ REST API) */
'use strict';

const state = { patients: [], doctors: [], appointments: [], bills: [] };
let view = 'dashboard';
const $ = (s) => document.querySelector(s);

/* ---------------- helpers ---------------- */
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const money = (n) => '₹' + Number(n).toLocaleString('en-IN', { minimumFractionDigits: 2, maximumFractionDigits: 2 });
const badge = (text, color) => `<span class="badge ${color}">${esc(text)}</span>`;
const nameOf = (list, id) => (state[list].find((o) => o.id == id) || {}).name || 'Unknown';
const todayStr = () => new Date().toLocaleDateString('en-CA'); // YYYY-MM-DD in local time

async function api(path, method = 'GET', body) {
  const res = await fetch('/api' + path, {
    method,
    headers: { 'Content-Type': 'application/json' },
    body: body ? JSON.stringify(body) : undefined,
  });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(data.error || 'Request failed (' + res.status + ')');
  return data;
}

let toastTimer;
function toast(msg, isError = false) {
  const t = $('#toast');
  t.textContent = msg;
  t.className = 'toast' + (isError ? ' error' : '');
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => (t.hidden = true), 3200);
}

/* ---------------- screen definitions ---------------- */
const statusColor = { Scheduled: 'blue', Completed: 'green', Cancelled: 'gray' };

const screens = {
  patients: {
    title: 'Patients', single: 'Patient',
    cols: [
      ['id', 'ID'], ['name', 'Name'], ['age', 'Age'], ['gender', 'Gender'],
      ['phone', 'Phone'], ['disease', 'Condition'],
    ],
    fields: [
      { n: 'name', l: 'Full name', t: 'text', req: true },
      { n: 'age', l: 'Age', t: 'number', req: true, min: 0, max: 150 },
      { n: 'gender', l: 'Gender', t: 'select', o: ['Male', 'Female', 'Other'], req: true },
      { n: 'phone', l: 'Phone', t: 'tel', req: true },
      { n: 'address', l: 'Address', t: 'text', full: true },
      { n: 'disease', l: 'Condition / diagnosis', t: 'text', full: true },
    ],
  },
  doctors: {
    title: 'Doctors', single: 'Doctor',
    cols: [
      ['id', 'ID'], ['name', 'Name', (r) => 'Dr. ' + esc(r.name)], ['specialization', 'Specialization'], ['phone', 'Phone'],
      ['available', 'Status', (r) => (r.available ? badge('Available', 'green') : badge('Unavailable', 'gray'))],
    ],
    fields: [
      { n: 'name', l: 'Full name', t: 'text', req: true },
      { n: 'specialization', l: 'Specialization', t: 'text', req: true },
      { n: 'phone', l: 'Phone', t: 'tel' },
      { n: 'available', l: 'Available for appointments', t: 'checkbox', def: true },
    ],
    actions: [
      { label: (r) => (r.available ? 'Set unavailable' : 'Set available'), patch: (r) => ({ available: !r.available }) },
    ],
  },
  appointments: {
    title: 'Appointments', single: 'Appointment',
    cols: [
      ['id', 'ID'],
      ['patientId', 'Patient', (r) => esc(nameOf('patients', r.patientId))],
      ['doctorId', 'Doctor', (r) => 'Dr. ' + esc(nameOf('doctors', r.doctorId))],
      ['date', 'Date'], ['time', 'Time'],
      ['status', 'Status', (r) => badge(r.status, statusColor[r.status] || 'gray')],
      ['notes', 'Notes'],
    ],
    fields: [
      { n: 'patientId', l: 'Patient', t: 'ref', ref: 'patients', req: true },
      { n: 'doctorId', l: 'Doctor', t: 'ref', ref: 'doctors', req: true },
      { n: 'date', l: 'Date', t: 'date', req: true, def: () => todayStr() },
      { n: 'time', l: 'Time', t: 'time', req: true },
      { n: 'status', l: 'Status', t: 'select', o: ['Scheduled', 'Completed', 'Cancelled'], def: 'Scheduled' },
      { n: 'notes', l: 'Notes', t: 'textarea', full: true },
    ],
    actions: [
      { label: 'Complete', patch: { status: 'Completed' }, show: (r) => r.status === 'Scheduled' },
      { label: 'Cancel', patch: { status: 'Cancelled' }, show: (r) => r.status === 'Scheduled' },
    ],
  },
  bills: {
    title: 'Billing', single: 'Bill',
    cols: [
      ['id', 'Bill #'],
      ['patientId', 'Patient', (r) => esc(nameOf('patients', r.patientId))],
      ['description', 'Description'],
      ['amount', 'Amount', (r) => money(r.amount)],
      ['date', 'Date'],
      ['paid', 'Status', (r) => (r.paid ? badge('Paid', 'green') : badge('Pending', 'amber'))],
    ],
    fields: [
      { n: 'patientId', l: 'Patient', t: 'ref', ref: 'patients', req: true },
      { n: 'description', l: 'Description', t: 'text', req: true, full: true },
      { n: 'amount', l: 'Amount (₹)', t: 'number', req: true, min: 0, step: '0.01' },
      { n: 'date', l: 'Date', t: 'date', def: () => todayStr() },
      { n: 'paid', l: 'Paid', t: 'checkbox', def: false },
    ],
    actions: [{ label: 'Mark paid', patch: { paid: true }, show: (r) => !r.paid }],
  },
};

/* ---------------- data loading ---------------- */
async function loadAll() {
  const keys = Object.keys(state);
  const lists = await Promise.all(keys.map((k) => api('/' + k)));
  keys.forEach((k, i) => (state[k] = lists[i]));
}

/* ---------------- rendering ---------------- */
async function renderDashboard() {
  const s = await api('/stats');
  const t = todayStr();
  const todays = state.appointments.filter((a) => a.status === 'Scheduled' && a.date === t);
  const upcoming = state.appointments
    .filter((a) => a.status === 'Scheduled' && a.date > t)
    .sort((a, b) => (a.date + a.time).localeCompare(b.date + b.time))
    .slice(0, 5);
  const recent = [...state.patients].slice(-5).reverse();

  const apptItem = (a) =>
    `<li><span><strong>${esc(nameOf('patients', a.patientId))}</strong><br><span class="muted">with Dr. ${esc(nameOf('doctors', a.doctorId))}</span></span>
     <span class="muted">${esc(a.date)} ${esc(a.time)}</span></li>`;

  $('#content').innerHTML = `
    <div class="cards">
      <div class="card"><div class="label">Total patients</div><div class="value">${s.patients}</div></div>
      <div class="card"><div class="label">Doctors</div><div class="value">${s.doctors}</div><div class="sub">${s.availableDoctors} available</div></div>
      <div class="card accent"><div class="label">Appointments today</div><div class="value">${s.scheduledToday}</div><div class="sub">${s.upcoming} upcoming in total</div></div>
      <div class="card"><div class="label">Revenue collected</div><div class="value">${money(s.revenue)}</div></div>
      <div class="card"><div class="label">Pending payments</div><div class="value">${money(s.pending)}</div></div>
    </div>
    <div class="panels">
      <div class="panel"><h3>Today's appointments</h3>
        <ul class="list">${todays.map(apptItem).join('') || '<li class="muted">No appointments scheduled for today.</li>'}</ul></div>
      <div class="panel"><h3>Upcoming appointments</h3>
        <ul class="list">${upcoming.map(apptItem).join('') || '<li class="muted">Nothing upcoming.</li>'}</ul></div>
      <div class="panel"><h3>Recently registered patients</h3>
        <ul class="list">${recent.map((p) => `<li><span><strong>${esc(p.name)}</strong><br><span class="muted">${esc(p.disease || 'No condition noted')}</span></span><span class="muted">${esc(p.age)} / ${esc(p.gender)}</span></li>`).join('') || '<li class="muted">No patients yet.</li>'}</ul></div>
    </div>`;
}

function searchText(r) {
  let txt = Object.values(r).join(' ');
  if (r.patientId) txt += ' ' + nameOf('patients', r.patientId);
  if (r.doctorId) txt += ' ' + nameOf('doctors', r.doctorId);
  return txt.toLowerCase();
}

function renderTable() {
  const cfg = screens[view];
  const q = $('#search').value.trim().toLowerCase();
  const rows = state[view].filter((r) => !q || searchText(r).includes(q));
  if (!rows.length) {
    $('#content').innerHTML = `<div class="table-wrap"><div class="empty">${q ? 'No records match your search.' : 'No ' + cfg.title.toLowerCase() + ' yet. Click "Add" to create one.'}</div></div>`;
    return;
  }
  const head = cfg.cols.map((c) => `<th>${esc(c[1])}</th>`).join('') + '<th></th>';
  const body = rows
    .map((r) => {
      const cells = cfg.cols.map((c) => `<td>${c[2] ? c[2](r) : esc(r[c[0]])}</td>`).join('');
      const acts = (cfg.actions || [])
        .map((a, i) => (!a.show || a.show(r)
          ? `<button class="btn small" data-act="${i}" data-id="${r.id}">${esc(typeof a.label === 'function' ? a.label(r) : a.label)}</button>`
          : ''))
        .join('');
      return `<tr>${cells}<td class="actions">${acts}
        <button class="btn small" data-edit="${r.id}">Edit</button>
        <button class="btn small danger" data-del="${r.id}">Delete</button></td></tr>`;
    })
    .join('');
  $('#content').innerHTML = `<div class="table-wrap"><table><thead><tr>${head}</tr></thead><tbody>${body}</tbody></table></div>`;
}

async function render() {
  const isDash = view === 'dashboard';
  $('#title').textContent = isDash ? 'Dashboard' : screens[view].title;
  $('#addBtn').hidden = isDash;
  $('#search').hidden = isDash;
  if (!isDash) $('#addBtn').textContent = '+ Add ' + screens[view].single;
  try {
    isDash ? await renderDashboard() : renderTable();
  } catch (e) {
    toast(e.message, true);
  }
}

/* ---------------- modal form ---------------- */
let editingId = null;

function fieldHtml(f, rec) {
  const val = rec ? rec[f.n] : typeof f.def === 'function' ? f.def() : f.def;
  const cls = f.full ? ' class="full"' : '';
  const req = f.req ? ' required' : '';
  if (f.t === 'checkbox')
    return `<label class="check full"><input type="checkbox" name="${f.n}" ${val ? 'checked' : ''}> ${esc(f.l)}</label>`;
  let input;
  if (f.t === 'select') {
    const opts = f.o.map((o) => `<option ${o === val ? 'selected' : ''}>${esc(o)}</option>`).join('');
    input = `<select name="${f.n}"${req}>${f.req && !rec ? '<option value="">Select...</option>' : ''}${opts}</select>`;
  } else if (f.t === 'ref') {
    const opts = state[f.ref]
      .map((o) => {
        const label = (f.ref === 'doctors' ? 'Dr. ' : '') + o.name + (f.ref === 'doctors' ? ' (' + o.specialization + (o.available ? '' : ', unavailable') + ')' : ' (#' + o.id + ')');
        return `<option value="${o.id}" ${o.id == val ? 'selected' : ''}>${esc(label)}</option>`;
      })
      .join('');
    input = `<select name="${f.n}"${req}><option value="">Select...</option>${opts}</select>`;
  } else if (f.t === 'textarea') {
    input = `<textarea name="${f.n}" rows="3">${esc(val)}</textarea>`;
  } else {
    const attrs = [f.min != null ? `min="${f.min}"` : '', f.max != null ? `max="${f.max}"` : '', f.step ? `step="${f.step}"` : ''].join(' ');
    input = `<input type="${f.t}" name="${f.n}" value="${esc(val)}" ${attrs}${req}>`;
  }
  return `<label${cls}>${esc(f.l)}${f.req ? ' *' : ''}${input}</label>`;
}

function openForm(id) {
  const cfg = screens[view];
  editingId = id || null;
  const rec = id ? state[view].find((r) => r.id == id) : null;
  if (cfg.fields.some((f) => f.t === 'ref' && !state[f.ref].length)) {
    const need = cfg.fields.filter((f) => f.t === 'ref').map((f) => f.ref).join(' and ');
    return toast('Please add ' + need + ' first.', true);
  }
  $('#modalTitle').textContent = (rec ? 'Edit ' : 'Add ') + cfg.single.toLowerCase();
  $('#formFields').innerHTML = cfg.fields.map((f) => fieldHtml(f, rec)).join('');
  $('#formError').hidden = true;
  $('#modal').showModal();
}

async function submitForm(ev) {
  ev.preventDefault();
  const cfg = screens[view];
  const fd = new FormData($('#form'));
  const body = {};
  cfg.fields.forEach((f) => {
    body[f.n] = f.t === 'checkbox' ? fd.has(f.n) : (fd.get(f.n) || '').toString();
  });
  try {
    await api('/' + view + (editingId ? '/' + editingId : ''), editingId ? 'PUT' : 'POST', body);
    $('#modal').close();
    await loadAll();
    render();
    toast(cfg.single + (editingId ? ' updated' : ' added'));
  } catch (e) {
    $('#formError').textContent = e.message;
    $('#formError').hidden = false;
  }
}

/* ---------------- events ---------------- */
$('#nav').addEventListener('click', async (e) => {
  const b = e.target.closest('button[data-view]');
  if (!b) return;
  document.querySelectorAll('#nav button').forEach((x) => x.classList.toggle('active', x === b));
  view = b.dataset.view;
  $('#search').value = '';
  try { await loadAll(); } catch (err) { toast(err.message, true); }
  render();
});

$('#addBtn').addEventListener('click', () => openForm());
$('#search').addEventListener('input', renderTable);
$('#cancelBtn').addEventListener('click', () => $('#modal').close());
$('#form').addEventListener('submit', submitForm);

$('#content').addEventListener('click', async (e) => {
  const btn = e.target.closest('button');
  if (!btn) return;
  const cfg = screens[view];
  try {
    if (btn.dataset.edit) return openForm(btn.dataset.edit);
    if (btn.dataset.del) {
      if (!confirm('Delete this ' + cfg.single.toLowerCase() + '? This cannot be undone.' +
          (view === 'patients' ? '\nTheir appointments and bills will also be deleted.' : ''))) return;
      await api('/' + view + '/' + btn.dataset.del, 'DELETE');
      toast(cfg.single + ' deleted');
    } else if (btn.dataset.act !== undefined) {
      const r = state[view].find((x) => x.id == btn.dataset.id);
      const a = cfg.actions[+btn.dataset.act];
      await api('/' + view + '/' + r.id, 'PUT', typeof a.patch === 'function' ? a.patch(r) : a.patch);
      toast('Updated');
    } else return;
    await loadAll();
    render();
  } catch (err) {
    toast(err.message, true);
  }
});

/* ---------------- start ---------------- */
(async function init() {
  const status = $('#serverStatus');
  try {
    await loadAll();
    status.textContent = 'Server connected';
    status.className = 'sidebar-foot ok';
  } catch (e) {
    status.textContent = 'Server unreachable';
    status.className = 'sidebar-foot bad';
    toast('Cannot reach the C++ backend. Is it running?', true);
  }
  render();
})();
