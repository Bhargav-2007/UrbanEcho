// UrbanEcho API server
// - Works with MongoDB when available.
// - Falls back to in-memory storage when MongoDB is unavailable.
const express = require('express');
const mongoose = require('mongoose');
const cors = require('cors');

const PORT = Number(process.env.PORT || 3000);
const MONGODB_URI = process.env.MONGODB_URI || 'mongodb://127.0.0.1:27017/urbanecho';

// Keep a bounded fallback store to avoid unbounded memory growth.
const MAX_IN_MEMORY_EVENTS = 10000;
const inMemoryEvents = [];

let mongoReady = false;
let EventModel = null;

const app = express();
app.use(cors());
app.use(express.json({ limit: '64kb' }));

function normalizeConfidence(input) {
  if (typeof input !== 'number' || Number.isNaN(input)) {
    return null;
  }

  // Accept either 0..1 or 0..100 from clients.
  if (input > 1.0 && input <= 100.0) {
    return input / 100.0;
  }

  if (input < 0.0 || input > 1.0) {
    return null;
  }

  return input;
}

function validatePayload(body) {
  const eventType = (body && typeof body.event_type === 'string') ? body.event_type.trim() : '';
  const confidence = normalizeConfidence(body ? body.confidence : undefined);

  if (!eventType) {
    return { ok: false, error: 'event_type must be a non-empty string' };
  }

  if (confidence === null) {
    return { ok: false, error: 'confidence must be a number in 0..1 or 0..100' };
  }

  return { ok: true, event: { event_type: eventType, confidence, timestamp: new Date() } };
}

app.get('/api/health', (_req, res) => {
  res.json({
    ok: true,
    storage: mongoReady ? 'mongodb' : 'memory',
    inMemoryEvents: inMemoryEvents.length,
  });
});

app.post('/api/upload', async (req, res) => {
  const checked = validatePayload(req.body);
  if (!checked.ok) {
    return res.status(400).json({ ok: false, error: checked.error });
  }

  try {
    if (mongoReady && EventModel) {
      await EventModel.create(checked.event);
    } else {
      inMemoryEvents.push(checked.event);
      if (inMemoryEvents.length > MAX_IN_MEMORY_EVENTS) {
        inMemoryEvents.shift();
      }
    }

    return res.json({ ok: true, storage: mongoReady ? 'mongodb' : 'memory' });
  } catch (err) {
    return res.status(500).json({ ok: false, error: 'failed to persist event', details: String(err.message || err) });
  }
});

app.get('/api/events', async (req, res) => {
  const hours = Number(req.query.hours || 24);
  const since = new Date(Date.now() - (Number.isFinite(hours) ? hours : 24) * 60 * 60 * 1000);

  try {
    if (mongoReady && EventModel) {
      const events = await EventModel.find({ timestamp: { $gte: since } })
        .sort({ timestamp: 1 })
        .lean();
      return res.json({ ok: true, storage: 'mongodb', count: events.length, events });
    }

    const events = inMemoryEvents.filter((e) => new Date(e.timestamp) >= since);
    return res.json({ ok: true, storage: 'memory', count: events.length, events });
  } catch (err) {
    return res.status(500).json({ ok: false, error: 'failed to load events', details: String(err.message || err) });
  }
});

async function initMongo() {
  try {
    await mongoose.connect(MONGODB_URI);
    const EventSchema = new mongoose.Schema({
      event_type: { type: String, required: true, trim: true },
      confidence: { type: Number, required: true, min: 0, max: 1 },
      timestamp: { type: Date, default: Date.now, index: true },
    });
    EventModel = mongoose.models.Event || mongoose.model('Event', EventSchema);
    mongoReady = true;
    console.log('[UrbanEcho API] MongoDB connected:', MONGODB_URI);
  } catch (err) {
    mongoReady = false;
    EventModel = null;
    console.warn('[UrbanEcho API] MongoDB unavailable, using in-memory storage.');
    console.warn('[UrbanEcho API] Reason:', String(err.message || err));
  }
}

async function start() {
  await initMongo();
  app.listen(PORT, '0.0.0.0', () => {
    console.log(`[UrbanEcho API] Listening on http://0.0.0.0:${PORT}`);
    console.log('[UrbanEcho API] POST /api/upload');
    console.log('[UrbanEcho API] GET  /api/events?hours=24');
    console.log('[UrbanEcho API] GET  /api/health');
  });
}

start();
