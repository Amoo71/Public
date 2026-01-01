import express from "express";
import axios from "axios";
import * as cheerio from "cheerio";

const app = express();
const PORT = process.env.PORT || 3000;

app.use(express.static("public"));

function normalizeTitle(t) {
  return t.replace(/^\s*Download\s+/i, "").trim();
}

app.get("/api/games", async (req, res) => {
  try {
    const { data: html } = await axios.get("https://game3rb.com/", {
      headers: {
        "User-Agent":
          "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120 Safari/537.36"
      },
      timeout: 15000
    });

    const $ = cheerio.load(html);
    const items = [];

    $("h3 a").each((_, el) => {
      const titleRaw = $(el).text().trim();
      const href = $(el).attr("href");
      if (!href) return;
      if (!/^download\b/i.test(titleRaw)) return;

      items.push({
        title: normalizeTitle(titleRaw),
        url: href
      });
    });

    const seen = new Set();
    const deduped = items.filter(i => {
      const key = i.title + i.url;
      if (seen.has(key)) return false;
      seen.add(key);
      return true;
    });

    res.json({
      source: "https://game3rb.com/",
      count: deduped.length,
      games: deduped
    });

  } catch (err) {
    res.status(500).json({
      error: "Fehler beim Laden der Daten",
      details: err.message
    });
  }
});

app.listen(PORT, () => {
  console.log("Server läuft auf Port", PORT);
});
