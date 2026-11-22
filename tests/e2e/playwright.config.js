const fs = require('fs');
const path = require('path');
const { defineConfig, devices } = require('@playwright/test');

const quarantine = JSON.parse(fs.readFileSync(path.join(__dirname, 'quarantine.json'), 'utf8'));
const excluded = (quarantine.tests || []).map((item) => item.name).filter(Boolean);
const escapeRegExp = (value) => value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
const grepInvert = excluded.length ? new RegExp(excluded.map(escapeRegExp).join('|')) : undefined;

module.exports = defineConfig({
  testDir: '.',
  testMatch: '**/*.spec.js',
  timeout: 30000,
  expect: { timeout: 5000 },
  fullyParallel: false,
  workers: 1,
  grepInvert,
  reporter: [['list'], ['junit', { outputFile: 'test-results/junit.xml' }]],
  use: {
    baseURL: 'http://127.0.0.1:8765',
    trace: 'on-first-retry',
    ...devices['Desktop Chrome'],
  },
  webServer: {
    command: 'python -m http.server 8765 --bind 127.0.0.1',
    url: 'http://127.0.0.1:8765/web/index.html',
    cwd: '../..',
    reuseExistingServer: !process.env.CI,
    timeout: 30000,
  },
});
