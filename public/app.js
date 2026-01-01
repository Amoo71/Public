const statusEl = document.getElementById("status");
const listEl = document.getElementById("list");

function setStatus(msg) {
  statusEl.textContent = msg;
}

async function loadGames() {
  setStatus("Lade Daten …");
  listEl.hidden = true;
  listEl.innerHTML = "";

  try {
    const res = await fetch("/api/games");
    if (!res.ok) throw new Error("HTTP " + res.status);

    const data = await res.json();

    if (!data.games || !data.games.length) {
      setStatus("Keine Spiele gefunden.");
      return;
    }

    setStatus(`Gefunden: ${data.count} Spiele`);

    for (const g of data.games) {
      const li = document.createElement("li");
      li.innerHTML = `
        <a href="${g.url}" target="_blank" rel="noopener noreferrer">
          ${g.title}
        </a>
      `;
      listEl.appendChild(li);
    }

    listEl.hidden = false;
  } catch (err) {
    setStatus("Fehler beim Laden: " + err.message);
  }
}

loadGames();
