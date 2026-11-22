const { test, expect } = require('@playwright/test');
const { KAFKA_CONFIG, jsonBody, newCalls, stubApis } = require('./helpers');

let calls;

test.beforeEach(async ({ page }) => {
  calls = newCalls();
  await stubApis(page, calls);
  await page.goto('/web/index.html');
});

test('Kafka 配置保存发送正确 payload', async ({ page }) => {
  await page.getByTestId('kafka-brokers').fill('127.0.0.1:9099');
  await page.getByTestId('kafka-topics').fill('topic-a\ntopic-b');
  await page.getByTestId('kafka-group').fill('e2e-new');

  const requestPromise = page.waitForRequest(
    (request) => request.url().includes('/api/kafka/config') && request.method() === 'POST');
  await page.getByTestId('kafka-save').click();
  const request = await requestPromise;

  expect(request.postDataJSON()).toMatchObject({
    brokers: '127.0.0.1:9099',
    topics: ['topic-a', 'topic-b'],
    groupId: 'e2e-new',
  });
  expect(calls.kafkaSave).toBe(1);
});

test('Kafka 停止与启动按钮调用对应接口', async ({ page }) => {
  await page.getByTestId('kafka-stop').click();
  await expect.poll(() => calls.kafkaStop).toBe(1);

  await page.getByTestId('kafka-start').click();
  await expect.poll(() => calls.kafkaStart).toBe(1);
});

test('日志配置保存与重启生效发送正确 payload', async ({ page }) => {
  await page.getByTestId('logger-level').selectOption('5');
  await page.getByTestId('logger-flush').fill('800');

  const saveRequestPromise = page.waitForRequest(
    (request) => request.url().includes('/api/logger/config') && request.method() === 'POST');
  await page.getByTestId('logger-save').click();
  const saveRequest = await saveRequestPromise;
  expect(saveRequest.postDataJSON()).toEqual({ level: 5, flushIntervalMs: 800 });

  const restartRequestPromise = page.waitForRequest(
    (request) => request.url().includes('/api/logger/restart') && request.method() === 'POST');
  await page.getByTestId('logger-restart').click();
  await restartRequestPromise;
  expect(calls.loggerRestart).toBe(1);
});

test('非法 Kafka 配置返回错误码时页面能正常处理', async ({ page }) => {
  await page.route('**/api/kafka/config', (route) => {
    if (route.request().method() === 'POST') {
      return route.fulfill(jsonBody({ code: 1001, message: 'invalid kafka config' }));
    }
    return route.fulfill(jsonBody({ code: 0, message: 'ok', data: KAFKA_CONFIG }));
  });

  const responsePromise = page.waitForResponse(
    (response) => response.url().includes('/api/kafka/config') &&
                 response.request().method() === 'POST');
  await page.getByTestId('kafka-save').click();
  const response = await responsePromise;
  expect((await response.json()).code).toBe(1001);
});

test('刷新按钮重新拉取状态', async ({ page }) => {
  const before = calls.status;
  await page.getByTestId('status-refresh').click();
  await expect.poll(() => calls.status).toBe(before + 1);
});
