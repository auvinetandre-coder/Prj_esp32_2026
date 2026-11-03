// Local UI preview only. No ESP proxy, config files, or write endpoints.
const http = require('http');
const fs = require('fs');
const path = require('path');
const files = {
  '/': ['preview_automate.html', 'text/html'],
  '/preview_automate.html': ['preview_automate.html', 'text/html'],
  '/preview-app.js': ['data/www/app.js', 'text/javascript'],
  '/data/www/style.css': ['data/www/style.css', 'text/css']
};
http.createServer((req, res) => {
  const entry = files[new URL(req.url, 'http://localhost').pathname];
  if (req.method !== 'GET' || !entry) { res.writeHead(404); return res.end(); }
  res.writeHead(200, {'Content-Type':entry[1]+'; charset=utf-8','Cache-Control':'no-store'});
  if (entry[1] === 'text/javascript') {
    // Use production functions without starting live API polling in this demo.
    const script = fs.readFileSync(path.join(__dirname, '..', entry[0]), 'utf8');
    return res.end(script.replace(/\nrender\(\);\s*setInterval\(pollActuatorRuntime, 1000\);\s*scheduleDashboardRefresh\(\);\s*refresh\(\);\s*$/, '\n'));
  }
  fs.createReadStream(path.join(__dirname, '..', entry[0])).pipe(res);
}).listen(8766, '127.0.0.1', () => console.log('Preview: http://127.0.0.1:8766/preview_automate.html'));
