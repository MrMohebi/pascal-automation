import { test, expect } from "@playwright/test";

test.beforeEach(async ({ page, request }) => {
  expect((await request.post("/__test/scenario/configured")).ok()).toBeTruthy();
  await page.goto("/");
  await expect(page.getByLabel("Username", { exact: true })).toHaveCount(0);
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

test("equipment shows status and modes for lamp, fan, and humidifier", async ({
  page,
}) => {
  await expect(page.getByLabel("Lamp is on", { exact: true })).toBeVisible();
  await expect(page.getByLabel("Fan is on", { exact: true })).toBeVisible();
  await expect(page.getByLabel("Humidifier is off", { exact: true })).toBeVisible();
  for (const name of ["Lamp", "Fan", "Humidifier"])
    await expect(
      page.getByRole("button", { name: `${name} Auto`, exact: true }),
    ).toHaveClass(/selected/);
  await expect(page.getByText("Day cycle 10 min on / 40 min off")).toBeVisible();
  await expect(page.getByText(/Turns OFF in \d+ min/)).toBeVisible();
  await expect(page.getByText("Schedule 09:00–19:00")).toBeVisible();
});

for (const [name, mode, label] of [
  ["Lamp", "Off", "Lamp is off"],
  ["Fan", "Off", "Fan is off"],
  ["Humidifier", "On", "Humidifier is on"],
] as const)
  test(`${name} ${mode} waits for device acknowledgment`, async ({ page }) => {
    await page.getByRole("button", { name: `${name} ${mode}`, exact: true }).click();
    await expect(page.getByRole("status")).toContainText(
      "Waiting for device acknowledgment",
    );
    await expect(
      page.getByRole("button", { name: `${name} Auto`, exact: true }),
    ).toBeDisabled();
    await expect(page.getByRole("status")).toContainText("applied:", {
      timeout: 15000,
    });
    await expect(
      page.getByRole("button", { name: `${name} ${mode}`, exact: true }),
    ).toHaveClass(/selected/);
    await expect(page.getByLabel(label, { exact: true })).toBeVisible();
    await expect(page.getByText(`Forced ${mode.toUpperCase()} · saved on device`)).toBeVisible();
  });

test("fan cycle settings save to the device", async ({ page }) => {
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  await page.getByLabel(/^Night · ON for/).fill("15");
  await page.getByLabel(/^Night · then OFF for/).fill("45");
  await page.getByRole("button", { name: "Save to device" }).click();
  await expect(page.getByText("Revision 2", { exact: true })).toBeVisible({
    timeout: 15000,
  });
  await expect(page.getByLabel(/^Night · ON for/)).toHaveValue("15");
  await expect(page.getByLabel(/^Night · then OFF for/)).toHaveValue("45");
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
  ).toHaveValue("40.1");
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
    page.getByRole("button", { name: "Lamp Auto", exact: true }),
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
    page.getByRole("button", { name: "Lamp Auto", exact: true }),
  ).toBeDisabled();
});

test("too hot only holds the lamp OFF and needs no acknowledgment", async ({
  page,
  request,
}) => {
  await request.post("/__test/scenario/fault");
  await expect(
    page.getByText("Too hot: lamp held OFF until the temperature drops below 31 °C"),
  ).toBeVisible({ timeout: 15000 });
  await expect(page.getByText("Too hot · OFF until below 31 °C")).toBeVisible();
  await expect(page.getByText("Resting after maximum run · resumes shortly")).toBeVisible();
  await expect(page.getByRole("button", { name: /Acknowledge/ })).toHaveCount(0);
  await expect(page.getByRole("button", { name: "Lamp On", exact: true })).toBeEnabled();
  await expect(page.getByRole("button", { name: "Lamp Off", exact: true })).toBeEnabled();
});


test("saved fan selection survives restart without a clock", async ({ page, request }) => {
  await page.getByRole("button", { name: "Fan Off", exact: true }).click();
  await expect(page.getByText("Forced OFF · saved on device")).toBeVisible({ timeout: 15000 });
  await request.post("/__test/scenario/restart-offline-clock");
  await expect(page.getByText("Holding saved automatic state")).toBeVisible({ timeout: 15000 });
  await expect(page.getByText("Forced OFF · saved on device")).toBeVisible();
  await expect(page.getByRole("button", { name: "Fan Off", exact: true })).toHaveClass(/selected/);
  await expect(page.getByText("Clock unavailable; using saved profile and automatic lamp state until time returns")).toBeVisible();
});

test("chart hover follows the pointer on a wide, letterboxed chart", async ({ page }) => {
  const chart = page.getByRole("img", { name: "temperature history in Tehran time" });
  await expect(chart).toBeVisible();
  await chart.scrollIntoViewIfNeeded();
  const box = (await chart.boundingBox())!;
  // The SVG is height-limited, so the plot is centered inside a wider box.
  const plot = await chart.evaluate((svg: SVGSVGElement) => {
    const m = svg.getScreenCTM()!;
    return { left: m.e + 45 * m.a, right: m.e + 725 * m.a };
  });
  expect(plot.left).toBeGreaterThan(box.x + 10);
  for (const fraction of [0.1, 0.5, 0.9]) {
    const targetX = plot.left + (plot.right - plot.left) * fraction;
    await page.mouse.move(targetX, box.y + box.height / 2);
    const line = page.locator(".temperature .hover-line");
    await expect(line).toHaveCount(1);
    const lineX = await line.evaluate((el) => el.getBoundingClientRect().left);
    // Nearest reading is at most half a sample spacing (~1% of width) away.
    expect(Math.abs(lineX - targetX)).toBeLessThan((plot.right - plot.left) * 0.02);
    await expect(page.locator(".temperature .chart-caption")).toContainText("°C");
    await expect(page.locator(".temperature .hover-dot")).toBeVisible();
  }
});


test("fan and humidifier can be switched off for the whole night", async ({ page }) => {
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  const fanNight = page
    .locator("section", { hasText: "Exhaust fan" })
    .getByLabel("Run during the night");
  const mistNight = page
    .locator("section", { hasText: "Humidity" })
    .getByLabel("Run during the night");
  await expect(fanNight).toBeChecked();
  await fanNight.uncheck();
  await mistNight.uncheck();
  await expect(page.getByLabel(/^Night · ON for/)).toBeDisabled();
  await page.getByRole("button", { name: "Save to device" }).click();
  await expect(page.getByText("Revision 2", { exact: true })).toBeVisible({
    timeout: 15000,
  });
  await expect(fanNight).not.toBeChecked();
  await expect(mistNight).not.toBeChecked();
  await expect(page.locator("section", { hasText: "Exhaust fan" }).getByLabel("Run during the day")).toBeChecked();
});
