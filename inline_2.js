
    setTimeout(function () {
      var app = document.getElementById('app');
      if (app && app.textContent.indexOf('Chargement') >= 0) {
        app.innerHTML = '<h1>Application non chargee</h1><div class="banner">Le fichier app.js ne se lance pas ou API sans reponse.</div><p><a href="/lite">Ouvrir interface Lite embarquee</a> <a href="/api/status-lite">Tester API status-lite</a> <a href="/fs">Voir LittleFS</a></p>';
      }
    }, 5000);
  
