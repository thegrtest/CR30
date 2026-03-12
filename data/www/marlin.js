document.addEventListener('DOMContentLoaded', () => {
  document.body.classList.add('loading');

  const ws = new WebSocket(`ws://${location.host}/ws`);

  const clearLoadingScreen = () => {
    document.body.classList.remove('loading');
  };

  const trayCountNode = document.getElementById('tray-count');
  const cyclePhaseNode = document.getElementById('cycle-phase');
  const cyclePositionNode = document.getElementById('cycle-position');

  const updateInfoFromLine = (line) => {
    const traysLoaded = line.match(/Trays Loaded:\s*(\d+)/i);
    if (traysLoaded) trayCountNode.textContent = traysLoaded[1];

    const cycle = line.match(/Cycle:\s*(Idle|Forward|Reverse|Complete)/i);
    if (cycle) cyclePhaseNode.textContent = cycle[1][0].toUpperCase() + cycle[1].slice(1).toLowerCase();

    const positionA = line.match(/Tray\s*(\d+)\s*Row\s*(\d+)\s*S(?:lot)?\s*(\d+)/i);
    if (positionA)
      cyclePositionNode.textContent = `Tray ${positionA[1]} • Row ${positionA[2]} • Slot ${positionA[3]}`;

    const positionB = line.match(/Cycle\s*(FWD|REV)\s*T(\d+)\s*R(\d+)\s*S(\d+)/i);
    if (positionB) {
      cyclePhaseNode.textContent = positionB[1].toUpperCase() === 'FWD' ? 'Forward' : 'Reverse';
      cyclePositionNode.textContent = `Tray ${positionB[2]} • Row ${positionB[3]} • Slot ${positionB[4]}`;
    }
  };

  ws.addEventListener('open', () => {
    setTimeout(clearLoadingScreen, 1200);
  });

  ws.addEventListener('error', () => {
    setTimeout(clearLoadingScreen, 2000);
  });

  setTimeout(clearLoadingScreen, 4000);

  ws.onmessage = (e) => {
    if (typeof e.data === 'string') {
      let node = document.createElement('li');
      let text = document.createTextNode(e.data);
      node.appendChild(text);
      document.getElementById('serial-output').appendChild(node);
      updateInfoFromLine(e.data);
    }
  };

  document.getElementById('serial-command-form').addEventListener('submit', (e) => {
    e.preventDefault();

    let value = document.getElementById('serial-command').value.trim();

    if (!value) return;

    ws.send(`${value}\n`);

    document.getElementById('serial-command').value = '';
  });
});
