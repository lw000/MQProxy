const { test, expect } = require('@playwright/test');
const { KAFKA_CONFIG, LOGGER_CONFIG, jsonBody, newCalls, stubApis } = require('./helpers');

let calls;

function statusBodyFromCalls(route, state) {
  const kafkaApplied = state.kafkaRestart > 0;
  const loggerApplied = state.loggerRestart > 0;
  return {
    code: 0,
    message: 'ok',
    data: {
      kafka: {
        running: state.kafkaStop > state.kafkaStart ? false : true,
        brokers: kafkaApplied ? state.lastBrokers || KAFKA_CONFIG.brokers : KAFKA_CONFIG.brokers,
        topics: kafkaApplied ? state.lastTopics || KAFKA_CONFIG.topics : KAFKA_CONFIG.topics,
        groupId: kafkaApplied ? state.lastGroup || KAFKA_CONFIG.groupId : KAFKA_CONFIG.groupId,
      },
      logger: {
        level: loggerApplied ? state.lastLevel || LOGGER_CONFIG.level : LOGGER_CONFIG.level,
        flushIntervalMs: loggerApplied
          ? state.lastFlush || LOGGER_CONFIG.flushIntervalMs
          : LOGGER_CONFIG.flushIntervalMs,
      },
      web: { host: '127.0.0.1', port: 18080 },
    },
  };
}

test.beforeEach(async ({ page }) => {
  calls = newCalls();
  await stubApis(page, calls, {
    status: statusBodyFromCalls,
    kafkaSave: (route, state) => {
      const body = route.request().postDataJSON();
      state.lastBrokers = body.brokers;
      state.lastTopics = body.topics;
      state.lastGroup = body.groupId;
      return { code: 0, message: 'saved' };
    },
    loggerSave: (route, state) => {
      const body = route.request().postDataJSON();
      state.lastLevel = body.level;
      state.lastFlush = body.flushIntervalMs;
      return { code: 0, message: 'saved' };
    },
  });
  await page.goto('/web/index.html');
});

test('Kafka 保存后状态仍显示旧配置（未重启不生效）', async ({ page }) => {
  await page.getByTestId('kafka-group').fill('e2e-delayed');
  await page.getByTestId('kafka-save').click();
  await expect(page.getByTestId('status-panel')).toContainText('e2e-group');
  await expect(page.getByTestId('status-panel')).not.toContainText('e2e-delayed');
});

test('Kafka 重启后状态显示新配置与运行中', async ({ page }) => {
  await page.getByTestId('kafka-group').fill('e2e-applied');
  await page.getByTestId('kafka-save').click();
  await page.getByTestId('kafka-restart').click();
  await expect(page.getByTestId('status-panel')).toContainText('e2e-applied');
  await expect(page.getByTestId('kafka-status')).toHaveText('运行中');
  expect(calls.kafkaRestart).toBe(1);
});

test('日志配置重启后运行时等级与刷新间隔更新', async ({ page }) => {
  await page.getByTestId('logger-level').selectOption('5');
  await page.getByTestId('logger-flush').fill('800');
  await page.getByTestId('logger-save').click();
  await expect(page.getByTestId('status-panel')).toContainText('2（运行时）');

  await page.getByTestId('logger-restart').click();
  await expect(page.getByTestId('status-panel')).toContainText('5（运行时）');
  await expect(page.getByTestId('status-panel')).toContainText('800 ms（运行时）');
});

test('非法日志配置返回错误码时页面能正常处理', async ({ page }) => {
  await page.route('**/api/logger/config', (route) => {
    if (route.request().method() === 'POST') {
      return route.fulfill(jsonBody({ code: 1001, message: 'invalid logger config' }));
    }
    return route.fulfill(jsonBody({ code: 0, message: 'ok', data: LOGGER_CONFIG }));
  });

  const responsePromise = page.waitForResponse(
    (response) => response.url().includes('/api/logger/config') &&
                 response.request().method() === 'POST');
  await page.getByTestId('logger-save').click();
  const response = await responsePromise;
  expect((await response.json()).code).toBe(1001);
});
