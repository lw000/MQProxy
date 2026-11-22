const { test, expect } = require('@playwright/test');
const { KAFKA_CONFIG, LOGGER_CONFIG, newCalls, stubApis } = require('./helpers');

let calls;

test.beforeEach(async ({ page }) => {
  calls = newCalls();
  await stubApis(page, calls);
  await page.goto('/web/index.html');
});

test('页面加载并展示后端状态', async ({ page }) => {
  await expect(page).toHaveTitle('MQProxy 管理控制台');
  await expect(page.getByTestId('kafka-status')).toHaveText('运行中');
  await expect(page.getByTestId('status-panel')).toContainText('127.0.0.1:9092');
  await expect(page.getByTestId('status-panel')).toContainText('e2e-group');
});

test('Kafka 配置由 API 填充到表单', async ({ page }) => {
  await expect(page.getByTestId('kafka-brokers')).toHaveValue(KAFKA_CONFIG.brokers);
  await expect(page.getByTestId('kafka-topics')).toHaveValue(KAFKA_CONFIG.topics.join('\n'));
  await expect(page.getByTestId('kafka-group')).toHaveValue(KAFKA_CONFIG.groupId);
  await expect(page.getByTestId('logger-level')).toHaveValue(String(LOGGER_CONFIG.level));
  await expect(page.getByTestId('logger-flush')).toHaveValue(String(LOGGER_CONFIG.flushIntervalMs));
});

test('重启生效按钮触发一次 POST /api/kafka/restart', async ({ page }) => {
  await page.getByTestId('kafka-restart').click();
  expect(calls.kafkaRestart).toBe(1);
  await expect(page.getByTestId('output')).toContainText('/api/status');
});
