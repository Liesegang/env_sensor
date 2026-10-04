import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: './tests/e2e',
  fullyParallel: true,
  reporter: 'list',
  use: { baseURL: 'http://127.0.0.1:5178', headless: true,
    ...(process.env.PLAYWRIGHT_CHANNEL ? { channel: process.env.PLAYWRIGHT_CHANNEL } : {}) },
  webServer: { command: 'npm run dev', url: 'http://127.0.0.1:5178', reuseExistingServer: !process.env.CI },
});
