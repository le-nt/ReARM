// SPDX-License-Identifier: Apache-2.0
// Node's TLS backend is used because Windows Schannel/.NET may be unavailable.
import { createHash } from 'node:crypto';
import { createReadStream } from 'node:fs';
import { mkdir, open, rename, stat, unlink } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

export async function sha256(path) {
  const digest = createHash('sha256');
  for await (const block of createReadStream(path)) digest.update(block);
  return digest.digest('hex');
}

export async function verify(path, expected) {
  if (!/^[a-fA-F0-9]{64}$/.test(expected)) throw new Error('Expected SHA-256 must contain 64 hexadecimal digits');
  const actual = await sha256(path);
  if (actual !== expected.toLowerCase()) throw new Error(`SHA-256 mismatch for ${path}: expected ${expected}, got ${actual}`);
  return { path: resolve(path), sha256: actual, bytes: (await stat(path)).size };
}

export async function download(url, destination, expected) {
  if (new URL(url).protocol !== 'https:') throw new Error('Downloads require HTTPS');
  if (!/^[a-fA-F0-9]{64}$/.test(expected)) throw new Error('Expected SHA-256 must contain 64 hexadecimal digits');
  try {
    await stat(destination);
    return { ...(await verify(destination, expected)), cached: true };
  } catch (error) {
    if (error.code !== 'ENOENT') throw error;
  }
  await mkdir(dirname(resolve(destination)), { recursive: true });
  const temporary = `${destination}.${process.pid}.part`;
  let file;
  try {
    const response = await fetch(url, { signal: AbortSignal.timeout(300000) });
    if (!response.ok || !response.body) throw new Error(`Download failed: HTTP ${response.status} ${url}`);
    if (new URL(response.url).protocol !== 'https:') throw new Error('Refusing a non-HTTPS redirect');
    file = await open(temporary, 'wx');
    for await (const block of response.body) await file.writeFile(block);
    await file.sync();
    await file.close();
    file = undefined;
    const result = await verify(temporary, expected);
    await rename(temporary, destination);
    return { ...result, path: resolve(destination), cached: false };
  } finally {
    if (file) await file.close();
    await unlink(temporary).catch(error => { if (error.code !== 'ENOENT') throw error; });
  }
}

if (process.argv[1] && resolve(process.argv[1]) === resolve(fileURLToPath(import.meta.url))) {
  try {
    const args = process.argv.slice(2);
    const result = args[0] === '--verify' && args.length === 3
      ? await verify(args[1], args[2])
      : args.length === 3 ? await download(...args) : null;
    if (!result) throw new Error('Usage: node tools/download.mjs URL DESTINATION SHA256 | --verify FILE SHA256');
    process.stdout.write(JSON.stringify(result) + '\n');
  } catch (error) {
    process.stderr.write(error.message + '\n');
    process.exitCode = 1;
  }
}
