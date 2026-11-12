import { defineConfig } from "@playwright/test";
export default defineConfig({
  testDir: "./tests",
  fullyParallel: false,
  workers: 1,
  timeout: 45000,
  use: {
    baseURL: "http://127.0.0.1:8765",
    viewport: { width: 1440, height: 1000 },
    trace: "retain-on-failure",
  },
  webServer: {
    command: "../.venv/bin/python ../tools/e2e_server.py",
    url: "http://127.0.0.1:8765/api/health",
    reuseExistingServer: false,
    timeout: 30000,
  },
});
