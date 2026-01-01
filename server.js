import express from "express";
import axios from "axios";
import * as cheerio from "cheerio";
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import { URL } from "url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const app = express();
const PORT = process.env.PORT || 3000;

app.use(express.static("public"));

function loadConfig() {
  const p = path.join(__dirname, "config.json");
  const raw = fs.readFileSync(p, "utf-8");
  const cfg = JSON.parse(raw);

  const hubDomains = (cfg.hubDomains || []).map(s => String(s).trim().toLowerCase()).filter(Boolean);
  const downloadDomains = (cfg.downloadDomains || []).map(s => String(s).trim().toLowerCase()).filter(Boolean);
  const sourceHome = String(cfg.sourceHome || "https://example.com/").trim();

  return { hubDomains, downloadDomains, sourceHome };
}

function hostOf(u) {
  return new URL(u).hostname.toLowerCase();
}

function isInDomains(u, domains) {
  if (!domains || domains.length === 0) return false;
  const h = hostOf(u);
  return domains.some(d => h === d || h.endsWith("." + d));
}

function absUrl(href, base) {
  return new URL(href, base).toString();
}

function normalizeTitle(t) {
  return t.replace(/^\s*Download\s+/i, "").trim();
}

function guessPart(label, url) {
  const s = (label + " " + url).toLowerCase();

  const m =
    s.match(/\bpart\s*0*(\d{1,3})\b/) ||
    s.match(/\bp\s*0*(\d{1,3})\b/) ||
    s.match(/\bdisc\s*0*(\d{1,3})\b/) ||
    s.match(/\bdisk\s*0*(\d{1,3})\b/) ||
    s.match(/(?:\.|_|-)\s*0*(\d{1,3})\b/);

  if (m) return parseInt(m[1], 10);

  const m2 = s.match(/\.(\d{3})\b/);
  if (m2) return parseInt(m2[1], 10);

  return null;
}

app.get("/api/config", (req, res) => {
  try {
    const cfg = loadConfig();
    res.json({ hubDomains: cfg.hubDomains, downloadDomains: cfg.downloadDomains, sourceHome: cfg.sourceHome });
  } catch (e) {
    res.status(500).json({ error: "Failed to load config.json", details: e?.message ?? String(e) });
  }
});

app.get("/api/games", async (req, res) => {
  try {
    const { sourceHome } = loadConfig();
    const { data: html } = await axios.get(sourceHome, {
      headers: {
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120 Safari/537.36",
        "Accept-Language": "en-US,en;q=0.9"
      },
      timeout: 20000
    });

    const $ = cheerio.load(html);
    const items = [];

    $("h3 a").each((_, el) => {
      const titleRaw = $(el).text().trim();
      const href = $(el).attr("href");
      if (!href) return;
      if (!/^download\b/i.test(titleRaw)) return;

      items.push({ title: normalizeTitle(titleRaw), url: href });
    });

    const seen = new Set();
    const deduped = items.filter(i => {
      const k = i.title + "||" + i.url;
      if (seen.has(k)) return false;
      seen.add(k);
      return true;
    });

    res.json({ count: deduped.length, games: deduped });
  } catch (e) {
    res.status(500).json({ error: "Failed to load games.", details: e?.message ?? String(e) });
  }
});

app.get("/api/resolve-hub", async (req, res) => {
  const postUrl = req.query.url;
  if (!postUrl || typeof postUrl !== "string") return res.status(400).json({ error: "Missing url parameter." });

  try {
    const { hubDomains } = loadConfig();
    if (!hubDomains.length) return res.status(400).json({ error: "hubDomains is empty in config.json" });

    const { data: html } = await axios.get(postUrl, {
      headers: { "User-Agent": "Mozilla/5.0", "Accept-Language": "en-US,en;q=0.9" },
      timeout: 20000
    });

    const $ = cheerio.load(html);
    const candidates = [];

    $("a[href]").each((_, a) => {
      const href = $(a).attr("href");
      if (!href) return;
      let u;
      try { u = absUrl(href, postUrl); } catch { return; }
      if (isInDomains(u, hubDomains)) candidates.push(u);
    });

    const hubUrl = candidates.length ? [...new Set(candidates)][0] : null;
    res.json({ hubUrl });
  } catch (e) {
    res.status(500).json({ error: "Failed to resolve hub url.", details: e?.message ?? String(e) });
  }
});

app.get("/api/extract-downloads", async (req, res) => {
  const hubUrl = req.query.url;
  if (!hubUrl || typeof hubUrl !== "string") return res.status(400).json({ error: "Missing url parameter." });

  try {
    const { hubDomains, downloadDomains } = loadConfig();
    if (!hubDomains.length) return res.status(400).json({ error: "hubDomains is empty in config.json" });
    if (!downloadDomains.length) return res.status(400).json({ error: "downloadDomains is empty in config.json" });

    if (!isInDomains(hubUrl, hubDomains)) {
      return res.status(400).json({ error: "URL is not on an allowed hub domain." });
    }

    const { data: html } = await axios.get(hubUrl, {
      headers: { "User-Agent": "Mozilla/5.0", "Accept-Language": "en-US,en;q=0.9" },
      timeout: 20000
    });

    const $ = cheerio.load(html);
    const found = [];

    $("a[href]").each((_, a) => {
      const href = $(a).attr("href");
      if (!href) return;
      const label = $(a).text().trim();
      let u;
      try { u = absUrl(href, hubUrl); } catch { return; }

      if (!isInDomains(u, downloadDomains)) return;

      found.push({ url: u, label: label || u });
    });

    const seen = new Set();
    const deduped = [];
    for (const f of found) {
      if (seen.has(f.url)) continue;
      seen.add(f.url);
      deduped.push(f);
    }

    const parts = deduped.map(x => ({ ...x, part: guessPart(x.label, x.url) }))
      .sort((a,b) => {
        const ap = (a.part ?? 1e9);
        const bp = (b.part ?? 1e9);
        if (ap !== bp) return ap - bp;
        return a.label.localeCompare(b.label);
      });

    res.json({ count: parts.length, links: parts });
  } catch (e) {
    res.status(500).json({ error: "Failed to extract downloads.", details: e?.message ?? String(e) });
  }
});

app.listen(PORT, () => console.log("Server running on port", PORT));
