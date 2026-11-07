// SPDX-License-Identifier: Apache-2.0
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdir, mkdtemp, readFile, readdir, rmdir, unlink, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import test from 'node:test';
import { download, verify } from './download.mjs';

const data = Buffer.from('public toolchain test payload\n');
const digest = createHash('sha256').update(data).digest('hex');
async function workspace(t) {
  await mkdir('build', { recursive: true });
  const path = await mkdtemp(join('build', 'download-test-'));
  t.after(async () => {
    for (const file of await readdir(path)) await unlink(join(path, file));
    await rmdir(path);
  });
  return path;
}
test('verified cached archive is reused without a network call', async t => {
  const path = join(await workspace(t), 'archive');
  await writeFile(path, data);
  const result = await download('https://example.invalid/archive', path, digest);
  assert.equal(result.cached, true);
  assert.equal(result.bytes, data.length);
});
test('cached checksum mismatch fails without overwriting the archive', async t => {
  const path = join(await workspace(t), 'archive');
  await writeFile(path, data);
  await assert.rejects(download('https://example.invalid/archive', path, '0'.repeat(64)), /SHA-256 mismatch/);
  assert.deepEqual(await readFile(path), data);
});
test('successful streamed transfer is verified and published', async t => {
  const path = join(await workspace(t), 'archive');
  const original = globalThis.fetch;
  t.after(() => { globalThis.fetch = original; });
  globalThis.fetch = async () => ({ ok: true, status: 200, url: 'https://example.invalid/archive',
    body: (async function* () { yield data.subarray(0, 4); yield data.subarray(4); })() });
  const result = await download('https://example.invalid/archive', path, digest);
  assert.equal(result.cached, false);
  assert.deepEqual(await readFile(path), data);
});
test('bad transfer never publishes destination or retains a partial file', async t => {
  const directory = await workspace(t);
  const original = globalThis.fetch;
  t.after(() => { globalThis.fetch = original; });
  globalThis.fetch = async () => ({ ok: true, status: 200, url: 'https://example.invalid/archive',
    body: (async function* () { yield data; })() });
  await assert.rejects(download('https://example.invalid/archive', join(directory, 'archive'), '0'.repeat(64)), /SHA-256 mismatch/);
  assert.deepEqual(await readdir(directory), []);
});
test('invalid hashes and HTTP sources fail before network access', async () => {
  await assert.rejects(verify('unused', 'bad'), /64 hexadecimal/);
  await assert.rejects(download('http://example.invalid/archive', 'unused', digest), /HTTPS/);
});
