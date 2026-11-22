const KAFKA_CONFIG = {
  brokers: '127.0.0.1:9092',
  topics: ['test-topic-1', 'test-topic-2'],
  groupId: 'e2e-group',
  saslUsername: '',
  saslPassword: '',
  sslCaLocation: '',
  sslKeyLocation: '',
  sslCertificateLocation: '',
  securityProtocol: '',
  saslMechanism: '',
  sslEndpointIdentificationAlgorithm: '',
};

const LOGGER_CONFIG = { level: 2, flushIntervalMs: 1500 };

const STATUS_BODY = {
  code: 0,
  message: 'ok',
  data: {
    kafka: {
      running: true,
      brokers: KAFKA_CONFIG.brokers,
      topics: KAFKA_CONFIG.topics,
      groupId: KAFKA_CONFIG.groupId,
    },
    logger: { level: LOGGER_CONFIG.level, flushIntervalMs: LOGGER_CONFIG.flushIntervalMs },
    web: { host: '127.0.0.1', port: 18080 },
  },
};

function jsonBody(body) {
  return { status: 200, contentType: 'application/json', body: JSON.stringify(body) };
}

function newCalls() {
  return {
    status: 0,
    kafkaConfig: 0,
    kafkaSave: 0,
    kafkaStart: 0,
    kafkaStop: 0,
    kafkaRestart: 0,
    loggerConfig: 0,
    loggerSave: 0,
    loggerRestart: 0,
  };
}

function resolveBody(name, defaultBody, overrides, route, calls) {
  const custom = overrides[name];
  return typeof custom === 'function' ? custom(route, calls) : (custom || defaultBody);
}

async function stubApis(page, calls, overrides = {}) {
  await page.route('**/api/status', (route) => {
    calls.status += 1;
    return route.fulfill(jsonBody(resolveBody('status', STATUS_BODY, overrides, route, calls)));
  });

  await page.route('**/api/kafka/config', (route) => {
    if (route.request().method() === 'POST') {
      calls.kafkaSave += 1;
      const body = resolveBody('kafkaSave',
        { code: 0, message: 'saved', data: KAFKA_CONFIG }, overrides, route, calls);
      return route.fulfill(jsonBody(body));
    }
    calls.kafkaConfig += 1;
    const body = resolveBody('kafkaConfig',
      { code: 0, message: 'ok', data: KAFKA_CONFIG }, overrides, route, calls);
    return route.fulfill(jsonBody(body));
  });

  await page.route('**/api/kafka/start', (route) => {
    calls.kafkaStart += 1;
    return route.fulfill(jsonBody(resolveBody('kafkaStart',
      { code: 0, message: 'kafka started' }, overrides, route, calls)));
  });

  await page.route('**/api/kafka/stop', (route) => {
    calls.kafkaStop += 1;
    return route.fulfill(jsonBody(resolveBody('kafkaStop',
      { code: 0, message: 'kafka stopped' }, overrides, route, calls)));
  });

  await page.route('**/api/kafka/restart', (route) => {
    calls.kafkaRestart += 1;
    return route.fulfill(jsonBody(resolveBody('kafkaRestart',
      { code: 0, message: 'kafka restarted with latest config' }, overrides, route, calls)));
  });

  await page.route('**/api/logger/config', (route) => {
    if (route.request().method() === 'POST') {
      calls.loggerSave += 1;
      return route.fulfill(jsonBody(resolveBody('loggerSave',
        { code: 0, message: 'saved', data: LOGGER_CONFIG }, overrides, route, calls)));
    }
    calls.loggerConfig += 1;
    return route.fulfill(jsonBody(resolveBody('loggerConfig',
      { code: 0, message: 'ok', data: LOGGER_CONFIG }, overrides, route, calls)));
  });

  await page.route('**/api/logger/restart', (route) => {
    calls.loggerRestart += 1;
    return route.fulfill(jsonBody(resolveBody('loggerRestart',
      { code: 0, message: 'logger runtime config applied' }, overrides, route, calls)));
  });
}

module.exports = { KAFKA_CONFIG, LOGGER_CONFIG, STATUS_BODY, jsonBody, newCalls, stubApis };
