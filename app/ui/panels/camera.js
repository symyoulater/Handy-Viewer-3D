// Shows the PC's camera full-size, for a camera panel in the headset.
(async () => {
  const msg = document.getElementById('msg');
  try {
    const stream = await navigator.mediaDevices.getUserMedia({ video: { width: { ideal: 1280 }, height: { ideal: 720 } }, audio: false });
    document.getElementById('v').srcObject = stream;
    msg.remove();
  } catch (e) {
    msg.textContent = 'No camera available. Check Windows Settings → Privacy & security → Camera, and that "Let desktop apps access your camera" is on.';
  }
})();
