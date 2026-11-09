/**
 * Copies the built view into the PrismaUI views folder of a Skyrim installation.
 *
 * Usage:
 *   node scripts/deploy.mjs                      # uses %XSE_TES5_GAME_PATH%
 *   node scripts/deploy.mjs "D:\Games\Skyrim SE" # explicit game folder
 *
 * The view always ends up in <game>\PrismaUI\views\PrismaUI-Example-UI\.
 */
import { cp, mkdir, readdir, rm, stat } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const VIEW_FOLDER = 'PrismaUI-Example-UI';

const scriptDir = dirname(fileURLToPath(import.meta.url));
const projectDir = resolve(scriptDir, '..');
const distDir = join(projectDir, 'dist');

const gamePath = process.argv[2] ?? process.env.XSE_TES5_GAME_PATH;

if (!gamePath) {
  console.error(
    'No Skyrim folder given.\n' +
      'Pass the game folder as an argument or set the XSE_TES5_GAME_PATH environment variable.',
  );
  process.exit(1);
}

const gameDir = resolve(gamePath);

try {
  const info = await stat(gameDir);
  if (!info.isDirectory()) {
    throw new Error('not a directory');
  }
} catch {
  console.error(`The Skyrim folder "${gameDir}" does not exist.`);
  process.exit(1);
}

const targetDir = join(gameDir, 'PrismaUI', 'views', VIEW_FOLDER);

try {
  await stat(distDir);
} catch {
  console.error(`"${distDir}" does not exist. Run "npm run build" first.`);
  process.exit(1);
}

await mkdir(targetDir, { recursive: true });

// Remove the previous deployment so that stale hashed assets do not pile up.
for (const entry of await readdir(targetDir)) {
  await rm(join(targetDir, entry), { recursive: true, force: true });
}

await cp(distDir, targetDir, { recursive: true });

console.log(`View deployed to ${targetDir}`);
