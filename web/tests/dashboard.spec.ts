import { test, expect } from "@playwright/test";

test.beforeEach(async ({ page, request }) => {
  expect((await request.post("/__test/scenario/configured")).ok()).toBeTruthy();
  await page.goto("/");
  await page.getByLabel("Password", { exact: true }).fill("e2e-password-only");
  await page.getByRole("button", { name: "Sign in", exact: true }).click();
  await expect(page.getByText("Device online", { exact: true })).toBeVisible({
    timeout: 15000,
  });
});

test("desktop readings, charts, and mobile layout", async ({ page }) => {
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await expect(
    page.getByRole("heading", { name: "A window into the terrarium." }),
  ).toBeVisible();
  await expect(
    page.getByRole("img", { name: "temperature history in Tehran time" }),
  ).toBeVisible();
  await expect(
    page.getByText("Simulation mode", { exact: true }),
  ).toBeVisible();
  await page.screenshot({
    path: "test-results/dashboard-desktop.png",
    fullPage: true,
  });
  await page.setViewportSize({ width: 390, height: 844 });
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth <= window.innerWidth,
    ),
  ).toBe(true);
  await page.screenshot({
    path: "test-results/dashboard-mobile.png",
    fullPage: true,
  });
  await page.getByRole("button", { name: "7 days", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "7 days", exact: true }),
  ).toHaveClass(/selected/);
  expect(errors).toEqual([]);
});

test("light command waits for device acknowledgment", async ({ page }) => {
  await page.getByRole("button", { name: "Light 1 Off", exact: true }).click();
  await expect(page.getByRole("status")).toContainText(
    "Waiting for device acknowledgment",
  );
  await expect(
    page.getByRole("button", { name: "Light 1 On", exact: true }),
  ).toBeDisabled();
  await expect(page.getByRole("status")).toContainText("applied:", {
    timeout: 15000,
  });
  await expect(
    page.getByRole("button", { name: "Light 1 Off", exact: true }),
  ).toHaveClass(/selected/);
});

test("settings save acknowledges a new revision", async ({ page }) => {
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  await page.getByLabel("Day · ON below", { exact: true }).first().fill("23.5");
  await page.getByRole("button", { name: "Save to device" }).click();
  await expect(page.getByRole("status")).toContainText(
    "Waiting for device acknowledgment",
  );
  await expect(page.getByText("Revision 2", { exact: true })).toBeVisible({
    timeout: 15000,
  });
  await expect(
    page.getByLabel("Day · ON below", { exact: true }).first(),
  ).toHaveValue("23.5");
});

test("another browser revision conflict preserves edits and blocks saving", async ({
  page,
  request,
}) => {
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  await page.getByLabel("Day · ON below", { exact: true }).first().fill("23.5");
  await request.post("/__test/scenario/conflict");
  await expect(
    page.getByText("Settings changed on the device while you were editing.", {
      exact: false,
    }),
  ).toBeVisible({ timeout: 15000 });
  await expect(
    page.getByRole("button", { name: "Save to device" }),
  ).toBeDisabled();
  await expect(
    page.getByLabel("Day · ON below", { exact: true }).first(),
  ).toHaveValue("23.5");
  await page.getByRole("button", { name: "Reload settings" }).click();
  await expect(
    page.getByLabel("Day · ON below", { exact: true }).first(),
  ).toHaveValue("24.1");
});

test("offline device disables controls and labels stale data", async ({
  page,
  request,
}) => {
  await request.post("/__test/scenario/offline");
  await expect(page.getByText("Device offline", { exact: true })).toBeVisible({
    timeout: 15000,
  });
  await expect(
    page.getByRole("button", { name: "Light 1 On", exact: true }),
  ).toBeDisabled();
  await expect(
    page.getByText("Awaiting fresh sensor data").first(),
  ).toBeVisible();
});

test("server connection loss disables controls", async ({ page }) => {
  await page.route("**/api/status", (route) => route.abort());
  await expect(page.getByRole("alert")).toContainText(
    "Server connection lost",
    { timeout: 15000 },
  );
  await expect(
    page.getByRole("button", { name: "Light 1 On", exact: true }),
  ).toBeDisabled();
});

test("faults appear and acknowledge through the real API", async ({
  page,
  request,
}) => {
  await request.post("/__test/scenario/fault");
  await expect(
    page.getByText("Latched fault: overtemperature", { exact: true }),
  ).toBeVisible({ timeout: 15000 });
  await page
    .getByRole("button", { name: "Acknowledge resolved faults" })
    .click();
  await expect(
    page.getByText("Latched fault: overtemperature", { exact: true }),
  ).not.toBeVisible({ timeout: 15000 });
});


test("saved light selection survives restart without a clock", async ({ page, request }) => {
  await page.getByRole("button", { name: "Light 1 Off", exact: true }).click();
  await expect(page.getByText("Saved manual mode · survives restart")).toBeVisible({ timeout: 15000 });
  await request.post("/__test/scenario/restart-offline-clock");
  await expect(page.getByText("Holding saved automatic state")).toBeVisible({ timeout: 15000 });
  await expect(page.getByText("Saved manual mode · survives restart")).toBeVisible();
  await expect(page.getByRole("button", { name: "Light 1 Off", exact: true })).toHaveClass(/selected/);
  await expect(page.getByText("Clock unavailable; using saved profile and automatic light states until time returns")).toBeVisible();
});
