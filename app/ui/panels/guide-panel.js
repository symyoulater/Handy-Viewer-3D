// Shows the app's guide (guide.js) on its own page, for the headset.
document.getElementById('guide').innerHTML = window.TPV_GUIDE || '<p>The guide could not be loaded.</p>';
document.querySelectorAll('img').forEach((i) => { i.loading = 'eager'; }); // load every picture, ready for scrolling in the headset
